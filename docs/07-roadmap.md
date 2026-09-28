# 07 – Roadmap

Jeder Meilenstein hat ein prüfbares **Abnahmekriterium**. Die Reihenfolge ist so gewählt,
dass nie zwei große Unbekannte gleichzeitig offen sind: die Mikro-Welt zuerst ganz ohne
Gast, Chromium zuerst ohne GPU, die GPU zuerst ohne Chromium, alles zuerst in QEMU.

Aufwand: S = Tage, M = Wochen, L = Monate, XL = viele Monate.

## M0 – Werkzeugkette, Boot und ein erstes Bild (S–M) ✔

Erledigt am 2026-09-28.

- C11-Grundgerüst mit Clang und `ld.lld`: freestanding, `-Os`, ohne SSE, eigenes
  Linker-Skript mit getrennten Rechten je Segment ([00](00-vision.md#entscheidungen))
- Limine 11.4.1 (Basisrevision 6), Version und Prüfsummen fest in `boot/limine.sha256`;
  Boot-Image als hybrides ISO für UEFI und BIOS
- Kernel: serielle Ausgabe, eigene GDT mit TSS, Ausnahmetabelle mit Panik-Bildschirm,
  Zellenkonsole (CP437, 8 × 8, Schrift der Familie) direkt im Framebuffer, Boot-Bericht, Local-APIC-Zeitgeber im
  Einmal-Modus, weitere CPUs schlafen, Leerlauf mit `hlt`
- Budgets: `kernel/core/budget.h` als eine Quelle; `tools/budget.py` prüft das Image beim
  Bauen, der Kernel prüft beim Boot und schreibt die GATE-Zeile
- `tools/qemu.py`: Start mit Fenster, oder ohne Fenster mit Prüfung des seriellen Logs und
  Bildschirmfoto über QMP; `make test` bootet UEFI und BIOS
- CI (`.github/workflows/build.yml`): bauen, Budget, Boot in QEMU, ISO und Bildschirmfotos
  als Ergebnis

**Abnahme (erfüllt):** In QEMU erscheint der Boot-Bericht auf dem Bildschirm und auf der
seriellen Schnittstelle, die GATE-Zeile lautet `gate ok`, und die CPU wacht im Leerlauf nur
auf, wenn sich die Uhr ändert. Gemessen: Image 28 KiB, Code und Konstanten 20 KiB, Daten
32 KiB, 1.472 Zeilen, 14–20 ms vom Kernel-Eintritt bis zum Leerlauf, 99,96 % Leerlauf in
der ersten Minute.

## M1 – Kernel-Kern (L)

- Physischer Speicher, eigene Direct-Map und Seitentabellen, Adressräume, VMO,
  Reservierungen, Verbuchung ohne Overcommit, Speicherdruck-Ereignis
- Threads, Scheduler (präemptiv, 32 Stufen, tickless), SMP
- Handles, Rechte, Kontingente
- Kanäle, Events, Ports, Futex, Timer
- Syscall-Eintritt, System-Kit vollständig, `sys_inspect`
- Prozesse mit Zustand `FAULTED` und Ausnahmedatensatz
- Kernel-Log als Ring (8 KiB)
- Kernel-Testprogramm im Userspace (`ktest`)

**Abnahme:** `ktest` läuft auf 1 und 4 CPUs ohne Fehler; ein absichtlicher Seitenfehler in
`ktest` erscheint als lesbarer Ausnahmedatensatz; die Latenzmessung liegt vor; der Kernel
ist ≤ 256 KiB groß und hat ≤ 10.000 Zeilen.

## M2 – Die Mikro-Welt steht (M)

- `init`: Namensdienst, Starter, `main.cfg` mit Ganz-Datei-Regel und Referenzprofil,
  `/now`-Server (`tasks`, `mem`, `budget`, `boot`, `log`)
- `devmgr`: ACPI-Tabellen (ohne AML), PCI über ECAM, Manifeste, `/now/devs`, `/now/res`
- Treiber-Kit, Treiber `uart16550`, `efifb`, `ps2`, `pci`
- `term`: Terminal und Bodenkonsole, auf der seriellen Schnittstelle und als Fenster, mit
  dem Rettungs-Prompt ([09](09-mikro-welt.md#die-prompt-sprache))
- `win` und `shell` ([11](11-fenster.md)): Zellenmodell 1:1 aus RCP-OS, Fenster auf dem
  Raster, Besitzkarte, Schatten als Schattierung, Zellen-Compositor auf der CPU über `efifb`,
  Leiste und Status-Kachel, Mauszeiger, Eingabe über `ps2`
- die ersten Werkzeuge (`help`, `ls`, `cat`, `tasks`, `mem`, `kill`, …), jedes ≤ 16 KiB
- die Zellenkonsole als gemeinsames C-Modul von Kernel und `win`

**Abnahme:** Das Profil `minimal` bleibt in QEMU unter **512 KiB RAM**, und die CI prüft
das über `/now/budget`. `cat /now/tasks` zeigt alle Prozesse. Eine absichtlich fehlerhafte
`main.cfg` startet das Referenzprofil und nennt Zeile und Grund. Zwei Terminal-Fenster
lassen sich verschieben; `/now` zeigt, dass dabei nur beschädigte Zellen neu gezeichnet
werden, und ohne Eingabe wacht keine CPU auf. Schließt das letzte Fenster, steht die
Bodenkonsole wieder da. Vom Bootloader bis zum Prompt vergeht höchstens 1 s.

## M3 – Speicher und Netz (M–L)

- `rcfs` ([10](10-rcfs.md)): ein C-Modul für `fs`, `mkrcfs` und Tests; Boot-Volume als
  rcfs-Abbild im RAM mit Ausführen am Ort; `/tmp`; Ringdateien unter `/log`
- rcp-fs-Volumes von RCP-OS lesen und schreiben
- `virtio-blk`, `virtio-net`, Netzdienst (lwIP), DHCP, DNS; `/now/vol`, `/now/net`
- `login`: Konten, Sitzungen, Vollbild-Oberfläche, `/usr/<uid>`

**Abnahme:** Ein rcp-fs-Abbild aus dem Web-Emulator von RCP-OS lässt sich unter rctos
lesen; Mount-Zeit und RAM-Bedarf von rcfs sind für 100.000 Einträge gemessen; ein Werkzeug
der Mikro-Welt lädt eine Datei über HTTP.

## M4 – Der Gast in Software (XL)

- musl-Port und POSIX-Schicht als Teil des Gast-Pakets
- Toolchain, GN-Plattform `rctos`, Umgang mit Chromiums eigenen Rust-Komponenten
  ([06](06-chromium.md#build)); `ports/chromium/args.gn` für einen leisen Gast
- base, PartitionAlloc, V8-Plattform, Mojo im Einzelprozessmodus
- Ozone `rctos` Stufe G0
- Entscheidung: Content-Einbettung `surf` oder vollständiger Chrome-Browser
  ([06](06-chromium.md#schlank-einbetten))

**Abnahme:** `content_shell --single-process` zeigt in QEMU eine Webseite mit JavaScript
an, und zwar in einem Fenster der Fensterschicht. Beim Beenden zeigt `/now/mem` denselben
Stand wie vor dem Start.

## M5 – Der Gast mit mehreren Prozessen, CAST (L)

- Prozessstart, Mojo über Prozessgrenzen, Kontingente pro Renderer
- Handle-basierte Sandbox, Speicherdruck-Ereignis an den Gast
- Eingabe (Tastatur, Maus), Schriften
- Entscheidung und erster Schritt zur Prompt-Sprache CAST
  ([09](09-mikro-welt.md#die-prompt-sprache))

**Abnahme:** Mehrere Tabs laufen in getrennten Renderern; ein abgestürzter Renderer reißt
den Einbetter nicht mit; bei Speicherdruck verwirft der Gast Tabs, statt dass irgendetwas
abgeschossen wird.

## M6 – Echte x86-64-Hardware (L)

- `xhci`, `usb-hid`, `usb-storage`, `ahci`, `nvme`, `e1000e`, `r8169`
- KPTI, Ausschalten über `_S5` ([08](08-plattformen.md))
- `intel-igpu` mit Anzeigestufe **D1** (Page-Flip, VBlank)
- Liste getesteter Geräte (`docs/hardware.md`)

**Abnahme:** Auf mindestens drei Referenzgeräten (Gen9, Gen11, Gen12) bootet das System bis
zum Gast in Stufe G0 mit ruckelfreiem Page-Flip; die Mikro-Welt bleibt dabei im Budget.

## M7 – Intel-GPU-Beschleunigung (XL)

- `intel-igpu`: GGTT, PPGTT, Execlists, Kontexte, Fences, Reset, Workarounds
- Mesa `anv` mit rctos-Backend, WSI, externe Speicher-Erweiterung
- Vorstufe in QEMU: `virtio-gpu` mit `venus`
- Gast in Stufe G1

**Abnahme:** `vkcube` läuft auf allen Referenzgeräten; der Gast rendert mit Vulkan;
WebGL-Beispiele laufen über ANGLE.

## M8 – Zero-Copy, Modesetting und Energie (L)

- Gast in Stufe G2 (NativePixmap, Overlays)
- Anzeigestufe D2 (externe Monitore) und D3 (Planes)
- Energie: CPU-Leerlaufzustände, RC6, DMC-Firmware, AML-Interpreter für Akku und Deckel

**Abnahme:** Ein Video spielt über eine Overlay-Plane; ein externer Monitor funktioniert;
die Akkulaufzeit ist dokumentiert.

## M9 – AArch64 (L–XL)

- Architekturschicht AArch64 ([08](08-plattformen.md)), zuerst `qemu-system-aarch64 -M virt`
  mit virtio und Venus
- Wahl des Referenzboards, Treiber für Display, GPU (Mesa `v3dv` bzw. `panvk`), Speicher und Netz

**Abnahme:** Dieselben Pakete, nur neu kompiliert, erreichen auf dem Referenzboard den
Stand von M7, mit denselben Budgets.

## M10 – Produktreife (dauerhaft)

- Updates als Austausch ganzer Pakete (A/B), signierte Pakete, Wiederherstellung über das
  Referenzprofil
- Audio, WLAN, Bluetooth
- Barrierefreiheit, Lokalisierung
- Laufende Pflege des Gasts im Extended-Stable-Takt
