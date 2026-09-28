# 01 – Kernel

## Aufgaben

Der Kernel übernimmt nur das, was nur privilegierter Code tun kann:

| Im Kernel | Nicht im Kernel (Prozess) |
|---|---|
| physischer und virtueller Speicher, Adressräume | Dateisysteme |
| Threads, Scheduling, SMP | Netzwerkstack |
| Handles und Rechteprüfung | Gerätetreiber (außer Timer, Interrupt-Controller, IOMMU) |
| IPC: Kanäle, Ereignisse, Ports, Futex | PCI-Enumeration, ACPI-Auswertung, Device Tree |
| Weiterleitung von Interrupts an Treiber | Programmlader (ELF), außer für `init` |
| Zeit und Timer | Namensdienst, Richtlinien, Benutzerverwaltung |
| Zufallszahlen (CSPRNG) | Grafik, Eingabe, Audio |
| Auskunft über den eigenen Zustand (`sys_inspect`) | Aufbereitung als Tabellen unter `/now` |
| frühe Diagnoseausgabe, Boot-Bericht, Panik (serielle Schnittstelle und Bildschirm) | Protokollierung (Ringdateien unter `/log`) |

## Objektmodell

Alles, was ein Prozess im Kernel referenzieren kann, ist ein **Objekt**. Ein Prozess sieht
Objekte ausschließlich über **Handles**, und jedes Handle trägt eine Bitmaske von
**Rechten** ([03](03-rechte.md)).

| Typ | Zweck |
|---|---|
| `Process` | Prozess: Handle-Tabelle, Adressraum, Kontingent |
| `Thread` | Ausführungsfaden |
| `AddressRegion` | Adressbereich. Die Wurzel ist der Adressraum eines Prozesses; Unterbereiche sind Reservierungen |
| `Vmo` | Speicherobjekt: normal (seitenweise, verzögert belegt), physisch (MMIO) oder zusammenhängend (DMA) |
| `Channel` | Kanalende, immer paarweise |
| `Event` | Signalobjekt |
| `EventPair` | Signalobjekt-Paar; jede Seite signalisiert der anderen |
| `Port` | Warteschlange für Ereignispakete |
| `Timer` | Einmal-Timer, signalisiert bei Ablauf |
| `Resource` | Berechtigung für Hardware und Sonderrechte (MMIO-, IO-Port- oder IRQ-Bereich, JIT, IOMMU, Energie, SMC, Auskunft) |
| `Interrupt` | an einen Treiber gebundene Unterbrechung |
| `Msi` | Block von MSI/MSI-X-Vektoren |
| `Bti` | DMA-Identität eines Geräts |
| `Pmt` | gepinnter Speicher für DMA |
| `Log` | Kernel-Log (Lesen und Schreiben) |

Das sind 15 Typen. Neue Typen kommen nur dazu, wenn sich etwas mit den vorhandenen
nachweislich nicht ausdrücken lässt.

### Signale

Jedes Objekt hat 32 Signalbits. Die Bits 0–23 legt der Kernel pro Typ fest, zum Beispiel
`READABLE`, `WRITABLE`, `PEER_CLOSED`, `TERMINATED`, `SIGNALED`. Die Bits 24–31 sind für
Anwendungen frei (`USER0`–`USER7`) und werden mit `sys_object_signal` gesetzt. Gewartet
wird auf Signale, nicht auf Objekte. Das ist die einzige Wartesemantik des Systems.

### Lebensdauer

Objekte werden per Referenzzählung verwaltet. Ein Objekt stirbt, wenn das letzte Handle und
die letzte Kernel-Referenz verschwinden, bei einem VMO also auch die letzte Einblendung. Ein
Kanal meldet `PEER_CLOSED`, sobald die Gegenseite stirbt. Daran erkennt man auch abgestürzte
Treiber.

## Speicher

### Physischer Speicher

- **Allgemeine Seiten (4 KiB):** Freie Seiten bilden eine verkettete Liste *in sich selbst*.
  Der Zeiger auf die nächste Seite steht in der freien Seite, die über die Direct-Map
  erreichbar ist. Noch nie benutzte Seiten schneidet der Verwalter von oben von den freien
  Bereichen der Speicherkarte ab. Dadurch gibt es keine Metadaten pro Seite, der Aufwand ist
  unabhängig von der RAM-Größe, und beim Start wird der RAM nicht angefasst (umgesetzt in
  `pmm.c`). Später bekommt jede CPU einen kleinen Zwischenspeicher (etwa 32 Seiten), damit
  sie nicht ständig um die globale Liste konkurriert.
