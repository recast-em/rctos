# 00 – Leitbild, Vision und Budgets

## Kernsatz

**Viel erreichen mit wenig Ressourcen.** rctos überträgt das Paradigma seines kleinen
Geschwisters RCP-OS (Recaster Pico) auf PC- und ARM-Hardware: ein System, das man ganz
überblicken, messen und lesen kann. Es rückt in den Hintergrund und lässt Platz für das,
worum es geht, nämlich Code, Inhalte und Verbindungen der Nutzer.

Die heutige Umgebung aus Schichten, Diensten „für alle Fälle“ und unsichtbaren
Automatismen weicht einer **überwachbaren, inspizierbaren, kleinen Mikro-Umgebung**.

Das Credo in drei Sätzen:

1. **Das System nimmt sich fast nichts**, damit fast die ganze Kraft des Geräts dem
   User-Space gehört: Nutzern und Entwicklern.
2. **Gute Bedienung ist eine Frage der Führung, nicht der Ressourcen.** Sie kostet
   Nachdenken, kaum RAM und kaum Speicherplatz.
3. **Ein Raster für alles:** Zellen von 8 × 8 Pixeln. Fenster rasten darauf ein, jedes
   Fenster ist zugleich ein Zellen-Terminal, und Textoberflächen stehen gleichberechtigt
   neben Grafik. Moderne Web-Darstellung gibt es in Fenstern oder im Vollbild, wo sie
   gebraucht wird, und nur dort kostet sie Energie.

## Zwei Welten

| | Mikro-Welt | Gast |
|---|---|---|
| Was | Kernel, Dienste, Treiber, Fensterschicht und Shell, Werkzeuge, Bodenkonsole, Systemoberflächen | Chromium mit seinen Begleitern: Mesa-Treibermodul, musl-libc, ICU, Schriften |
| Maßstab | jedes Byte hat einen Namen und ein Budget | eigenes Kontingent aus `main.cfg`, gemessen und begrenzt |
| Code | eigener, lesbarer C-Code; ein Mensch kann ihn ganz lesen | übernommen; Änderungen nur als Patch-Stapel |
| Schnittstellen | Kits und Protokolle, kein POSIX | POSIX-Teilmenge als Bibliothek, **nur** im Gast |
| Zeichensatz | ein Byte, ein Zeichen (CP437) | Unicode |
| Ohne den anderen | voll funktionsfähig: Konsole, Dateien, Netz, Anmeldung | läuft nie ohne die Mikro-Welt |

Die Oberfläche gehört der Mikro-Welt: eine Fensterschicht auf dem 8 × 8-Raster mit dem
Zellenmodell von RCP-OS ([11](11-fenster.md)). Chromium ist **die eine Ausnahme** vom
Paradigma und bleibt ein Gast: eine Render-Instanz für moderne Web-Darstellung in Fenstern
oder im Vollbild, in einem festen Kontingent, jederzeit beendbar, ohne dass die Mikro-Welt
es merkt. Alles um den Gast herum tritt deutlich kleiner auf und bleibt im
Hintergrund.

## Leitsätze

Jeder Leitsatz stammt aus der Praxis von RCP-OS und ist hier auf rctos übertragen.

### Klein bleiben

1. **Der Kernel macht nur, was nur der Kernel kann:** Adressräume, Threads, Scheduling,
   IPC, Handles, Interrupt-Weiterleitung, Zeit. Alles andere ist ein Prozess.
2. **Flach statt geschichtet.** Ein Aufruf ist eine C-Funktion, die eine Sache tut. Keine
   Zwischenschichten „für später“, keine Callback-Kaskaden, keine Frameworks.
3. **Statisch statt dynamisch.** Jedes Programm ist eine statisch gelinkte ELF-Datei. Die
   einzige Ausnahme, Treibermodule im Gast, ist eng geregelt ([04](04-treibermodell.md)).
4. **Die Mikro-Welt braucht keine libc.** Ihre Programme benutzen nur die Kits
   ([02](02-syscalls-und-kits.md)). POSIX gibt es als Bibliothek, und zwar nur für den Gast
   ([06](06-chromium.md)).
5. **Übernehmen statt neu schreiben** nur dort, wo Eigenbau weder Größe noch Klarheit
   bringt: Chromium, Mesa, musl für den Gast, lwIP für das Netz. Übernommener Code wird
   getrennt gezählt.

### Sichtbar sein

