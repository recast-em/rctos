# 09 – Die Mikro-Welt

Die Mikro-Welt ist alles außer dem Gast: Kernel, Dienste, Treiber, Werkzeuge, die
Bodenkonsole und die Systemoberflächen. Dieses Dokument beschreibt, wie sie aussieht, wie
man sie bedient und wie sie über sich Auskunft gibt. Vieles ist aus RCP-OS übernommen; die
Quellen dort sind `shell-v3.md`, `cast-shell.md`, `architecture.md` und `rcp-fs.md`.

## Überblick

```
┌────────────────────────── Gast: Chromium (Kontingent, Lease) ──────────────────────────┐
└─────────────────────────────────────────┬─────────────────────────────────────────────┘
                                          │ Kanäle: Dateien, Netz, Anzeige, Eingabe, GPU
┌──────────────────────────────── Mikro-Welt ───────────────────────────────────────────┐
│ console     Bodenkonsole: Prompt auf Bildschirm und serieller Schnittstelle           │
│ login       Anmeldung, Sperre, Benutzerwechsel (Vollbild-Oberfläche)                  │
│ fs          Dateisystem rcfs, Namensraum, /tmp                                        │
│ net         Netzstack (lwIP)                                                          │
│ devmgr      Geräte, Treiber, Ressourcen                                               │
│ init        Start, Namensdienst, Starter, /now                                        │
│ Treiber     je ein Prozess                                                            │
│ Kernel      Adressräume, Threads, IPC, Handles, Interrupts, Zeit                      │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

Jeder Prozess der Mikro-Welt ist ein kleines, statisch gelinktes Programm, das nur die Kits
benutzt. Keiner braucht eine libc, keiner läuft „für alle Fälle“: Was nicht in `main.cfg`
steht, startet nicht.

## `main.cfg`

Die eine Konfigurationsdatei, wie in RCP-OS.

- **Ort:** `/sys/main.cfg` auf dem Boot-Volume. `init` liest sie einmal beim Start; der
  Kernel liest keine Konfiguration.
- **Format:** eine Zeile pro Schlüssel, `key = value`, Kommentare mit `;`, Leerzeichen um
  `=` beliebig. Nur ASCII. Kommt ein Schlüssel doppelt vor, gilt die letzte Zeile.
- **Ganz-Datei-Regel:** Ein unbekannter Schlüssel oder ein ungültiger Wert macht die ganze
  Datei ungültig. Dann startet das **Referenzprofil**, das in `init` eingebaut ist
  (Profil `minimal` mit Konsole auf Bildschirm und serieller Schnittstelle), und die
  Konsole nennt Datei, Zeile und Grund. Ein Tippfehler kann das System so nie unbedienbar
  machen.
- **Referenzprofil erzwingen:** ein zweiter Eintrag im Limine-Menü mit der Kommandozeile
  `reference` (das Gegenstück zu BOOTSEL bei RCP-OS).
- **Neue Schlüssel:** Ein Schlüssel wird erst hier dokumentiert und in den Parser
  aufgenommen, bevor eine ausgelieferte `main.cfg` ihn benutzt. Parser und Schlüsseltabelle
  sind ein C-Modul, das `init`, `tools/` und die Tests gemeinsam benutzen.

Startsatz der Schlüssel (er wächst mit den Meilensteinen):

| Schlüssel | Werte | Vorgabe | Bedeutung |
|---|---|---|---|
| `profile` | `minimal`, `embedded`, `desktop` | `minimal` | welche Dienste starten |
| `start` | Programmpfade, durch Leerzeichen getrennt | – | zusätzliche Programme |
| `console` | `screen`, `serial`, `both` | `both` | wo die Bodenkonsole lebt |
| `ground_init` | eine Prompt-Zeile | – | läuft unsichtbar vor dem ersten Prompt |
| `guest` | Programmpfad | – | der Gast (Chromium-Einbettung) |
| `guest_memory` | Größe (`K`, `M`, `G`) | 50 % des RAM | Kontingent des Gasts |
| `net_memory` | Größe | `4M` | Puffer des Netzstacks |
| `fs_cache` | Größe | `64M` | Dateisystem-Cache, feste Obergrenze |
| `tmp_max` | Größe | `16M` | RAM-Volume `/tmp` |
| `log_size` | Größe | `64K` | Größe jeder Ringdatei unter `/log` |
| `user_login` | `none`, `anonymous`, Kontoname | `none` | automatische Anmeldung wie in RCP-OS |
| `lock_color` | `#RRGGBB` | `#dbdbaa` | Farbe der Anmelde- und Sperroberfläche |

