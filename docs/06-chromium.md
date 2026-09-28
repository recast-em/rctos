# 06 – Chromium-Portierung

## Strategie

Chromium läuft offiziell schon auf zwei Systemen ohne Unix-Kernel: auf **Windows** und auf
**Fuchsia**. Fuchsia ähnelt rctos stark: ein Mikrokernel mit Handles, Kanälen, VMOs und
Ports, eine POSIX-Teilmenge nur als Bibliothek und Chromium als Oberfläche. Deshalb ist
der Fuchsia-Port **die Vorlage** für rctos. Die Kernel-Objekte von rctos sind so gewählt,
dass sich die Fuchsia-Implementierungen fast 1:1 übertragen lassen.

In Chromium gilt `BUILDFLAG(IS_POSIX)` auch für Fuchsia. Chromium setzt also selbst dort
eine libc mit POSIX-Teilmenge voraus. rctos übernimmt das: **`IS_RCTOS` und `IS_POSIX`**.
Überall, wo es um Prozesse, IPC, Speicher oder Ereignisschleifen geht, gibt es dagegen
eigenen Code.

## libc und POSIX-Schicht

- **Grundlage ist musl**, als Port nach rctos. Die Linux-Syscalls von musl werden durch
  System-Kit-Aufrufe und User-Kit-Stubs ersetzt.
- **Dateideskriptoren gibt es nur in der libc:** Eine Tabelle in der libc bildet
  `int fd` auf einen Kanal zum Datei- oder Netzdienst ab. Fuchsia macht es mit `fdio` genauso.
- **Vorhanden:** `pthread_*` auf Futex-Basis, `open`/`read`/`write`/`close`/`stat`/`readdir`,
  BSD-Sockets, `clock_gettime`, `nanosleep`, `mmap` für anonymen Speicher und für
  VMO-gestützte Dateien, `getrandom`, `dlopen`, aber nur für Treibermodule.
- **Nicht vorhanden:** `fork`, `exec*` (dafür `posix_spawn` über `usr_process_spawn`),
  Signale außer einer Emulation von `raise`/`abort`, Unix-Rechte, `ioctl` außer ein paar
  Terminal-Aufrufen.