- **Zusammenhängender Pool:** Für DMA-Puffer ohne IOMMU und für Ringpuffer reserviert der
  Kernel beim Boot einen Pool. Die Größe steht in der Boot-Konfiguration; Vorgabe sind
  4 MiB im Profil `minimal` und 64 MiB im Profil `desktop`. Nur dieser Pool hat eine Bitmap.
- **Seitenbesitz:** Eine Seite gehört genau einem VMO. Deshalb braucht es keine
  Referenzzähler pro Seite. Geteilt werden VMOs, nicht Seiten. Copy-on-Write-Klone gibt es
  in Version 1 nicht.
- **Direct-Map:** RAM, Firmware-Daten und Framebuffer sind im Kernel-Adressraum
  eingeblendet, an derselben Stelle wie beim Bootloader, damit dessen Zeiger gültig bleiben.
  Aneinander grenzende Bereiche gleicher Art werden zusammengefasst und mit 1-GiB-,
  2-MiB- oder 4-KiB-Seiten abgebildet, je nachdem, was passt. RAM ist write-back, der
  Framebuffer write-combining, MMIO ungecacht (PAT wie bei Limine). Lücken wie der
  VGA-Bereich unter 1 MiB bleiben unabgebildet. Die Seitentabellen dafür kosten auf der
  UEFI-Referenzmaschine 48 KiB (eigener Budgetposten).
- **Kernel mit W^X:** Code `r-x`, Konstanten `r--`, Daten und bss `rw-`; ein Selbsttest beim
  Boot prüft die Rechte in den Tabellen.

### Virtueller Speicher

- **Adressraum** = Wurzel-`AddressRegion`. Die Kernel-Hälfte ist in allen Adressräumen
  gleich.
- **Reservieren ohne Belegen:** `sys_vm_reserve` legt einen Unterbereich an, ohne
  Seitentabellen oder Seiten zu belegen. Chromium braucht das dringend: V8 reserviert
  unter Umständen bis zu 1 TiB für seine Sandbox, PartitionAlloc reserviert
  Gigabyte-große Pools. Reservierungen kosten deshalb nur ein Kernel-Objekt.
- **Sofort verbuchen, später belegen (kein Overcommit):** Ein VMO wird beim Anlegen und bei
  `sys_vmo_set_size` in voller Größe dem Kontingent und dem Gesamtbestand angerechnet. Die
  Seiten holt und nullt der Kernel erst beim ersten Zugriff. Ein Zugriff kann deshalb nie
  an fehlendem Speicher scheitern: Fehlt Speicher, scheitert das Anlegen mit
  `RC_ERR_NO_MEMORY` oder `RC_ERR_QUOTA`, sichtbar und genau dort, wo es verursacht wurde.
  Einen OOM-Killer gibt es nicht. Reservierungen (`sys_vm_reserve`) werden nicht verbucht.
  Chromium kennt dieses Modell von Windows, das ebenfalls ohne Overcommit arbeitet.
- **W^X:** Eine Einblendung ist nie gleichzeitig beschreibbar und ausführbar. Ausführbar
  werden kann nur ein VMO mit dem Recht `EXECUTE`. Dieses Recht erzeugt
  `sys_vmo_make_executable` nur mit einer `Resource` der Art `EXEC` ([03](03-rechte.md)).
  JIT-Compiler wie V8 blenden dasselbe VMO zweimal ein, einmal RW und einmal RX.
- **Cache-Attribute:** Pro VMO gibt es `WB`, `WC`, `UC` und `DEVICE`. Auf x86-64 wird das
  über PAT umgesetzt, auf AArch64 über MAIR.
- **ASLR:** Ohne feste Adresse wählt `sys_vm_map` eine zufällige Adresse im Bereich.

### Kontingente

Jeder Prozess bekommt beim Anlegen ein **Kontingent**: maximale Speicherseiten, maximale
Handles und höchste Thread-Priorität. Alle Kernel-Objekte und Seiten, die ein Prozess
erzeugt, werden ihm angerechnet. Kontingente sind hierarchisch, denn ein Kind bekommt
einen Teil des Kontingents seines Elternprozesses. So kann kein Prozess den Kernel
aushungern, und die Budgets lassen sich durchsetzen.

Kontingente sind Obergrenzen, keine Reservierungen; ihre Summe darf den RAM übersteigen.
Entscheidend ist die Verbuchung beim Anlegen.

