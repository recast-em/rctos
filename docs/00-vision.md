# 00 – Vision und Budgets

## Ziel

rctos soll zeigen, dass ein modernes, GPU-beschleunigtes Web-Betriebssystem auf einem
winzigen Kern stehen kann. Klein und schnell soll dabei vor allem der Unterbau sein:
Kernel, Basisdienste und Treiber. Die großen Brocken, also Chromium, GPU-Treiber-Userspace
und Netzstack, laufen als gewöhnliche Prozesse darüber, bringen ihren Speicherbedarf mit
und belasten den Kern nicht.

Die Oberfläche ist eine Chromium-Instanz, die den Bildschirm direkt ansteuert. Einen
Fenster-Server, X11, Wayland oder Android gibt es nicht. Chromium ist sein eigener
Compositor.

## Nicht-Ziele

- **Volle POSIX-Kompatibilität.** Es gibt kein `fork`, keine Signale im Kernel, keine
  Unix-Benutzerrechte und keine Dateideskriptoren im Kernel. Eine POSIX-Teilmenge gibt es
  nur als Bibliothek (siehe [06](06-chromium.md)).
- **Binärkompatibilität** zu Linux, Windows oder Android.
- **Mikrocontroller ohne MMU.** „Embedded“ meint hier Geräte der Klasse ARMv8-A oder x86-64
  mit MMU, also etwa Einplatinenrechner, Kiosk- und Industrie-PCs oder Thin Clients.
- **32-Bit-Architekturen.**
- **Eine eigene Browser-Engine.** Chromium wird portiert, nicht ersetzt.
- **Ein Objekt-Framework im Kernel.** Also keine Vererbung, keine Reflexion und kein
  generischer Property-Baum.

## Prinzipien

1. **Der Kernel macht nur, was nur der Kernel kann:** Adressräume, Threads, Scheduling,
   IPC, Handles, Interrupt-Weiterleitung und Zeit. Alles andere läuft als Prozess.
2. **Keine Rechte ohne Handle.** Es gibt keine globale Autorität (keine „ambient
   authority“): Was ein Prozess darf, steht vollständig in seiner Handle-Tabelle.
3. **Flach statt geschichtet.** Ein Aufruf ist eine C-Funktion, die genau eine Sache tut.
   Es gibt keine Callback-Kaskaden und keine Zwischenschichten „für später“.
4. **Statisch statt dynamisch.** Jedes Programm ist eine statisch gelinkte ELF-Datei. Die
   einzige Ausnahme, Treibermodule, ist eng geregelt ([04](04-treibermodell.md)).
5. **Austauschbarkeit über Protokolle, nicht über Bibliotheken.** Wer mit einem Treiber
   spricht, spricht mit einer Geräteklasse und nicht mit einer Implementierung.
6. **Direktzugriff ist erlaubt, wenn er ausdrücklich vergeben wurde.** Ein flach gemappter
   Framebuffer oder IO-Ports sind in Ordnung, solange ein Handle dazu vergeben und
   widerrufbar ist.
7. **Messen statt schätzen.** Die Budgets unten prüft die CI bei jedem Commit.
8. **Übernehmen statt neu schreiben**, wo Eigenbau weder Größe noch Architektur verbessert:
   musl, Mesa, Chromium, ein Netzstack.

## Budgets

### Größe auf dem Datenträger

| Komponente | Harte Grenze | Ziel |
|---|---|---|
| Kernel-Image (`.text` + `.rodata` + `.data`) | 1 MiB | ≤ 256 KiB |
| Basissystem (Kernel, init, devmgr, Basistreiber, Shell) | 10 MiB | ≤ 4 MiB |
| Mesa-ICD (je Treiber) | – | wird gemessen, kein Budget |
| Chromium | – | wird gemessen, kein Budget |

### RAM, resident

Gemessen wird im Profil `minimal` (siehe unten) nach dem Boot im Leerlauf, und zwar alle
physischen Seiten, die Kernel und Prozesse belegen. Nicht mitgezählt werden von der
Firmware reservierter Speicher, ACPI-Tabellen und das Framebuffer-Memory der Firmware.

