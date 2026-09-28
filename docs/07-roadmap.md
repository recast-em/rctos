# 07 – Roadmap

Jeder Meilenstein hat ein prüfbares **Abnahmekriterium**. Die Reihenfolge ist so gewählt,
dass nie zwei große Unbekannte gleichzeitig offen sind: Chromium läuft zuerst ohne GPU, die
GPU zuerst ohne Chromium, alles zuerst in QEMU.

Aufwand: S = Tage, M = Wochen, L = Monate, XL = viele Monate.

## M0 – Werkzeugkette und Boot (S–M)

- C11-Grundgerüst: Compiler-Flags, Linker-Skript, `-ffreestanding` für den Kernel ([00](00-vision.md#getroffene-entscheidungen))
- Cross-Toolchain, Build-System, Boot-Image-Werkzeug
- Limine-Boot unter QEMU (x86-64, UEFI über OVMF), serielle Ausgabe
- CI: Build, QEMU-Boot, Größenmessung des Kernels

**Abnahme:** `hallo` auf der seriellen Konsole in QEMU; die CI misst `.text`.

## M1 – Kernel-Kern (L)

- Physischer Speicher, Direct-Map, Adressräume, VMO, Reservierungen
- Threads, Scheduler (präemptiv, 32 Stufen, tickless), SMP
- Handles, Rechte, Kontingente
- Kanäle, Events, Ports, Futex, Timer
- Syscall-Eintritt, System-Kit vollständig
- Kernel-Testprogramm im Userspace (`ktest`)

**Abnahme:** `ktest` läuft auf 1 und 4 CPUs ohne Fehler; die Latenzmessung liegt vor; der
Kernel ist ≤ 256 KiB groß.

## M2 – Basis-Userland und Budget-Nachweis (M)

- `init` mit Namensdienst und Starter, Profil-Datei
- `devmgr`: ACPI-Tabellen (ohne AML), PCI über ECAM, Treiber-Manifeste
- Treiber-Kit, Treiber `uart16550`, `efifb`, `ps2`, `pci`
- `shell` (einfache Befehlszeile über die serielle Konsole oder `efifb`)
- `tools/budget`

**Abnahme:** Das Profil `minimal` bleibt in QEMU unter **512 KiB RAM**, und die CI prüft das.
Eine Demo schreibt über `display.lease` und `map_framebuffer` direkt in den Framebuffer, und
ein Widerruf funktioniert.

## M3 – libc, Speicher und Netz (M–L)

- musl-Port, POSIX-Schicht, `libc.a` und `libc.so` mit Modul-Lader
- `virtio-blk`, Dateidienst (ext2 und FAT32, siehe offene Entscheidungen)
- `virtio-net`, Netzdienst (lwIP), DHCP, DNS
- Portierte Testprogramme (z. B. `lua`, `curl` gegen BoringSSL)

**Abnahme:** `curl https://…` funktioniert in QEMU.

## M4 – Chromium in Software (XL)

- Toolchain, GN-Plattform `rctos`, Umgang mit Chromiums eigenen Rust-Komponenten ([06](06-chromium.md#build))
- base, PartitionAlloc, V8-Plattform, Mojo im Einzelprozessmodus
- Ozone `rctos` Stufe G0

**Abnahme:** `content_shell --single-process` zeigt in QEMU eine Webseite mit JavaScript an.

## M5 – Chromium mit mehreren Prozessen (L)

- Prozessstart, Mojo über Prozessgrenzen, Kontingente pro Renderer
- Handle-basierte Sandbox
- Eingabe (Tastatur, Maus), Schriften

**Abnahme:** Mehrere Tabs laufen in getrennten Renderern; ein abgestürzter Renderer reißt den
Browser nicht mit.

## M6 – Echte x86-64-Hardware (L)

- `xhci`, `usb-hid`, `usb-storage`, `ahci`, `nvme`, `e1000e`, `r8169`
- KPTI, Ausschalten über `_S5` ([08](08-plattformen.md))
- `intel-igpu` mit Anzeigestufe **D1** (Page-Flip, VBlank)
- Liste getesteter Geräte (`docs/hardware.md`)

**Abnahme:** Auf mindestens drei Referenzgeräten (Gen9, Gen11, Gen12) bootet das System bis
Chromium G0 mit ruckelfreiem Page-Flip.

## M7 – Intel-GPU-Beschleunigung (XL)

- `intel-igpu`: GGTT, PPGTT, Execlists, Kontexte, Fences, Reset, Workarounds
- Mesa `anv` mit rctos-Backend, WSI, externe Speicher-Erweiterung
- Vorstufe in QEMU: `virtio-gpu` mit `venus`
- Chromium G1

**Abnahme:** `vkcube` läuft auf allen Referenzgeräten; Chromium rendert mit Vulkan; WebGL-
Beispiele laufen über ANGLE.

## M8 – Zero-Copy, Modesetting und Energie (L)

- Chromium G2 (NativePixmap, Overlays)
- Anzeigestufe D2 (externe Monitore) und D3 (Planes)
- Energie: CPU-Leerlaufzustände, RC6, DMC-Firmware, AML-Interpreter für Akku und Deckel

**Abnahme:** Ein Video spielt über eine Overlay-Plane; ein externer Monitor funktioniert;
die Akkulaufzeit ist dokumentiert.

## M9 – AArch64 (L–XL)

- Architekturschicht AArch64 ([08](08-plattformen.md)), zuerst `qemu-system-aarch64 -M virt`
  mit virtio und Venus
- Wahl des Referenzboards, Treiber für Display, GPU (Mesa `v3dv` bzw. `panvk`), Speicher und Netz

**Abnahme:** Dieselben Binärpakete, nur neu kompiliert, erreichen auf dem Referenzboard den
Stand von M7.

## M10 – Produktreife (dauerhaft)

- Updates (A/B-Partitionen), signierte Pakete, Wiederherstellung
- Audio, WLAN, Bluetooth
- Barrierefreiheit, Lokalisierung
- Laufende Chromium-Pflege im Extended-Stable-Takt