### Speicherdruck und Auslagern

- **Speicherdruck ist ein Ereignis.** Der Kernel führt zwei Schwellen für den unverbuchten
  Speicher (Vorgabe 10 % und 3 %). Wird eine Schwelle unterschritten, signalisiert er ein
  `Event` mit `PRESSURE_WARN` bzw. `PRESSURE_CRITICAL`. Das Event bekommt nur `init`; es
  reicht das Signal an Programme mit der Berechtigung weiter, vor allem an den Gast, der
  dann Tabs verwirft (Chromium hat dafür den `MemoryPressureListener`).
- **Auslagern ist ein bewusster Schritt**, nie ein Automatismus: Ein Programm, das ein
  `suspend` bekommt, gibt seine Registrierungen frei und wird vom Starter als Abbild in die
  Datei `swap` geschrieben. Beim `resume` kommt es zurück. Das ist das „sequenzielle
  Multitasking“ von RCP-OS. Der Kernel selbst lagert nie aus und kennt keine
  Seitenfehler-getriebene Auslagerung.

## Threads und Scheduler

- **Präemptiv**, mit **32 festen Prioritäten** und Round-Robin innerhalb einer Stufe:

  | Stufen | Verwendung |
  |---|---|
  | 0–7 | Hintergrund |
  | 8–15 | normal (Vorgabe: 12) |
  | 16–23 | interaktiv (Anzeige, Eingabe, Audio-Mischer) |
  | 24–31 | Treiber, Echtzeit |

  Die höchste erlaubte Stufe steht im Kontingent.
- **Pro CPU eine Run-Queue** mit einer 32-Bit-Belegungsmaske. Die Auswahl des nächsten
  Threads kostet O(1).
- **Zeitscheibe** 4 ms (konfigurierbar). **Tickless:** Der Timer wird nur für den nächsten
  tatsächlichen Termin programmiert: auf x86-64 im TSC-Deadline-Modus, wo vorhanden, sonst
  mit dem Local APIC im Einmal-Modus (so arbeitet M0); auf AArch64 mit dem Generic Timer.
- **Der Kernel hat keine eigenen Threads.** Er arbeitet nur im Auftrag eines Syscalls oder
  eines Interrupts. Pro CPU gibt es einen Leerlauf, der die CPU anhält (`hlt` bzw. `wfi`).
  Ein untätiges System weckt keine CPU; M0 wacht einmal pro Minute auf, und nur, weil sich
  die angezeigte Uhr ändert.
- **SMP:** Ein Thread läuft bevorzugt auf der CPU, auf der er zuletzt lief. Beim Aufwecken
  darf er auf eine untätige CPU wandern. Es gibt keinen periodischen Lastausgleich.
- **Kernel-Präemption:** Der Kernel ist nicht präemptiv, aber jeder Pfad ist kurz. Lange
  Operationen wie das Nullen großer VMOs oder das Abbauen großer Einblendungen haben
  Präemptionspunkte. Ziel für die Latenz im schlechtesten Fall: unter 50 µs auf
  Referenz-Hardware, gemessen in M1.
- **Prioritätsvererbung** über Futex und Kanal-Calls ist in Version 1 nicht vorgesehen.
  Sie kommt dazu, wenn Messungen eine Prioritätsinversion zeigen.
- **Pro Thread** kostet das 8 KiB Kernel-Stack plus FPU/SIMD-Zustand: auf x86-64 so groß,
  wie XSAVE meldet (512 B bis etwa 2,7 KiB), auf AArch64 528 B.

## IPC

### Kanäle

- Immer paarweise. Eine Nachricht besteht aus bis zu **64 KiB Bytes** und bis zu
  **64 Handles**. Größere Daten gehen per VMO.
- Mit `sys_channel_write` wandern Handles aus dem Sender zum Empfänger. Sie werden
  übertragen, nicht kopiert, und der Sender braucht dafür das Recht `TRANSFER`.
- **Asynchron** mit einer Warteschlange pro Seite, deren Länge über das Kontingent
  begrenzt ist.
- **Synchroner Aufruf** `sys_channel_call`: Die ersten 4 Bytes einer Nachricht sind eine
  Transaktions-ID. Der Kernel ordnet die Antwort zu und weckt den Aufrufer direkt, ohne
  den Umweg über einen Port. Das ist der schnelle Pfad für Dienstaufrufe.
- Den Inhalt einer Nachricht interpretiert der Kernel nicht, abgesehen von der
  Transaktions-ID.

