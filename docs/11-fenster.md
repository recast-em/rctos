# 11 – Fensterschicht und Zellenmodell

Stand: 2026-09-28. Das Zellenmodell ist **1:1 aus RCP-OS übernommen** (`architecture.md`,
`gfx-engine.md`, `windows.md`, `shell-v3.md`). Wo rctos abweicht, steht es ausdrücklich da,
mit Grund.

## Grundsatz

Die Oberfläche gehört der Mikro-Welt. Alles, was man sieht, liegt auf einem Raster von
**8 × 8 Pixeln**. Fenster rasten auf Zellen ein, jedes Fenster ist zugleich ein
Zellen-Terminal, und neu gezeichnet wird nur, was sich ändert. Moderne Web-Darstellung und
GPU-Grafik sind **Render-Instanzen in Fenstern** oder im Vollbild. Chromium verwaltet keine
Fenster mehr, er füllt sie.

## Das Zellenmodell (wie RCP-OS)

### Drei Ebenen je Zelle

Jede Zelle hat drei Bytes, als drei getrennte Ebenen gespeichert (erst alle `TEXT`, dann
alle `ATTR`, dann alle `CTRL`):

| Ebene | Inhalt |
|---|---|
| `TEXT` | Zeichen 0–255 im Zeichensatz der Zelle; bei einer Ebenen-Zelle die Ebenen-ID |
| `ATTR` | `fg \| bg << 4`: zwei Indizes in die Palette der Zelle (4-Bit-Zellen nutzen nur `bg`) |
| `CTRL` | Bits 0–1 `pal` (Palette 0–3), Bit 2 `shade`, Bit 3 `type` (0 = 1-Bit-Zeichensatz, 1 = 4-Bit-Zeichensatz), Bits 4–7 `map` (Zeichensatz-Slot 0–15) |

`CTRL = 0` ist die Normalzelle: Palette 0, nicht schattiert, Zeichen aus Slot 0.

### Zeichensätze (Slots)

Ein Zeichensatz hat 256 Zeichen zu 8 × 8 Pixeln.

| Slot | Inhalt |
|---|---|
| 0 | die Textschrift der Familie, `raw_assets/charmap01.png` (CP437) |
| 1 | das Symbolblatt, `raw_assets/charmap02.png`: 64 Symbole zu 16 × 16 (je vier Codes); `0x00`–`0x3F` Systemsymbole, `0x40`–`0xFF` Slots für Programmsymbole |
| 2–14 | gehören je einem Programm: 2.048 Byte (1 Bit) oder 8.192 Byte (4 Bit) in dessen Speicher |
| 15 | **Ebenen-Zelle:** `TEXT` ist eine Ebenen-ID, die 8 × 8 Pixel kommen aus einer linearen 1- oder 4-Bit-Ebene des Programms (Leinwand) |

1 Bit: `char * 8 + row`, ein Byte, MSB links, 1 = `fg`. 4 Bit: `char * 32 + row * 4`, vier
Bytes, zwei Pixel je Byte, oberes Halbbyte links, Wert = Palettenindex; 4-Bit-Zellen sind
deckend. Eine Zelle, deren Slot nicht (mehr) belegt ist oder den falschen Typ hat, malt
flächig `palette[pal][bg]`. Freigegebener Speicher wird so nie gelesen.

### Farben

- **Jede Farbe ist ein RGB332-Byte** (`RRRGGGBB`), wie in der Familie. Erweitert wird durch
  Bitwiederholung (`r8 = r3 << 5 | r3 << 2 | r3 >> 1`, `b8 = b2 * 0x55`).
- **Paletten:** 4 × 16 Einträge. Palette 0 ist das System-EGA, Eintrag 1 das Recaster-Blau
  `0x2A`; die Fenster-Chrome nutzt Palette 1; Palette 2 und 3 gehören den Programmen.
- **Schattierung:** In einer Zelle mit `shade` wird jede Farbe zu
  `DIM(c) = (c >> 1) & 0x6D` (jeder Kanal halbiert). Das ist die einzige Form von
  Abdunklung im System.

