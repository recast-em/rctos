# rctos

rctos ist ein kleines Betriebssystem, das nicht auf Unix aufbaut. Es besteht aus einem
Capability-basierten Mikrokernel, und seine grafische Oberfläche ist eine GPU-beschleunigte
Chromium-Instanz. Dieselbe Basis soll auf Embedded-Geräten und auf vollständigen Desktops laufen.

> **Status:** Architekturphase. Es gibt noch keinen Code, nur die Dokumente unter `docs/`.

## Eckdaten

| Thema | Festlegung |
|---|---|
| Kernel | Mikrokernel mit Image ≤ 1 MiB (Ziel: ≤ 256 KiB) |
| RAM-Bedarf | Basissystem ohne GUI und Netz im Leerlauf: ≤ 512 KiB resident |
| Multitasking | präemptiv, SMP, 32 feste Prioritätsstufen |
| Rechte | ausschließlich über Handles (Capabilities); kein Root, keine globalen Namen im Kernel |
| Direktzugriff | erlaubt, wenn er ausdrücklich vergeben wurde, etwa ein flach gemappter Framebuffer oder IO-Ports |
| Treiber | laufen als Prozesse und sind über Geräteklassen-Protokolle austauschbar |
| API | flache C-ABI in drei Kits: System-Kit, Treiber-Kit, User-Kit |
| Binärformat | statisch gelinktes ELF. Einzige Ausnahme sind Treibermodule (Vulkan-ICDs) mit festen Regeln |
| Erstes Ziel | x86-64 mit Intel-iGPU der Generationen Gen9 bis Gen12 (ältere Business-Laptops und -PCs) |
| Eigentliches Ziel | ARM64 (AArch64) |
| Oberfläche | Chromium über eine eigene Ozone-Plattform; Vulkan über Mesa |

## Dokumente

| Nr. | Dokument | Inhalt |
|---|---|---|
| 00 | [Vision und Budgets](docs/00-vision.md) | Ziele, Nicht-Ziele, Prinzipien, Speicherbudgets, Profile, offene Entscheidungen |
| 01 | [Kernel](docs/01-kernel.md) | Objektmodell, Speicher, Scheduler, IPC, Interrupts, Boot |
| 02 | [Syscalls und Kits](docs/02-syscalls-und-kits.md) | ABI, Nummernkreise, vollständige Aufrufliste, Stabilitätsregeln |
| 03 | [Rechte](docs/03-rechte.md) | Handle-Rechte, Ressourcen, Manifeste, Direktzugriff, Sicherheitsgrenzen |
| 04 | [Treibermodell](docs/04-treibermodell.md) | Gerätemanager, Matching, Protokolle, Austausch, Treiberpakete |
| 05 | [Grafik](docs/05-grafik.md) | Anzeige- und GPU-Protokoll, Intel-iGPU-Treiber, Mesa-Anbindung |
| 06 | [Chromium-Portierung](docs/06-chromium.md) | POSIX-Schicht, Plattformcode, Ozone-Plattform, Build, Pflege |
| 07 | [Roadmap](docs/07-roadmap.md) | Meilensteine mit Abnahmekriterien |
| 08 | [Plattformen](docs/08-plattformen.md) | x86-64 und AArch64: Architekturschicht, Boot, Referenz-Hardware |

## Geplante Verzeichnisstruktur

```
kernel/            Mikrokernel
  core/            architekturunabhängiger Teil
  arch/x86_64/     Architekturschicht x86-64
  arch/aarch64/    Architekturschicht AArch64
kits/
  sys/             System-Kit (Header + Syscall-Stubs)
  drv/             Treiber-Kit
  usr/             User-Kit (flache Stubs auf Dienst-Protokolle)
services/          init (mit Namensdienst und Starter), devmgr, fs, net, …
drivers/           uart16550, efifb, ps2, pci, virtio-*, xhci, intel-igpu, …
protocols/         Geräteklassen- und Dienstprotokolle (Header)
libc/              musl-Port mit POSIX-Schicht
ports/             Patches und Build-Skripte für Mesa und Chromium
tools/             Image-Bau, Budget-Messung, QEMU-Skripte
docs/
```

## Konventionen

- Die Dokumente sind auf Deutsch, Bezeichner im Code auf Englisch.
- Größenangaben sind binär: KiB, MiB, GiB.
- Implementierungssprache ist C11, kein Rust (siehe [00](docs/00-vision.md#getroffene-entscheidungen)).
- Kernel-Typen beginnen mit `rc_`, Aufrufe mit `sys_` (System-Kit), `drv_` (Treiber-Kit) oder `usr_` (User-Kit).