### Ports

Ein Port sammelt Ereignispakete (32 Byte). Ein Objekt wird mit einem Schlüssel und einer
Signalmaske an den Port gebunden. Sobald das Signal kommt, landet ein Paket im Port, und
Threads warten mit `sys_port_wait` auf viele Quellen gleichzeitig. Das ist die Grundlage
für Ereignisschleifen, auch für die `MessagePump` von Chromium. Interrupts und Timer lassen
sich ebenfalls binden.

### Futex

Die Aufrufe `sys_futex_wait` und `sys_futex_wake` arbeiten auf einem 32-Bit-Wort im
Adressraum. Mutexe, Condition Variables und `pthread_*` baut die libc darauf auf, ohne
eigenes Kernel-Objekt.

## Auskunft

Der Kernel gibt über alles Auskunft, was er verwaltet, aber er bereitet nichts auf.

- `sys_inspect(inspect, table, index, rec, len)` liefert **einen Datensatz fester Größe je
  Index**: Prozesse, Threads eines Prozesses, Speicherbestände, CPUs, Budgets, Ringe. Wer
  eine Tabelle will, zählt die Indizes hoch, bis `RC_ERR_OUT_OF_RANGE` kommt; der Kernel
  kopiert nie Listen (wie `SYS_TASK_INFO` in RCP-OS).
- Die Layouts der Datensätze stehen in einem Header mit `_Static_assert`, den Kernel,
  `init` und Werkzeuge gemeinsam benutzen.