```ini
; /sys/main.cfg - Beispiel für einen Desktop
profile      = desktop
guest        = /app/surf
guest_memory = 2G
fs_cache     = 128M
user_login   = none
```

## Die Bodenkonsole

**Die leere Oberfläche ist eine Konsole.** Wer das Gerät einschaltet, bekommt einen Prompt,
wie beim Heimcomputer. Die Regeln übernehmen die „Ground Console“ von RCP-OS:

1. **Sie hat den Bildschirm, wenn ihn niemand sonst hat.** Hält der Gast die Lease auf die
   Anzeige, schläft die Konsole; ihre Sitzung (Verzeichnis, Variablen, Verlauf) bleibt
   erhalten. Endet der Gast oder stürzt er ab, fällt die Lease an die Konsole zurück, und
   sie steht wieder, mit leerem Bildschirm.
2. **Sie ist immer auch auf der seriellen Schnittstelle da.** Dieselben Bytes, dieselbe
   Sitzung: Ein Gerät ohne Bildschirm ist genauso bedienbar.
3. **Kein Rückspeicher.** Der Bildschirm ist ihr Speicher. Zellen werden nur gemalt, wenn
   sie sich ändern, und dann genau einmal. So arbeitet schon die Kernel-Konsole von M0.
4. **Aussehen:** Zellen von 8 × 16 Pixeln, CP437, Palette 0 aus den EGA-Farben mit dem
   Recaster-Blau als Eintrag 1; schwarzer Grund. Die letzte Zeile ist die **Statuszeile**:
   links der Zustand, rechts die Kachel `HH:MM  U <uid>`. Die Uhr zeigt bewusst keine
   Sekunden: Ohne Sekunden gibt es nichts zu animieren, und die CPU schläft bis zur
   nächsten Minute.