- **Ausgeliefert** wird die libc statisch (`libc.a`) für das Basissystem und als `libc.so`
  für Prozesse, die Treibermodule laden ([04](04-treibermodell.md#treiberpakete-und-treibermodule)).

## Plattformcode

| Chromium-Bereich | Fuchsia-Vorlage (u. a.) | rctos-Umsetzung |
|---|---|---|
| Threads, Locks | `base/threading/platform_thread_*`, `lock_impl_posix` | pthreads aus der libc |
| Ereignisschleife | `base/message_loop/message_pump_fuchsia.cc` | `MessagePumpRctos` auf Basis von `Port` |
| Shared Memory | `base/memory/platform_shared_memory_region_fuchsia.cc` | VMO |
| Prozesse | `base/process/launch_fuchsia.cc`, `process_fuchsia.cc` | `libusr`-Lader, `sys_process_*` |
| Zeit | `base/time/time_fuchsia.cc` | Zeitseite bzw. `sys_clock_get` |
| Zufall | `base/rand_util_fuchsia.cc` | `sys_random_get` |
| Seitenverwaltung von PartitionAlloc | `page_allocator_internals_fuchsia.h` | `sys_vm_reserve`, `sys_vm_map`, `sys_vm_protect` |
| V8-Plattform | `v8/src/base/platform/platform-fuchsia.cc` | dieselben Aufrufe, JIT über Doppel-Einblendung und `EXEC`-Ressource |
| Mojo-Kanal | `mojo/core/channel_fuchsia.cc`, `PlatformHandle` | `Channel` mit Handle-Übertragung |
| Sandbox | `sandbox/policy/fuchsia/` | Kindprozess bekommt nur die nötigen Handles |
| Dateien, Netz | POSIX | libc-POSIX-Schicht |
| Schriften | Fuchsia-Fontdienst | Skia `SkFontMgr_custom` (Verzeichnis) oder ein kleiner Fontdienst |
| Absturzberichte | Crashpad-Fuchsia | anfangs abgeschaltet |

### Speicherbedarf an den Kernel

- **Große Reservierungen:** V8 reserviert für seine Sandbox bis zu 1 TiB Adressraum,
  PartitionAlloc reserviert Pools von mehreren GiB. `sys_vm_reserve` kostet nur ein
  Kernel-Objekt, und der 47-Bit-Benutzeradressraum (128 TiB) auf x86-64 und AArch64 reicht
  dafür.
- **JIT:** Ein VMO wird einmal RW und einmal RX eingeblendet. Der Kernel erlaubt nie W+X auf
  derselben Einblendung ([01](01-kernel.md#virtueller-speicher)).

## Prozessmodell

| Chromium-Prozess | Handles bei Start |
|---|---|
| Browser | Namensraum (fs, net, display, input, audio, fonts), `EXEC`, Starter-Recht `process.spawn` |
| GPU | Mojo-Kanal, Kanäle zu `gpu` und `display`, `libc.so` + Treibermodul, `EXEC` für den Shader-JIT von SwiftShader im Rückfall |
| Renderer | Mojo-Kanal, `EXEC` (V8), sonst nichts |
| Netzwerk | Mojo-Kanal, Kanal zum Netzdienst |
| Utility | Mojo-Kanal, je nach Aufgabe |

Der Browser-Prozess startet seine Kinder selbst über `libusr` im eigenen Kontingent. Für
Geräte mit wenig RAM gibt es `--renderer-process-limit` und den Low-End-Modus von Chromium.
Für die Inbetriebnahme gibt es `--single-process`, das für den Produktivbetrieb nicht
geeignet ist.

## Ozone-Plattform „rctos“

Ozone ist die Plattformschicht für Fenster, Grafik und Eingabe in Chromium. Neu entsteht
`ui/ozone/platform/rctos/`. Als Vorlagen dienen `headless` (klein), `drm` (direkte Anzeige,
Page-Flip, Overlays) und `flatland` (Fuchsia).

| Ozone-Schnittstelle | Umsetzung |
|---|---|
| `OzonePlatform` | Initialisierung, Verbindung zu `display` und `input` |
| `PlatformWindow` | ein Vollbildfenster pro Anzeige; Ash verwaltet die Fenster innerhalb von Chromium |
| `PlatformScreen` | Anzeigen und Modi aus `display.list` |
| `SurfaceFactoryOzone::CreateCanvasForWidget` | **G0:** Software-Ausgabe, kopiert in den per `map_framebuffer` eingeblendeten Speicher |
| `SurfaceFactoryOzone::CreateVulkanImplementation` | **G1/G2:** `VulkanImplementationRctos` lädt das Treibermodul, das `gpu.query(MODULE)` liefert |
| `CreateNativePixmap` | **G2:** Grafikpuffer über `gpu.buffer_create` |
| `OverlayManagerOzone` | **G2:** Planes der Anzeige |
| Eingabe | Ereignisse aus dem Eingabedienst, umgesetzt in `ui::Event` (Vorlage: `ui/events/ozone/evdev`) |

## Build

- **GN:** `target_os = "rctos"` und `target_cpu = "x64"` bzw. `"arm64"`, dazu neue Dateien
  unter `build/config/rctos/` und `build/toolchain/rctos/`, sowie `IS_RCTOS` in
  `build/build_config.h`.
- **Compiler:** das mitgelieferte Clang von Chromium mit `--target=x86_64-unknown-rctos`
  bzw. `aarch64-unknown-rctos`, `--sysroot` auf den musl-Port und libc++ aus dem
  Chromium-Baum. Clang akzeptiert unbekannte OS-Namen im Triple, eine Anpassung an LLVM ist
  nicht nötig.
- **Rust:** Neuere Chromium-Versionen enthalten Rust-Komponenten, die `std` verwenden.
  Das ist ein **erkanntes Risiko**. Möglichkeiten:
  1. eine eigene Rust-Zielplattform mit `std`-Port (sauber, aber aufwendig);
  2. die `linux-musl`-Variante von `std` gegen den rctos-musl-Port. Das geht nur, wenn die
     libc die wenigen direkten Linux-Syscalls von `std` übersetzt, etwa `futex`;
  3. die betroffenen Komponenten per GN-Argument abschalten, solange das möglich ist.

  Entschieden wird in M4, nachdem geprüft wurde, welche Komponenten zu diesem Zeitpunkt
  zwingend sind.
- **Ziel für den Anfang** ist `content_shell`, erst danach der vollständige Browser mit Ash.

## Pflege

- Neue Chromium-Hauptversionen erscheinen etwa alle 4 Wochen. rctos folgt dem **Extended
  Stable**-Kanal (alle 8 Wochen).
- Die rctos-Änderungen liegen als Patch-Stapel in `ports/chromium/patches/`, nach
  Bereich geordnet (build, base, mojo, v8, partition_alloc, ozone, gpu, sandbox).
- Ein Upstreaming ist wenig wahrscheinlich, weil Chromium neue Betriebssysteme nur zögerlich
  aufnimmt. Die Patches sollen deshalb **eng am Fuchsia-Code** bleiben, damit ein Rebase
  meist mechanisch bleibt.

## Aufwand und Risiken

| Bereich | Aufwand | Risiko |
|---|---|---|
| musl-Port und POSIX-Schicht | mittel | gering |
| base, PartitionAlloc, V8-Plattform | mittel | mittel (Speicherfeinheiten) |
| Mojo und Prozessstart | mittel | gering (Fuchsia-Vorlage) |
| Ozone G0 | gering | gering |
| Ozone G1/G2 und Vulkan-Erweiterungen | hoch | mittel |
| Build-System und Toolchain | mittel | **hoch (Rust `std`)** |
| laufende Pflege | dauerhaft | hoch, wenn die Patches ausufern |

Die Chromium-Portierung ist mit Abstand der größte Einzelposten des Projekts. Kernel und
Treiber sind dagegen überschaubar.