- Aufrufen darf das nur, wer die Ressource `INSPECT` hat; `init` macht daraus die Tabellen
  unter `/now` ([09](09-mikro-welt.md#now--der-zustand-als-tabellen)).

## Fehler als Exponate

- **Ausnahme in einem Prozess:** Der Prozess geht in den Zustand `FAULTED`. Der Kernel hält
  den Ausnahmedatensatz fest (Vektor, Fehlercode, Adresse, alle Register) und signalisiert
  `TERMINATED`. Der Datensatz bleibt über `sys_object_info(RC_INFO_FAULT)` und
  `sys_inspect` lesbar, bis das letzte Handle auf den Prozess geschlossen ist. Ein späterer
  Debugger setzt genau hier an.
- **Kernel-Panik:** Ursache und alle Register erscheinen auf dem Bildschirm und auf der
  seriellen Schnittstelle, dann hält der Kernel an (seit M0 umgesetzt und getestet).

## Interrupts

- Der Kernel besitzt nur den Interrupt-Controller (LAPIC/IOAPIC bzw. GIC), den Timer und
  die IOMMU.
- Jeder andere Interrupt gehört einem `Interrupt`-Objekt eines Treibers. Der Kernel
  quittiert beim Controller, maskiert die Leitung bei pegelgesteuerten Interrupts und
  signalisiert das Objekt. Der Treiber weckt auf, bearbeitet das Gerät und ruft dann
  `drv_interrupt_ack` auf.
- **MSI/MSI-X wird bevorzugt.** Es ist flankengesteuert und muss nicht maskiert werden.
  Die Intel-iGPU, xHCI, NVMe, AHCI und die meisten Netzwerkkarten unterstützen es. Damit
  braucht Phase 1 keine ACPI-AML-Auswertung für die IRQ-Zuordnung ([08](08-plattformen.md)).

## Zeit

- `RC_CLOCK_MONOTONIC` zählt Nanosekunden seit dem Boot und springt nie.
  `RC_CLOCK_UTC` = monotone Zeit plus Offset. Den Offset setzt ein Zeitdienst mit dem
  Recht dazu.
- **Zeitseite:** Ein schreibgeschütztes VMO enthält die Umrechnungsparameter vom TSC bzw.
  Generic Timer in Nanosekunden. `libsys` blendet es ein und liest die Uhr ohne Syscall.
  Die Zeitseite enthält nur Daten und keinen Code, anders als ein vDSO.
- Alle Wartezeiten sind **absolute** Termine in monotonen Nanosekunden.
  `RC_TIME_INFINITE` bedeutet „nie“, `0` bedeutet „nur prüfen“.
- **Frequenzen:** Die TSC-Frequenz meldet der Bootloader. Den Local-APIC-Zeitgeber
  kalibriert der Kernel in 5 ms gegen den TSC. Einen PIT braucht er nicht.

## Prozessstart

Der Kernel kennt kein `fork` und kein `exec`, und ELF kennt er nur für `init`.

1. Der Elternprozess ruft `sys_process_create` auf und erhält Prozess- und
   Adressraum-Handle.
2. Die Lade-Bibliothek des User-Kits (`libusr`) blendet die ELF-Segmente per `sys_vm_map`
   in den neuen Adressraum ein. Den Code bezieht sie als VMO mit `EXECUTE` vom
   Dateidienst.
3. `sys_thread_create` legt einen Thread an, `sys_process_start` startet ihn mit
   **genau einem Handle**, dem Bootstrap-Kanal.
4. Über den Bootstrap-Kanal kommt die erste Nachricht: Argumente, Umgebung und alle
   Start-Handles, darunter Namensraum, Zeitseite, Log und je nach Manifest
   Ressourcen oder Dienstkanäle.

Den Starter in `init` nutzen normale Programme über `usr_process_spawn`. Chromium startet
seine Kindprozesse mit `libusr` selbst, innerhalb seines eigenen Kontingents.

**Code wird nie kopiert.** Aus dem Boot-Image, das ohnehin im RAM liegt, blendet der Lader
die Code-Seiten direkt ein. Auf Datenträgern teilen sich alle Instanzen eines Programms die
Code-Seiten im Dateicache. Nur beschreibbare Daten bekommt jede Instanz neu; eine zweite
Instanz kostet also nur ihre Daten. Das ist das Gegenstück zu XIP in RCP-OS.

## Boot

```
Firmware (UEFI) → Limine → Kernel → init → devmgr → Treiber → Dienste → Profil
```

1. **Limine** ([08](08-plattformen.md)) lädt den Kernel und das Boot-Image als Modul.
   Es übergibt Speicherkarte, Framebuffer, RSDP bzw. DTB und die SMP-Information.
2. **Der Kernel** richtet ein: Architekturschicht, physischen Speicher, Direct-Map,
   Interrupt-Controller, Timer und die weiteren CPUs.
3. **Der Kernel lädt `init`** aus dem Boot-Image. Dafür hat er einen minimalen ELF-Lader,
   der nur statische ELF-Dateien mit `PT_LOAD` versteht. `init` bekommt als einziger
   Prozess die Wurzel-Ressource, dazu VMOs für Boot-Image, ACPI/DTB und den
   Firmware-Framebuffer.
4. **`init`** liest `/sys/main.cfg` ([09](09-mikro-welt.md#maincfg)), teilt die
   Wurzel-Ressource in Teilressourcen auf und übergibt sie an `devmgr`. Selbst behält es
   nur `INSPECT` für `/now`. Dann startet es die Dienste des Profils und zuletzt die
   Bodenkonsole.

**Stand M1, Schritt 1:** Schritt 1 und Teile von Schritt 2 sind umgesetzt: eigene GDT und
Ausnahmetabelle, Boot-Konsole, Local-APIC-Zeitgeber, weitere CPUs geparkt, physischer
Seitenverwalter und eigene Seitentabellen mit W^X. Die Speicherbereiche des Bootloaders
sind noch nicht zurückgeholt: Die geparkten CPUs stehen noch auf seinen Tabellen.

## Größen-Disziplin

- Keine Allgemein-Heap-Allokation im Kernel. Jeder Objekttyp hat einen Pool fester
  Objektgröße (Ziel: ≤ 128 Byte pro Objekt).
- Keine Zeichenkettenverarbeitung außer Prozess- und Thread-Namen (max. 32 Byte) und der
  Log-Ausgabe.
- Kein Dateisystem, keine ELF-Relokation, kein ACPI-Interpreter und kein Device-Tree-Parser
  über das Auslesen von Speicher- und Interrupt-Controller-Knoten hinaus.
- Jeder neue Syscall braucht eine Begründung, warum er nicht als Dienst-Protokoll umgesetzt
  werden kann.
- Das Kernel-Log ist ein Ring fester Größe (8 KiB); es wächst nie.
- **Codebudget:** höchstens 10.000 Zeilen C und Assembler für Kern und eine Architektur,
  gezählt von `tools/loc.sh` ohne Leer- und Kommentarzeilen. Stand M1, Schritt 1: 1.713.
- **Messung:** `make` prüft das Image mit `tools/budget.py` gegen `kernel/core/budget.h`;
  der Kernel prüft beim Boot dieselben Zahlen samt belegter Seiten und schreibt die
  GATE-Zeile. `make test` bootet in QEMU und verlangt `gate ok`
  ([00](00-vision.md#budgets)).
