# 06 – Chromium, der Gast

## Der Gast

Chromium ist die eine große Ausnahme vom Leitbild ([00](00-vision.md#zwei-welten)): ein
riesiges Projekt, das die Denkweise der Mikro-Welt nicht teilt, aber das Web mitbringt und
die Grundlage der Fensterverwaltung ist. Deshalb bekommt es einen klaren Rahmen. **Der
Gast darf groß sein, aber nicht übergriffig.**

1. **Kontingent statt Anspruch.** Der Gast lebt in einem festen Kontingent aus `main.cfg`
   (`guest_memory`). Bei Speicherdruck bekommt er das Ereignis und verwirft Tabs; stellt er
   mehr Speicher an, scheitert die Anforderung, und nur er selbst ist betroffen.
2. **Der Bildschirm ist geliehen.** Der Gast zeichnet nur, solange er die Lease hält. Endet
   er oder stürzt er ab, steht die Bodenkonsole wieder da ([05](05-grafik.md#zwei-welten-auf-einem-bildschirm)).
3. **Keine Systemrechte.** Kein `INSPECT`, keine Anmeldung, keine Treiber, keine
   Ressourcen. Passwörter der Anmeldung sieht er nie.
4. **Leise.** Kein Netzverkehr, den der Nutzer nicht auslöst: keine Google-Dienste, keine
   Telemetrie, keine Absturzberichte nach außen, kein Komponenten-Updater, keine
   Hintergrund-Synchronisation. Das regeln Build-Argumente, nicht Einstellungen.
5. **Alles an einem Ort.** Seine Daten liegen in `/usr/<uid>/surf`, sein Paket unter
   `/app/surf`. Was er mitbringt (musl-libc, Mesa-Treibermodul, ICU, Schriften), gehört
   zum Paket und wird mit ihm gemessen und ersetzt.
6. **POSIX nur hier.** Die POSIX-Schicht existiert nur im Gast. Die Mikro-Welt bekommt
   dadurch keine einzige Zeile Unix-Erbe.
7. **Ersetzbar.** Ein Update des Gasts ist der Austausch eines Pakets. Die Mikro-Welt hängt
   an keiner seiner Schnittstellen außer den Protokollen, die sie selbst definiert.

## Schlank einbetten

Chromium besteht grob aus der Content-Schicht (Blink, V8, Netzwerk, GPU, Viz) und dem
Chrome-Browser darüber (Profile, Synchronisation, Erweiterungen, Einstellungen, Ash). Die
Empfehlung: **nur die Content-Schicht einbetten**, mit einem eigenen, kleinen Einbetter
`surf` (der Name kommt von der Browser-App in RCP-OS). Den gleichen Weg gehen Fuchsias
WebEngine und die Cast-Geräte.

| | Content-Einbettung (`surf`) | Vollständiger Chrome-Browser mit Ash |
|---|---|---|
| Umfang | Content-Schicht plus einige tausend Zeilen Einbetter | zusätzlich das gesamte `//chrome` |
| Fensterverwaltung | eigene, kleine Verwaltung auf Aura: Fenster, Leiste, Fokus | Ash, die Fensterverwaltung von ChromeOS |
| Browser-Funktionen | Adressleiste, Tabs, Downloads, Berechtigungen: selbst gebaut, schmal | alles vorhanden, auch was niemand braucht |
| Google-Dienste, Sync, Erweiterungen | gar nicht erst vorhanden | abzuschalten |
| Portierungsaufwand | geringer | deutlich höher |

Die Entscheidung fällt in M4. Bis dahin ist `content_shell` das Ziel der Portierung.

## Strategie der Portierung

Chromium läuft offiziell schon auf zwei Systemen ohne Unix-Kernel: auf **Windows** und auf
**Fuchsia**. Fuchsia ähnelt rctos stark: ein Mikrokernel mit Handles, Kanälen, VMOs und
Ports, eine POSIX-Teilmenge nur als Bibliothek und Chromium als Oberfläche. Deshalb ist
der Fuchsia-Port **die Vorlage** für rctos. Die Kernel-Objekte von rctos sind so gewählt,
dass sich die Fuchsia-Implementierungen fast 1:1 übertragen lassen.

In Chromium gilt `BUILDFLAG(IS_POSIX)` auch für Fuchsia. Chromium setzt also selbst dort
eine libc mit POSIX-Teilmenge voraus. rctos übernimmt das: **`IS_RCTOS` und `IS_POSIX`**.
Überall, wo es um Prozesse, IPC, Speicher oder Ereignisschleifen geht, gibt es dagegen
eigenen Code.

## libc und POSIX-Schicht, nur für den Gast

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
- **Ausgeliefert** wird die libc als Teil des Gast-Pakets: `libc.a` für die statisch
  gelinkten Prozesse des Gasts und `libc.so` für seinen GPU-Prozess, der Treibermodule lädt
  ([04](04-treibermodell.md#treiberpakete-und-treibermodule)). Die Mikro-Welt benutzt sie
  nicht.

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
| Einbetter (`surf`) | Namensraum der Sitzung (fs, net, display, input, audio, fonts), `EXEC`, Starter-Recht `process.spawn` |
| GPU | Mojo-Kanal, Kanäle zu `gpu` und `display`, `libc.so` + Treibermodul, `EXEC` für den Shader-JIT von SwiftShader im Rückfall |
| Renderer | Mojo-Kanal, `EXEC` (V8), sonst nichts |
| Netzwerk | Mojo-Kanal, Kanal zum Netzdienst |
| Utility | Mojo-Kanal, je nach Aufgabe |

Der Einbetter startet seine Kinder selbst über `libusr` im eigenen Kontingent. Für
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
| `PlatformWindow` | ein Vollbildfenster pro Anzeige; die Fenster darin verwaltet der Einbetter (Aura) |
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
- **Rust:** rctos selbst enthält kein Rust ([00](00-vision.md#entscheidungen)).
  Neuere Chromium-Versionen bringen aber eigene Rust-Komponenten mit, die `std` verwenden.
  Das ist ein **erkanntes Risiko**. Möglichkeiten, in dieser Reihenfolge der Präferenz:
  1. die betroffenen Komponenten per GN-Argument abschalten bzw. durch C++-Alternativen
     ersetzen, solange Chromium das zulässt;
  2. die `linux-musl`-Variante von `std` gegen den rctos-musl-Port bauen. Das geht nur,
     wenn die libc die wenigen direkten Linux-Syscalls von `std` übersetzt, etwa `futex`.
     Das bleibt reiner Fremdcode im Chromium-Build, nicht Teil von rctos;
  3. eine eigene Rust-Zielplattform mit `std`-Port (aufwendig, nur als letzter Ausweg).

  Entschieden wird in M4, nachdem geprüft wurde, welche Komponenten zu diesem Zeitpunkt
  zwingend sind.
- **Ziel für den Anfang** ist `content_shell`, danach der Einbetter `surf`
  ([Schlank einbetten](#schlank-einbetten)).
- **Build-Argumente** für einen leisen Gast (ohne Google-Dienste, Telemetrie,
  Absturzberichte, Komponenten-Updater) stehen gesammelt in `ports/chromium/args.gn`.
  Jede Abweichung davon ist eine dokumentierte Entscheidung.

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
| Einbetter `surf` (Fensterverwaltung, Adressleiste, Tabs, Downloads) | mittel | gering |
| Build-System und Toolchain | mittel | **hoch (Chromiums Rust-Komponenten)** |
| laufende Pflege | dauerhaft | hoch, wenn die Patches ausufern |

Die Chromium-Portierung ist mit Abstand der größte Einzelposten des Projekts. Kernel und
Treiber sind dagegen überschaubar, und das soll so bleiben: Der Gast rechtfertigt keine
Schicht in der Mikro-Welt, die es ohne ihn nicht gäbe.