5. **Erste Zeile:** `ground_init` aus `main.cfg` läuft unsichtbar vor dem ersten Prompt.
6. **Der Bildschirm wird geliehen.** Programme, die aus der Konsole starten, dürfen mit der
   Berechtigung `framebuffer.map` direkt auf den Bildschirm zeichnen; beim Ende des
   Programms fällt er an die Konsole zurück ([03](03-rechte.md#direktzugriff)).
7. **Die sichere Tastenkombination** (Vorgabe Strg+Alt+Entf) führt immer zur Mikro-Welt
   zurück, egal wer die Lease hält.

## Die Prompt-Sprache

Ziel ist **CAST**, die Sprache der Familie: Der Prompt ist das REPL der Systemsprache, und
Shell-Skripte sind Programme („one environment“ in RCP-OS). Der Weg dorthin:

- **M2: der Rettungs-Prompt.** Er entspricht dem Rückfall-Prompt von RCP-OS: Das erste
  Wort einer Zeile ist ein Programm unter `/sys`, die Argumente sind wörtlich, `$dir`,
  `$uid`, `$home` und `$ver` werden ersetzt, `$$` ist ein Dollar. Dazu kommen `run <pfad>
  [args] [&]` und `quit`. Die Programme heißen wie ihre Geschwister in RCP-OS: `help`,
  `ls`, `cat`, `tail`, `echo`, `cd`, `cwd`, `cp`, `rm`, `mkdir`, `tasks`, `mem`, `vol`,
  `kill`, `resume`, `reset`, `date`, `hex`, `uptime`.
- **M5: CAST.** Zwei Wege, Entscheidung in M5:
  - **(a) Die RCP-VM als Prozess.** Der Bytecode-Interpreter von RCP-OS läuft als
    rctos-Programm und führt `.rpx`-Binärdateien unverändert aus, auch den
    selbstgehosteten Compiler `cast.rpx`. Die Syscalls der VM werden auf Kits abgebildet.
    Damit laufen alle Programme der Familie sofort, und Nutzer-Code bekommt eine kleine,
    inspizierbare Sandbox.
  - **(b) Ein natives CAST-Backend** für x86-64 und AArch64: schneller, aber ein zweiter
    Compiler-Zweig.

  Empfehlung: (a) zuerst, (b) nur, wenn Messungen es verlangen.

## `/now` – der Zustand als Tabellen

Der Zustand des Systems ist lesbar wie Dateien; das ist die Plan-9-Antwort, die RCP-OS
gewählt hat.

- `/now` ist eine **virtuelle Bindung** im Namensraum, kein Verzeichnis auf einem
  Datenträger. Gelesen wird sie vom `/now`-Server in `init`, der dazu die Ressource
  `INSPECT` hält ([03](03-rechte.md#ressourcen)). Jeder Zugriff liefert den Zustand in
  diesem Moment.
- **Format:** eine Zeile pro Datensatz, Felder durch Kommas getrennt, nur das letzte Feld
  darf freier Text sein. Das ist der Serialisierungsvertrag von RCP-OS, also liest
  `cat /now/tasks` dieselbe Tabelle wie später ein CAST-`list tasks`.
- **Datensätze** haben feste Layouts, die in C-Headern mit `_Static_assert` stehen. Kernel,
  `/now`-Server und Werkzeuge benutzen denselben Header.

| Pfad | Felder | Quelle |
|---|---|---|
| `/now/tasks` | `id, state, uid, pages, handles, threads, prio, exit, name` | Kernel (`sys_inspect`) |
| `/now/mem` | `pool, total, used` (kernel-code, kernel-data, percpu, pagetables, user, cache, guest, free) | Kernel |
| `/now/budget` | `name, used, target, limit, state` | Kernel und `init` |
| `/now/boot` | der Boot-Bericht des Kernels | Kernel |
| `/now/log` | das Kernel-Log (Ring fester Größe) | Kernel (`sys_log_read`) |
| `/now/devs` | `name, driver, class, state, bus, address` | `devmgr` |
| `/now/res` | `kind, range, owner` (das Ressourcenbuch) | `devmgr` |
| `/now/vol` | `kind, writable, block, blocks, free` | `fs` |
| `/now/net` | `if, state, address, rx, tx` | `net` |
| `/now/screen` | die Zellen der Konsole als Text | `console` |

Wer die Tabellen lesen darf: alle angemeldeten Sitzungen, außer `/now/log` (nur die
Admin-Sitzung). Programme der Mikro-Welt sind nur Sichten auf diese Quellen: Ein späteres
Programm „Task“ zeigt `/now/tasks` in einem Fenster, genauso wie in RCP-OS.

## Logs sind Ringdateien

**Logs wachsen nie.** Jedes Log hat eine feste Größe und enthält immer die letzten N
Kilobyte.

- Das **Kernel-Log** ist ein Ring im Kernel-Speicher (8 KiB), lesbar über `/now/log`.
- Die **Logs der Dienste** liegen als Ringdateien (`RING`, [10](10-rcfs.md)) unter `/log`,
  jede so groß wie `log_size`. `cat` liest sie chronologisch, `tail` die neuesten Zeilen,
  ohne dass ein Werkzeug etwas über Ringe wissen muss.

## Fehler sind Exponate

- **Kernel-Panik:** Bildschirm und serielle Schnittstelle zeigen Ursache und alle
  Register; der Kernel hält an. In M0 umgesetzt und getestet.
- **Absturz eines Prozesses:** Der Prozess geht in den Zustand `faulted`. Sein Datensatz
  (Ausnahme, Adresse, Register, Exit-Code) bleibt in `/now/tasks` stehen, bis der
  Elternprozess ihn abräumt. Der Starter schreibt eine Zeile nach `/log/faults.log`.
- **Absturz eines Treibers:** `devmgr` startet neu oder fällt auf den nächsten Treiber der
  Kette zurück ([04](04-treibermodell.md#austausch-und-rückfall)) und schreibt, was
  geschah.

## Systemoberflächen: Vollbild statt Popup

Wie in RCP-OS gibt es auf Systemebene **keine Popups**. Jeder Dialog der Mikro-Welt ist ein
Wechsel der ganzen Oberfläche.

- **Anmeldung, Sperre und Benutzerwechsel** sind eine Oberfläche in drei Zuständen,
  gezeichnet vom Programm `login` der Mikro-Welt, nicht vom Gast. Das Passwort geht nie
  durch Chromium. Die sichere Tastenkombination holt diese Oberfläche jederzeit; die Lease
  des Gasts ruht so lange.
- **Stil:** wie in RCP-OS ein Vorhang in `lock_color`, der Inhalt rastet zellweise ein.
  Die Zelle ist die ehrliche Bewegungseinheit einer Textoberfläche.
- **Hinweise der Mikro-Welt** an den Nutzer, etwa „Grafiktreiber neu gestartet“, stehen in
  der Statuszeile der Konsole und in `/log`, nicht in einem Fenster über dem Gast.

## Werkzeuge

- Jedes Werkzeug ist ein statisches Programm, das nur Kits benutzt; Ziel **≤ 16 KiB** je
  Programm, gemessen in der CI.
- Ein Werkzeug tut eine Sache. Tabellen kommen aus `/now`, Dateien aus dem Dateisystem;
  es gibt keine zweite Datenquelle „nur für die Anzeige“.
- Ablage: Systemprogramme in `/sys`, Anwendungen in `/app`, Programme der Nutzer in
  `/usr/<uid>`.

## Zeichensatz

- **Ein Byte, ein Zeichen.** Konsole, Meldungen, Logs, `main.cfg` und Dateinamen verwenden
  CP437. Die Namen sind zusätzlich auf `0-9 A-Z a-z . _` beschränkt ([10](10-rcfs.md)).
- **Quellen:** Zeichenketten, die den Bildschirm oder die serielle Schnittstelle erreichen,
  sind ASCII. Ein CP437-Zeichen oberhalb von 0x7F wird als Bytewert geschrieben (`"\xFA"`),
  nie getippt. Dokumente und Kommentare sind UTF-8-Prosa.
- **Meldungen des Systems** sind englisch und kurz; die Dokumente sind deutsch.
- **Unicode** ist Sache des Gasts. Übernimmt der Gast einen Dateinamen, etwa bei einem
  Download, wird er an der Grenze zum Dateisystem umgeschrieben (`ä` → `ae`, sonst `_`).
  Die Familie reserviert `__` als Präfix für eine spätere Unicode-Kodierung von Namen.

## Der Bildschirm in M0

Der Boot-Bericht, den der Kernel in M0 zeigt (Bootloader, CPU, Speicher, Anzeige, ACPI,
Zeit, Budgets, GATE, Log, Statuszeile), ist die Entwicklungsansicht. Ab M2 gehört der
Bildschirm der Konsole: Der Kernel schreibt den Bericht dann auf die serielle
Schnittstelle und nach `/now/boot`. Auf dem Bildschirm erscheint er nur ohne das Wort
`quiet` auf der Kernel-Kommandozeile (`cmdline` in `boot/limine.conf`); `main.cfg` liest
der Kernel nie.
