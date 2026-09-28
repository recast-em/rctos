# 08 – Plattformen

## Architekturschicht

Alles, was sich zwischen x86-64 und AArch64 unterscheidet, liegt in `kernel/arch/<arch>/`
hinter einer schmalen Schnittstelle. Der übrige Kernel sieht keine Architekturdetails.

| Funktion | x86-64 | AArch64 |
|---|---|---|
| Einstieg, frühe Initialisierung | Limine-Einstieg, GDT/IDT/TSS | Limine-Einstieg, `VBAR_EL1`, EL2→EL1 |
| Seitentabellen | 4 Ebenen, 4-KiB-Seiten, 2 MiB/1 GiB groß, PCID | 4 Ebenen, 4-KiB-Granule, 48 Bit, ASID, TTBR0/TTBR1 |
| TLB-Invalidierung zwischen CPUs | IPI + `invlpg`/`invpcid` | `tlbi … is` (Broadcast, kein IPI nötig) |
| Syscall-Eintritt | `syscall`/`sysret` (Prüfung auf kanonische Adresse!) | `svc`/`eret` |
| Kontextwechsel, FPU/SIMD | XSAVE/XRSTOR, Größe laut CPUID | `q0`–`q31`, FPSR/FPCR (später SVE) |
| Thread-lokaler Speicher | FS-Basis (`sys_thread_set_tls` bzw. `wrfsbase`) | `TPIDR_EL0` |
| Interrupt-Controller | x2APIC/xAPIC, IOAPIC | GICv2 / GICv3 |
| Timer | Local APIC im Einmal-Modus (M0), TSC-Deadline wo vorhanden; kalibriert gegen die TSC-Frequenz, die der Bootloader meldet | Generic Timer (`CNTV_*`) |
| Weitere CPUs starten | Limine-SMP (INIT-SIPI-SIPI) | Limine-SMP bzw. PSCI `CPU_ON` |
| IOMMU | Intel VT-d (DMAR), AMD-Vi | SMMUv2/v3 |
| Cache-Pflege für DMA | nicht nötig (kohärent) | bei nicht-kohärenten Geräten nötig (`drv_cache_op`) |
| Schutzfunktionen | NX, SMEP, SMAP, KPTI bei Bedarf | PXN, UXN, PAN (ab v8.1), BTI/PAC optional |
| Gerätebeschreibung | ACPI | Device Tree (Einplatinenrechner), ACPI (Server, Laptops) |
| Frühe Konsole | 16550 auf 0x3F8 | PL011 bzw. Mini-UART, Adresse aus dem DTB |

## Boot mit Limine

rctos nutzt das **Limine-Boot-Protokoll** auf beiden Architekturen. Limine ist ein
BSD-lizenzierter Bootloader für UEFI und, auf x86-64, auch für BIOS. Er liefert:

- Speicherkarte und Direct-Map des Speichers (HHDM)
- Framebuffer aus GOP bzw. VBE
- RSDP (ACPI) bzw. DTB
- Modul-Dateien, darunter das Boot-Image mit `init`, `devmgr`, Treibern und `main.cfg`
- SMP-Information und Start der weiteren CPUs
- TSC-Frequenz, Uhrzeit beim Boot (aus der RTC) und die Zeit im Bootloader

**Festgelegt:** Limine 11.4.1 mit Protokoll-Basisrevision 6. Tag und Prüfsummen aller
Dateien, die ins Boot-Image gehen, stehen in `boot/limine.sha256`; `tools/fetch-limine.sh`
holt und prüft sie. Das Boot-Image ist ein hybrides ISO für UEFI und BIOS, das auch auf
einen USB-Stick geschrieben werden kann.

Seit Basisrevision 3 bildet die Direct-Map nur noch RAM, Bootloader-Daten, Kernel, Module,
Framebuffer und (ab Revision 4) ACPI ab. MMIO wie den Local APIC trägt der Kernel selbst
ein (`arch_map_mmio`, ungecacht). Weitere CPUs übergibt Limine schlafend, und M0 legt sie
mit `hlt` schlafen, statt sie warten zu lassen.

Damit braucht rctos keinen eigenen Bootloader. Für Boards ohne UEFI, etwa den Raspberry Pi
ohne Community-UEFI, kommt später ein kleiner eigener Boot-Stub dazu. Er nimmt das DTB in
`x0` entgegen und baut dieselbe Übergabestruktur auf, die Limine liefert.

## x86-64

### Mindestanforderungen

- **x86-64-v2** (SSE4.2, POPCNT, CMPXCHG16B). AVX/AVX2 wird nicht vorausgesetzt, weil
  Celeron- und Pentium-Modelle der Skylake-Ära kein AVX haben.
- UEFI (alle Geräte mit Gen9-Grafik haben UEFI), 64-Bit-GOP
- 2 GiB RAM für `desktop`, 256 MiB für `embedded` ohne Browser

### ACPI in Phase 1: nur Tabellen, keine AML