| Posten | Ziel |
|---|---|
| Kernel-Code und -Konstanten | 160 KiB |
| Kernel-Daten, Objekt-Pools, Handle-Tabellen | 48 KiB |
| Pro CPU (Kernel-Stacks, TSS/Vektortabelle, Run-Queue) | 16 KiB |
| Direct-Map-Seitentabellen (1-GiB-Seiten) | 8 KiB |
| `init` (mit Namensdienst und Starter) | 80 KiB |
| `devmgr` | 72 KiB |
| Treiber `uart16550` | 48 KiB |
| `shell` | 64 KiB |
| **Summe (1 CPU)** | **≤ 496 KiB**, harte Grenze 512 KiB |

Jede weitere CPU kostet etwa 16 KiB. Die größten Hebel sind **Seitentabellen** (etwa
16–24 KiB je Prozess), **Stacks** (die Seiten werden erst bei Zugriff belegt) und die
**Zahl der Prozesse**. Deshalb beherbergt `init` im Minimalprofil den Namensdienst und
den Starter.

Messung: `sys_system_info(RC_INFO_MEMORY, …)` liefert die belegten Seiten nach Kategorie.
`tools/budget` bootet das Image in QEMU, liest die Werte über die serielle Konsole aus und
lässt den Build fehlschlagen, wenn eine Grenze überschritten ist.

### Was nicht ins Basisbudget fällt

Grafikpuffer, GUI-Daten, der Netzstack, Dateisystem-Caches, Mesa und Chromium haben eigene,
realistische Größen und werden getrennt gemessen. Zur Orientierung: Chromium mit einem Tab
braucht einige hundert MiB. Ein Desktop-Gerät sollte mindestens 2 GiB haben, empfohlen
sind 4 GiB. Der Unterbau soll davon nahezu nichts abzweigen.

## Profile

Es gibt einen Kernel und ein Userland. Die Profile unterscheiden sich nur darin, was
gestartet wird. Ein Profil ist eine Textdatei im Boot-Image, die `init` liest.

| Profil | Inhalt | Zweck |
|---|---|---|
| `minimal` | Kernel, init, devmgr, uart16550, shell | Budget-Nachweis, Kernel-Tests |
| `embedded` | + Dateisystem, efifb bzw. Display-Treiber, Eingabe, optional Netz | Kiosk, Steuerung, Anzeige ohne Browser |
| `desktop` | + GPU-Treiber, Netz, Audio, Chromium | das vollständige System |

## Getroffene Entscheidungen

| Thema | Entscheidung | Begründung |
|---|---|---|
| Sprache für Kernel, Kits, Dienste und Treiber | **C11** (freestanding im Kernel), übersetzt mit Clang; Assembler nur in der Architekturschicht. **Kein Rust im eigenen Code.** | Vorgabe des Projekts; C passt nahtlos zu musl, Mesa und den C-ABIs der Kits |

## Offene Entscheidungen

| Thema | Empfehlung | Alternative | Fällig bis |
|---|---|---|---|
| Beschreibung der Protokolle | handgeschriebene C-Header mit festen Struct-Layouts; ein Generator erst, wenn es mehr als etwa 10 Protokolle gibt | eigene kleine IDL | M2 |
| Netzstack | lwIP (C, klein, ausgereift) | eigener minimaler IPv4/IPv6-Stack in C | M3 |
| System-Dateisystem | ext2 (einfach, verbreitet), dazu FAT32 für die EFI-Partition | littlefs (Embedded, robust bei Stromausfall) | M3 |
| ARM-Referenzboard | Raspberry Pi 5 (V3D, Mesa `v3dv`) | RK3588-Board (Mali-G610, Mesa `panvk`) | M9 |

## Glossar

| Begriff | Bedeutung |
|---|---|
| Handle | prozesslokale Nummer, die auf ein Kernel-Objekt zeigt und Rechte trägt |
| VMO | Speicherobjekt (Virtual Memory Object): eine Menge von Seiten, die man einblenden kann |
| Kanal | bidirektionale Nachrichtenverbindung, die Bytes und Handles überträgt |
| Port | Warteschlange, auf der ein Thread auf viele Ereignisse gleichzeitig wartet |
| Ressource | Handle, das Hardware-Zugriff erlaubt (MMIO, IO-Ports, IRQs, …) |
| BTI | Bus-Transaction-Initiator: DMA-Identität eines Geräts gegenüber der IOMMU |
| Kit | Gruppe von API-Aufrufen: System-Kit, Treiber-Kit, User-Kit |
| Treiberpaket | Treiberprozess, Manifest und optional ein Treibermodul (z. B. ein Vulkan-ICD) |
| Lease | zeitweise exklusive Nutzung eines Geräts, etwa eines Bildschirms; widerrufbar |