Abweichung: Bilder, Video, GPU- und Web-Inhalte sind echtfarbig (siehe
[Render-Instanzen](#render-instanzen)). RGB332 gilt für alles, was aus Zellen besteht.

## Fenster

- **Eine Fensterart, Rasterpflicht.** Position und Größe eines Fensters sind immer ganze
  Zellen. Es gibt keine Pixel-Fenster und keine Rahmenlinien; Kontrast entsteht durch die
  Hintergrundfarbe jedes Fensters.
- **Inhalt im Speicher des Besitzers.** Die Zellebenen eines Fensters liegen in einem VMO
  des Programms (0, 1 oder 3 Byte je Zelle, je nach Zellmodus). Die Fensterschicht blendet
  es nur lesend ein und stellt jeden freigelegten Bereich selbst wieder her; nichts wird von
  einem anderen Fenster gelöscht.
- **Zellmodi** wie in RCP-OS: `CELLS_NONE` (nur Leinwand), `CELLS_TEXT` (ein Attribut für
  alles), `CELLS_FULL` (alle drei Ebenen).
- **Titelzeile:** Die erste Zeile ist Chrome. Der Titel steht in der proportionalen
  3 × 5-Schrift der Familie, gerastert in Ebene 0 des Fensters; Knöpfe sind Zeichen.
- **Schatten:** Die Zellen eine Spalte rechts und eine Zeile unter einem Fenster werden
  schattiert (`shade`). Was darunter liegt, erscheint abgedunkelt, in Zellengenauigkeit.
- **Ohne Rückspeicher** (`UNBACKED`): Ein Fenster kann auf eigene Zellebenen verzichten;
  dann fordert die Schicht bei Bedarf genau die beschädigten Zeilen neu an. So läuft der
  Dateimanager in RCP-OS in 4 KiB.
- **Die Bodenkonsole** ist das Fenster ohne Chrome über den ganzen Bildschirm, das nur
  existiert, solange kein anderes Fenster offen ist ([09](09-mikro-welt.md#die-bodenkonsole)).

## Render-Instanzen

Ein Fenster füllt seinen Inhaltsbereich aus einer oder mehreren Render-Instanzen. Jede
Instanz belegt ein Rechteck aus ganzen Zellen.

| Instanz | Pixel kommen aus | Typische Nutzer | Kosten |
|---|---|---|---|
| **Zellen** | `TEXT`/`ATTR`/`CTRL` + Zeichensätze + Paletten | Terminal, TUI, Dateimanager, Systemprogramme | ein getipptes Zeichen = eine Zelle |
| **Leinwand** | Ebenen-Zellen (Slot 15), 1 oder 4 Bit | Zeichnen, Diagramme, einfache Spiele | nur geänderte Zellen |
| **Fläche** | ein echtfarbiger Puffer (32 Bit) im Speicher des Programms | Bilder, Video in Software, Bildschirmfotos | Kopie nur der gemeldeten Schadenszellen |
| **GPU / Web** | Grafikpuffer (VMO) aus Vulkan, z. B. von Chromium | Web-Seiten, 3D, beschleunigtes Video | pro geliefertem Frame; nichts, solange verdeckt |

Jede Instanz meldet **Schaden** in Zellen. Nur beschädigte, sichtbare Zellen werden neu
zusammengesetzt.

## Die Fensterschicht und die Shell

Wie in RCP-OS sind Mechanismus und Politik getrennt:

- **`win` (Fensterschicht, Mechanismus):** hält die Lease auf die Anzeige, führt die
  Fenstertabelle, die **Besitzkarte** (für jede Bildschirmzelle: welches Fenster ist oben)
  und die Zeichensatz-Slots, setzt Zellen zusammen, liefert Treffer für Mausklicks und
  verteilt Eingaben an das Fenster mit Fokus.
- **`shell` (Politik, entspricht `wm` in RCP-OS):** Leiste, Status-Kachel, Platzieren,
  Verschieben und Größe ändern, Minimieren, Fokusregeln, Grund der Oberfläche, Vorhang der
  Anmeldung.

### Zusammensetzen

Weil Fenster auf Zellen einrasten, gehört jede Bildschirmzelle genau einem Fenster (oder
dem Grund). Die Besitzkarte macht Verdeckung trivial.

1. Programme melden Schaden (Zellen) oder liefern einen Frame (GPU/Web, mit Schadensrechteck).
2. `win` schlägt für jede beschädigte Zelle den Besitzer nach, malt die Zelle **genau
   einmal** (Zellebenen, Leinwand, Fläche oder GPU-Puffer), wendet `shade` an und merkt
   sich die Zeile als geändert.
3. Ist etwas geändert, wird zum nächsten VBlank umgeschaltet (Page-Flip). Sonst geschieht
   nichts: **Es gibt keinen Frame-Takt.**

Zellen malt die CPU direkt in den Scanout-Puffer. GPU- und Web-Fenster setzt die CPU
anfangs ebenfalls zusammen (Kopie der Schadenszellen); mit dem GPU-Treiber übernimmt die
GPU diese Fenster und legt Vollbild-Inhalte direkt auf den Bildschirm
([05](05-grafik.md#stufen-der-anzeige)).

### Leiste und Status-Kachel (wie RCP-OS)

- Die **Leiste** liegt in den untersten vier Zellzeilen: Slots von 4 × 4 Zellen mit den
  16 × 16-Symbolen der angehefteten Programme (2 × 2 Zellen aus Slot 1), höchstens zehn
  angeheftete Programme aus `/usr/<uid>/.profile` (`app.<n> = <pfad>`). `main.cfg`:
  `appbar = on | auto | off`.
- Die **Status-Kachel** rechts zeigt `HH:MM` (ohne Sekunden) und `U <uid>`; ein Klick
  holt die Sperroberfläche.
- **Keine Popups.** Systemdialoge sind ganze Oberflächen ([09](09-mikro-welt.md#systemoberflächen-vollbild-statt-popup)).

### Sprites

Sprites sind **Überlagerungen im OAM-Format von RCP-OS** (entschieden 2026-09-28):

- 128 Einträge zu 8 Byte: `i16 X` (`0x8000` = verborgen, der einzige Ausschalter), `i16 Y`,
  `u16 pattern` (Bits 0–11 Zeilenversatz × 4 Byte ab der Belegung, Bit 14 = Ebene),
  `u8 Höhe − 1`, `u8 flags` (Bits 0–1 Modus, Bits 2–3 Palette, Bits 4–7 Farbe).
- Alle 32 Pixel breit (eine `u32`-Musterzeile, Bit 31 links), 1–128 Pixel hoch; gesetzte
  Bits malen `palette[pal][color]`, gelöschte sind durchsichtig. Innerhalb einer Ebene liegt
  der kleinere OAM-Index vorn.
- **Zwei Ebenen:** L0 vor allem, L1 hinter den Zellen-Schattierungen; in schattierten Zellen
  wird L1 mit `DIM` gemalt, L0 schwebt darüber.
- **Mustersatz und OAM werden gemeinsam vergeben** (`NEW`, `PATCH`, `SHARE` wie
  `SYS_OAM_LOAD`); OAM 0 und 1 gehören der Shell (Markierung und Tooltip). Das Ende eines
  Programms gibt seine Einträge frei.
- **Atomar bewegen:** Änderungen werden gesammelt und zum nächsten Umschalten gemeinsam
  sichtbar (wie `SYS_OAM_SET_V`).
- **Unterschied zu RCP-OS:** Es gibt keine Grenze von 16 Sprites je Zeile und daher kein
  Flackern durch Multiplexing. Ein bewegtes Sprite beschädigt nur die Zellen, die es alt und
  neu berührt; `win` setzt genau diese neu zusammen.

### Zeiger

Der Mauszeiger ist eine eigene Überlagerung über allem, auf echter Hardware die
Hardware-Cursor-Ebene der Anzeige. Er bewegt sich ohne ein einziges Neuzeichnen von Zellen.

## Energie

- **Kein Frame-Takt, kein Pollen.** `win` schläft, bis Schaden oder Eingabe kommt.
- **Ein ruhender Bildschirm kostet nichts.** Auf Laptops mit Panel Self Refresh ruhen dann
  Grafikchip und Verbindung.
- **Verdeckte und minimierte Fenster bekommen keine Frames.** Chromium erfährt über
  Ozone, dass sein Fenster nicht sichtbar ist, und hört auf zu zeichnen.
- **Vollbild auf Wunsch:** Ein Programm mit `display.fullscreen` bekommt die ganze Anzeige
  (Lease); der Anzeige-Treiber legt seinen Puffer direkt auf den Bildschirm. Das kostet die
  Energie des Programms, nicht die des Zusammensetzens.

## Was aus RCP-OS nicht übernommen wird

| RCP-OS | rctos | Grund |
|---|---|---|
| Zeilen-Compositor auf Core 1, Scanline-Budget | Zellen-Compositor nach Schaden, Page-Flip bei VBlank | die Anzeige liest aus dem Speicher; es gibt keinen Strahl |
| Segmente A/B, Raster-Effekte je Zeile, `SYS_LINE_WAIT` | – | ohne Strahl kein Rennen mit ihm |
| 128 Sprites (OAM), 16 je Zeile | 128 Sprites als Überlagerungen der Fensterschicht im gleichen OAM-Format, **ohne** Grenze je Zeile ([Sprites](#sprites)) | kein Zeilenbudget; dieselbe Programmierschnittstelle wie in der Familie |
| fester Bildschirm 640 × 480, 80 × 60 Zellen | Raster so groß wie die Anzeige; `cell_double` verdoppelt Zeilen und Spalten | PC-Bildschirme sind größer |
| Mono-Profil (1 Bit) | – | es gibt keine 1-Bit-Ausgabe |

## Offene Punkte

1. **Symbolblatt:** `charmap02.png` aus RCP-OS übernehmen (Slot 1).
2. **EGA-Palette als RGB332:** die exakten Bytes von RCP-OS übernehmen; der Kernel von M0
   rechnet noch mit 24-Bit-Werten.
3. **Schnittstelle:** Syscalls wie `SYS_WIN_*` in RCP-OS oder ein Protokoll von `win`;
   Empfehlung: Protokoll von `win` mit denselben Namen und Semantiken, damit die RCP-VM sie
   1:1 abbilden kann.