6. **Messen statt schätzen.** Budgets für Größe, RAM, Codezeilen und Bootzeit. Der Kernel
   prüft sie beim Start selbst und schreibt die Zeile `gate OK` oder `gate FAILED`
   (wie der `bench GATE` von RCP-OS); die CI prüft dieselben Zahlen aus derselben Quelle.
7. **Alles ist inspizierbar.** Der Zustand des Systems liegt als lesbare Tabellen unter
   `/now` (Prozesse, Speicher, Geräte, Ressourcen, Datenträger, Netz, Budgets). Wer wissen
   will, was los ist, liest eine Datei ([09](09-mikro-welt.md#now--der-zustand-als-tabellen)).
8. **Fehler sind Exponate.** Ein Absturz endet nie still: Ein abgestürzter Prozess bleibt
   mit Registern und Ursache sichtbar, bis jemand ihn abräumt; eine Kernel-Panik zeigt
   alles auf dem Bildschirm und auf der seriellen Schnittstelle.
9. **Eine Wahrheit pro Tatsache.** Layouts stehen einmal als C-Header mit
   `_Static_assert`; dasselbe C-Modul läuft in Kernel, Werkzeugen und Tests (etwa
   `budget.h`, der Dateisystemtreiber, der `main.cfg`-Parser). Entscheidungen stehen mit
   Datum in den Dokumenten, und das Dokument wird zuerst geändert.

### Ehrlich sein

10. **Explizit statt automatisch.** Kein Overcommit, kein OOM-Killer, kein automatisches
    Auslagern, keine Hintergrunddienste „für alle Fälle“, keine Caches ohne feste Grenze,
    keine Updates oder Telemetrie im Hintergrund. Speicherdruck ist ein Ereignis, auf das
    Programme reagieren; Auslagern ist ein bewusster Lebenszyklus-Schritt (Suspend).
11. **Grenzen sind Entscheidungen.** Namenslänge, Warteschlangentiefen, Logrößen, Handles
    pro Prozess: Jede Grenze ist ein bewusst gewählter, dokumentierter Wert, kein Zufall
    und kein „unbegrenzt“.
12. **Keine Rechte ohne Handle.** Was ein Prozess darf, steht vollständig in seiner
    Handle-Tabelle. Es gibt keine globale Autorität.
13. **Direktzugriff ist erlaubt, wenn er ausdrücklich vergeben wurde** und widerrufbar
    ist: ein flach gemappter Framebuffer, IO-Ports, eine Lease auf den Bildschirm.

### Sparsam sein

14. **Jedes Aufwachen hat einen Grund.** Kein periodischer Takt, kein Pollen. Der Bildschirm
    ändert sich nur dort, wo sich Zellen ändern; ein ruhender Bildschirm kostet nichts.
    Schatten und Abdunkeln sind Zellen-Schattierungen, keine Transparenz. Energie für
    bewegte Bilder, Web-Darstellung und Vollbild fällt nur an, solange sie sichtbar sind.

### Menschlich sein

15. **Einschalten und tippen.** Die leere Oberfläche ist eine Konsole mit Prompt, wie beim
    Heimcomputer. Es gibt keinen Splash-Screen; ein Banner ist Konfiguration.
16. **Ein Byte, ein Zeichen, in der Mikro-Welt.** Konsole, Meldungen, Konfiguration und
    Namen verwenden CP437 bzw. ASCII. Unicode ist Sache des Gasts.
17. **Eine Konfigurationsdatei.** `/sys/main.cfg` im Format `key = value`, beim Boot einmal
    gelesen. Ein unbekannter Schlüssel macht die ganze Datei ungültig, und das System startet
    mit dem Referenzprofil.
18. **Austauschbar über Protokolle, nicht über Bibliotheken.** Wer mit einem Treiber
    spricht, spricht mit einer Geräteklasse. Wer mit dem Gast spricht, spricht mit einem
    Prozess, dem man den Bildschirm wieder abnehmen kann.

## Nicht-Ziele

- **Volle POSIX-Kompatibilität.** Kein `fork`, keine Signale, keine Unix-Rechte, keine
  Dateideskriptoren im Kernel. Die POSIX-Teilmenge gehört dem Gast.
- **Binärkompatibilität** zu Linux, Windows oder Android.
- **Mikrocontroller ohne MMU.** Die gehören RCP-OS. „Embedded“ meint hier ARMv8-A oder
  x86-64 mit MMU: Einplatinenrechner, Kiosk- und Industrie-PCs, Thin Clients.
- **32-Bit-Architekturen.**
- **Eine eigene Browser-Engine.** Chromium wird portiert, nicht ersetzt.
- **Ein Objekt-Framework im Kernel**, ebenso wenig Plug-in-Systeme in der Mikro-Welt.

## Budgets

Die Zahlen des Kernels stehen in [`kernel/core/budget.h`](../kernel/core/budget.h); Kernel
und `tools/budget.py` lesen dieselbe Datei. Dieses Dokument ändert sich nur zusammen mit ihr.

### Größe

| Komponente | Harte Grenze | Ziel | Stand M0 |
|---|---|---|---|
| Kernel-Image | 1 MiB | ≤ 256 KiB | 28 KiB |
| Mikro-Welt auf dem Datenträger (Kernel, Dienste, Treiber, Werkzeuge) | 10 MiB | ≤ 2 MiB | – |
| Gast (Chromium, Mesa-Modul, libc, Daten) | – | gemessen und ausgewiesen | – |

### Code

| Komponente | Ziel | Stand M0 |
|---|---|---|
| Kernel, eigener Code (Kern und eine Architektur; C und Assembler, ohne Leer- und Kommentarzeilen) | ≤ 10.000 Zeilen | 1.472 |
| Mikro-Welt gesamt, eigener Code | ≤ 60.000 Zeilen | – |
| Übernommener Code (lwIP, musl, Mesa, Chromium) | wird getrennt ausgewiesen | – |

### RAM, resident

Gemessen wird im Profil `minimal` nach dem Boot im Leerlauf: alle physischen Seiten, die
Kernel und Prozesse belegen. Nicht mitgezählt werden Firmware-Speicher, ACPI-Tabellen und
der Framebuffer.

| Posten | Ziel | Stand M0 |
|---|---|---|
| Kernel-Code und -Konstanten | 160 KiB | 20 KiB |
| Kernel-Daten, Objekt-Pools, Handle-Tabellen | 48 KiB | 32 KiB (mit CPU-Anteil) |
| Pro CPU (Kernel-Stapel, TSS, Vektortabelle, Run-Queue) | 16 KiB | |
| Direct-Map-Seitentabellen (1-GiB-Seiten) | 8 KiB | – (noch die des Bootloaders) |
| `init` (mit Namensdienst und Starter) | 80 KiB | – |
| `devmgr` | 72 KiB | – |
| Treiber `uart16550` | 48 KiB | – |
| `term` (Bodenkonsole über die serielle Schnittstelle) | 64 KiB | – |
| **Summe (1 CPU)** | **≤ 496 KiB**, harte Grenze 512 KiB | |

Jede weitere CPU kostet etwa 16 KiB. Die größten Hebel sind **Seitentabellen** (etwa
16–24 KiB je Prozess), **Stapel** (Seiten werden erst bei Zugriff belegt) und die **Zahl
der Prozesse**. Deshalb beherbergt `init` im Minimalprofil den Namensdienst und den Starter.

Messung ab M2: `/now/mem` und `/now/budget` zeigen die belegten Seiten nach Kategorie;
`make test` liest sie über die serielle Schnittstelle und lässt den Build bei einer
gerissenen Grenze fehlschlagen.

### Zeit

| Strecke | Ziel | Stand M0 (QEMU, ohne KVM) |
|---|---|---|
| Kernel-Eintritt bis Leerlauf | ≤ 50 ms | 14–20 ms |
| Übergabe des Bootloaders bis Prompt der Bodenkonsole | ≤ 1 s | – |
| Aufwachen im Leerlauf ohne Ereignis | nur, wenn sich die angezeigte Uhr ändert | 1× pro Minute |

### Der Platz für Nutzer

Planwerte für ein Desktop-Gerät mit 4 GiB; die tatsächlichen Werte stehen in `/now/mem`.

| Bereich | Planwert | Festgelegt durch |
|---|---|---|
| Mikro-Welt ohne GUI und Netz | ≤ 0,5 MiB | Budget oben |
| Netzstack mit Puffern | ≤ 4 MiB | `net_memory` in `main.cfg` |
| Dateisystem-Cache | fester Wert | `fs_cache` in `main.cfg` |
| Bildschirmpuffer der Konsole und der Anzeige | ≈ 2 × Auflösung × 4 Byte | Anzeige |
| Gast (Chromium) | Kontingent | `guest_memory` in `main.cfg` |
| **Rest** | **frei für Inhalte und Verbindungen der Nutzer** | |

## Profile

Es gibt einen Kernel und ein Userland. Ein Profil ist nichts weiter als der Inhalt von
`main.cfg`: Welche Dienste starten, welcher Gast, welche Kontingente. Die Bodenkonsole gibt
es in jedem Profil.

| Profil | Inhalt | Zweck |
|---|---|---|
| `minimal` | Kernel, init, devmgr, uart16550, term | Budget-Nachweis, Kernel-Tests |
| `embedded` | + Dateisystem, Anzeige, Eingabe, Fensterschicht, optional Netz; Programme der Mikro-Welt | Kiosk, Steuerung, Anzeige ohne Browser |
| `desktop` | + GPU-Treiber, Netz, Audio, der Gast | das vollständige System |

## Familie RCP-OS

rctos und RCP-OS teilen Konventionen, damit Wissen, Daten und Werkzeuge zwischen beiden
wandern können. Wo sich die Hardware unterscheidet, unterscheidet sich die Technik, nicht
die Haltung.

| Gemeinsam | In rctos |
|---|---|
| `main.cfg` (`key = value`, Ganz-Datei-Regel, Referenzprofil) | [09](09-mikro-welt.md#maincfg) |
| `/now`-Tabellen, Serialisierung als kommagetrennte Zeilen | [09](09-mikro-welt.md#now--der-zustand-als-tabellen) |
| Namensregeln (28 Byte, `0-9 A-Z a-z . _`) und uid-Bereiche, Heimat `/usr/<uid>` | [10](10-rcfs.md), [03](03-rechte.md#identität-und-sitzungen) |
| Dateisystem-Familie rcp-fs: 4-KiB-Blöcke, 64-Byte-Einträge, Ringdateien für Logs | [10](10-rcfs.md) |
| Zellenmodell 1:1: `TEXT`/`ATTR`/`CTRL`, Zeichensatz-Slots, Paletten in RGB332, Schattierung, Ebenen-Zellen | [11](11-fenster.md) |
| Fenster auf dem Raster, Leiste mit höchstens zehn Programmen, Status-Kachel `HH:MM  U <uid>`, Bodenkonsole, Vollbild-Oberflächen statt Popups | [11](11-fenster.md), [09](09-mikro-welt.md) |
| CP437 in der Konsole, ASCII in Quellen, Sonderzeichen als `\xNN` | [09](09-mikro-welt.md#zeichensatz) |
| Recaster-Blau `#2449AA` (RGB332 `0x2A`) als Eintrag 1 der Palette | Kernel-Konsole |
| Boot-Epoche 2026-10-09 10:10 UTC für reproduzierbare Tests | `tools/qemu.py --rtc family` |

| Anders, weil die Hardware anders ist | Grund |
|---|---|
| Hardware-MMU statt Software-MMU und Bytecode-VM | x86-64 und ARMv8-A haben eine MMU |
| Ausführen am Ort aus dem RAM-Boot-Image statt XIP aus Flash | PCs haben Blockgeräte, keinen memory-mapped Flash |
| Zellen-Compositor nach Schaden statt Zeilen-Compositor, keine Raster-Effekte je Zeile | die Anzeige liest aus dem Speicher, es gibt keinen Strahl |
| Unicode im Gast | das Web ist Unicode |

Offene Familienfrage: Die Prompt-Sprache der Bodenkonsole soll CAST werden
([09](09-mikro-welt.md#die-prompt-sprache)).

## Entscheidungen

| Datum | Entscheidung |
|---|---|
| 2026-09-28 | Sprache für Kernel, Kits, Dienste und Treiber ist **C11** (Kernel freestanding, Clang). Assembler nur in `kernel/arch/`. **Kein Rust im eigenen Code.** |
| 2026-09-28 | Das **Leitbild Mikro-Umgebung** aus RCP-OS gilt; Chromium ist der Gast und die eine Ausnahme. |
| 2026-09-28 | Mikro-Welt ohne libc; POSIX nur im Gast. |
| 2026-09-28 | Kein Overcommit, kein OOM-Killer, kein automatisches Auslagern ([01](01-kernel.md#virtueller-speicher)). |
| 2026-09-28 | Systemzustand als `/now`-Tabellen; Fehler als Exponate. |
| 2026-09-28 | Dateisystem aus der rcp-fs-Familie (`rcfs`) statt ext2 ([10](10-rcfs.md)). |
| 2026-09-28 | Konsole: CP437, Palette 0 = EGA mit Recaster-Blau. |
| 2026-09-28 | Zellen sind 8 × 8 Pixel, Schrift ist `charmap01.png` der Familie (RCP-OS); die PNG ist die eine Wahrheit, `tools/mkfont.py` erzeugt daraus den C-Code. |
| 2026-09-28 | Zellen werden nur auf Wunsch verdoppelt, und nur als ganze Verdopplung jeder Zeile und Spalte (2 × 2): `cell_double` in `main.cfg`, Vorgabe `no`. Keine automatische Skalierung. |
| 2026-09-28 | Die Fensterschicht gehört der Mikro-Welt; das **Zellenmodell von RCP-OS gilt 1:1** ([11](11-fenster.md)). Chromium ist eine Render-Instanz in Fenstern oder im Vollbild. |
| 2026-09-28 | Chromium wird nur mit der Content-Schicht eingebettet (`surf`), ohne `//chrome` und Ash. |
| 2026-09-28 | Credo: reduziertes System, volle Kraft für den User-Space, Bedienung durch Führung, ein 8 × 8-Raster für alles, Web in Fenstern oder im Vollbild. |
| 2026-09-28 | Bootloader Limine 11.4.1 (Basisrevision 6), Version und Prüfsummen fest in `boot/limine.sha256`. |

## Offene Entscheidungen

| Thema | Empfehlung | Alternative | Fällig bis |
|---|---|---|---|
| Beschreibung der Protokolle | handgeschriebene C-Header mit festen Layouts; ein Generator erst ab etwa 10 Protokollen | eigene kleine IDL | M2 |
| Prompt-Sprache der Bodenkonsole | CAST (Familie); M2 startet mit dem Rettungs-Prompt von RCP-OS | eigener minimaler Befehlsinterpreter | M5 |
| Netzstack | lwIP (C, klein, ausgereift) | eigener minimaler IPv4/IPv6-Stack | M3 |
| rcfs: Dateien über 4 GiB | v1 begrenzt auf 4 GiB je Datei (bewusste Grenze) | 64-Bit-Größe in einer Formatrevision | M3 |
| Sprites in der Fensterschicht | Überlagerungen im OAM-Format von RCP-OS | weglassen | M2 |
| ARM-Referenzboard | Raspberry Pi 5 (V3D, Mesa `v3dv`) | RK3588-Board (Mali-G610, Mesa `panvk`) | M9 |

## Glossar

| Begriff | Bedeutung |
|---|---|
| Mikro-Welt | alles außer dem Gast: Kernel, Dienste, Treiber, Werkzeuge, Konsole |
| Gast | Chromium mit Begleitern; bekommt Bildschirm und Kontingent geliehen |
| Bodenkonsole | die Konsole, die den Bildschirm hat, wenn ihn niemand sonst hat |
| `/now` | lesbare Tabellen mit dem aktuellen Systemzustand |
| Exponat | ein Fehler, der sichtbar stehen bleibt, bis jemand ihn ansieht |
| GATE | die Budgetprüfung beim Boot und in der CI |
| Handle | prozesslokale Nummer, die auf ein Kernel-Objekt zeigt und Rechte trägt |
| VMO | Speicherobjekt (Virtual Memory Object): Seiten, die man einblenden kann |
| Kanal | bidirektionale Nachrichtenverbindung, die Bytes und Handles überträgt |
| Port | Warteschlange, auf der ein Thread auf viele Ereignisse gleichzeitig wartet |
| Ressource | Handle, das Hardware-Zugriff erlaubt (MMIO, IO-Ports, IRQs, …) |
| BTI | Bus-Transaction-Initiator: DMA-Identität eines Geräts gegenüber der IOMMU |
| Kit | Gruppe von API-Aufrufen: System-Kit, Treiber-Kit, User-Kit |
| Treiberpaket | Treiberprozess, Manifest und optional ein Treibermodul (z. B. ein Vulkan-ICD) |
| Lease | zeitweise exklusive Nutzung eines Geräts, etwa eines Bildschirms; widerrufbar |
| Zelle | 8 × 8 Pixel mit drei Bytes: Zeichen (`TEXT`), Farben (`ATTR`), Steuerung (`CTRL`) |
| Fensterschicht | `win`: Fenstertabelle, Besitzkarte, Zellen-Compositor; die Politik macht `shell` |
| Render-Instanz | Quelle für den Inhalt eines Fensterbereichs: Zellen, Leinwand, Fläche, GPU/Web |
| Ringdatei | Datei fester Größe, die sich selbst überschreibt: die letzten N KiB, immer |
| rcfs | das Dateisystem von rctos aus der rcp-fs-Familie |