| Tabelle | Verwendung |
|---|---|
| MADT | CPUs, LAPIC, IOAPIC, Interrupt-Übersteuerungen |
| MCFG | ECAM-Adresse für den PCI-Konfigurationsraum |
| HPET | Timer-Kalibrierung |
| FADT | Reset-Register, PM-Timer, Hinweise auf Legacy-Geräte (8042) |
| DMAR | VT-d-Einheiten und Geräte-Zuordnung |

Die Interrupt-Zuordnung für PCI-Geräte, die sonst `_PRT` aus der AML bräuchte, entfällt, weil
alle Phase-1-Geräte **MSI/MSI-X** nutzen.

**Ausschalten** braucht die Werte `SLP_TYPa/b` aus dem `\_S5`-Paket der DSDT. `devmgr` liest
sie mit einem Mini-Parser, der nur dieses eine Paket versteht. Das ist ein gängiger Trick.
**Neustart** läuft über das FADT-Reset-Register, notfalls über 0xCF9 oder den 8042.

Ein vollständiger AML-Interpreter kommt in M8 als eigener Dienst dazu, für Akku, Deckel,
Helligkeit, Tasten und `i2c-hid`. Kandidaten sind ACPICA (vollständig, aber groß) und lai
(klein).

### Referenzgeräte (Vorschlag)

Gesucht sind je ein häufiges Gerät pro Grafikgeneration, zum Beispiel:

| Generation | Beispielklasse |
|---|---|
| Gen9 (Skylake/Kaby Lake) | Business-Laptop 2016–2018, Mini-PC mit Core i5-6xxx/7xxx |
| Gen11 (Ice Lake) | Ultrabook 2019–2020 |
| Gen12 (Tiger Lake) | Business-Laptop 2021 |

Die konkrete Auswahl richtet sich danach, was die Zielgruppe tatsächlich übrig hat, und wird
in `docs/hardware.md` gepflegt.

### Die Referenzmaschine in QEMU

`tools/qemu.py` startet immer dieselbe Maschine, damit Messwerte vergleichbar bleiben:
`q35`, `-cpu max` (mit KVM `-cpu host`), 4 CPUs, 1 GiB RAM, UEFI über OVMF oder mit
`--bios` SeaBIOS. Mit `--rtc family` wacht sie wie jedes Gerät der Familie am 9. Oktober
2026 um 10:10 UTC auf; so bleiben Bildschirmfotos und Logs zwischen zwei Läufen
vergleichbar. Ohne KVM (in der CI) läuft QEMU in der Emulation; die Bootzeiten sind dann
Obergrenzen.

## AArch64

### Mindestanforderungen

- **ARMv8.0-A** mit 4-KiB-Granule und 48-Bit-VA. PAN, LSE-Atomics und ähnliches werden
  genutzt, wenn vorhanden, aber nicht vorausgesetzt. Grund: Der Raspberry Pi 4
  (Cortex-A72) ist v8.0.
- UEFI über Limine, alternativ der Boot-Stub

### Entwicklungsreihenfolge

1. **`qemu-system-aarch64 -M virt`:** GICv3, PL011, Generic Timer, PSCI, virtio. Alle
   virtio-Treiber aus x86-64 laufen unverändert, Venus liefert Vulkan.
2. **Referenzboard** (Entscheidung in M9):

| Kandidat | GPU / Mesa | Anzeige | Vorteile | Nachteile |
|---|---|---|---|---|
| Raspberry Pi 5 | V3D 7.x / `v3dv` (Vulkan 1.3) | HVS/PixelValve | sehr verbreitet, gut dokumentiert, keine GPU-Firmware für 3D | `v3dv` hat keine Kernel-Abstraktion, braucht also Anpassung; Boot ohne UEFI nur mit Stub |
| RK3588-Boards | Mali-G610 / `panvk` | VOP2 | leistungsstark, PCIe, `pan_kmod`-Abstraktion | CSF-Firmware nötig (darf weiterverteilt werden), Board-Vielfalt |
| Snapdragon-X-Laptops | Adreno / `turnip` | DPU | echte Laptops, `tu_knl`-Abstraktion | ACPI plus DT-Mischbetrieb, viel signierte Firmware, junge Linux-Unterstützung |

### Was die Portierung leicht macht

- Kits, Protokolle, Dienste und Treiber für virtio, USB, NVMe und Netz sind
  architekturunabhängig und werden nur neu kompiliert.
- Die Kernel-Schnittstelle ist auf beiden Architekturen gleich. Nur die Registerbelegung der
  Syscalls unterscheidet sich ([02](02-syscalls-und-kits.md#binärschnittstelle)).
- Grafikpuffer, Anzeige- und GPU-Protokoll sind bewusst nicht Intel-spezifisch
  ([05](05-grafik.md#ausblick-arm)).
- Chromium und Mesa unterstützen `arm64` ohnehin. Neu ist nur die Plattform `rctos`, und die
  ist architekturunabhängig.
