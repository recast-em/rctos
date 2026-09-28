# 10 – rcfs, das Dateisystem (Entwurf v0.1)

Stand: Entwurf vom 2026-09-28. Grundlage ist rcp-fs Draft v0.3 aus RCP-OS. rcfs ersetzt
die frühere Planung mit ext2: ext2 bringt Inodes, Unix-Rechte und Blockgruppen mit, also
genau die Schichten, die die Mikro-Welt nicht braucht.

## Ziele

1. **Die Prinzipien von rcp-fs** auf Blockgeräten (SSD, Festplatte, USB-Stick) und
   RAM-Volumes, so wenig verändert wie möglich.
2. **Familie:** rctos liest und schreibt rcp-fs-Volumes direkt (SD-Karten, Abbilder aus dem
   Web-Emulator). rcfs-Volumes verwenden dieselben Verzeichniseinträge.
3. **Ein C-Modul** für den Dateidienst `fs`, das Host-Werkzeug `mkrcfs` und die Tests;
   anzustreben ist dasselbe Modul wie `rcpfs.c` von RCP-OS, bei dem nur die Ein-/Ausgabe
   verschieden ist.

## Was bleibt wie in rcp-fs

- **Blockgröße 4 KiB** = Seitengröße.
- **Byte-genaue Strukturen** ohne Füllbytes, gespiegelt in einem C-Header mit
  `_Static_assert`.
- **Superblock** in Block 0, 32 Byte, mit CRC; nur `mkrcfs` schreibt ihn.
- **Verzeichniseintrag = Dateibeschreibung, genau 64 Byte:** `name[28]`, `size` (u32),
  `mtime` (u32), `flags` (u8), `uid` (u8), `extents_num` (u16) und 24 Byte `storage`
  (Inhalt direkt im Eintrag bis 24 Byte, 1–3 Extents oder eine Kette).
- **Flags:** `FILE`, `DIR`, `INLINE`, `READONLY`, `SYSTEM` (Extents festgenagelt),
  `EXTENDED`, `PRIVATE`, `RING`.
- **Namen:** höchstens 28 Byte aus `0-9 A-Z a-z . _`; `..` und `__` sind nirgends erlaubt
  (`__` ist für eine spätere Unicode-Kodierung reserviert).
- **Identität:** uid als u8 mit den Bereichen der Familie, Schreibmatrix, Stempel der
  erzeugenden uid, kein `chown` ([03](03-rechte.md#identität-und-sitzungen)).
- **Ringdateien** (`RING`) für Logs: feste Größe, Zeigerwort statt Größe, chronologisch
  lesbar.
- **Löschen** setzt die Flags eines Eintrags auf `0x00`.
- **Jeder Mount ist eine Prüfung:** Der Belegungszustand wird beim Mount im RAM aus dem
  Verzeichnisbaum neu aufgebaut. Ein Fehler im Baum (Extent außerhalb, doppelte Belegung)
  macht das Volume nur lesbar, statt stillschweigend weiterzuschreiben.

## Was sich wegen der Größe ändert

| Thema | rcp-fs | rcfs | Grund |
|---|---|---|---|
| Volume-Größe | bis 2 GB | bis 16 TiB (u32-Blocknummern) | SSDs |
| Freiraum im RAM | Bitmap, 1 Bit je Block | **Liste freier Extents** (8 Byte je freiem Bereich), beim Mount gebaut | eine Bitmap für 1 TiB wären 32 MiB RAM; Extents kosten je Lücke, nicht je Gigabyte |
| Bitmap auf dem Datenträger | veralteter Schnappschuss in Block 0 | keine (`bitmap_blocks = 0`) | nichts, was konsistent gehalten werden müsste |
| Schreibdisziplin | NOR-Flash: nur 1→0 ohne Löschen, sonst Lesen-Löschen-Schreiben | Daten zuerst, dann der Eintrag; Reihenfolge per Flush | absturzsicher ohne Journal |
| Partitionen | keine | Datenträger ohne Partitionstabelle; Boot-Datenträger mit GPT: EFI-Systempartition (FAT32, Limine und Boot-Image) plus eine rcfs-Partition | UEFI braucht eine ESP |
| Ausführen am Ort | XIP aus dem Flash | Boot-Image im RAM: Code-Seiten werden direkt eingeblendet; auf Datenträgern teilen sich Instanzen die Code-Seiten im Cache | Blockgeräte sind nicht in den Adressraum eingeblendet |
| Swap | festgenagelte Datei `swap`, nur bei explizitem Suspend | ebenso, und optional | gleiche Haltung |
| Dateigröße | bis 4 GiB − 1 | in v1 ebenso, als bewusste Grenze | offener Punkt 1 |

## Absturzsicherheit ohne Journal

Die Wahrheit ist der Verzeichnisbaum; alles andere wird beim Mount aus ihm abgeleitet.

- **Schreiben:** Blöcke aus der freien Liste im RAM nehmen, Daten schreiben, Flush, dann
  den 64-Byte-Eintrag ändern, Flush.
- **Absturz vor dem Eintrag:** Die neuen Blöcke gehören niemandem und sind beim nächsten
  Mount wieder frei. **Absturz danach:** alles konsistent.
- **Löschen:** Flags auf `0x00`, Flush. Die Blöcke sind sofort im RAM frei und beim
  nächsten Mount ohnehin.
- **Atomarität:** Ein 64-Byte-Eintrag überschreitet nie eine 512-Byte-Sektorgrenze
  (64 teilt 512). Selbst Geräte mit 512-Byte-Sektoren schreiben einen Eintrag also
  ganz oder gar nicht.

## Kosten des Mounts

100.000 Einträge sind 6,4 MB Verzeichnisblöcke: auf einer SSD wenige zehn Millisekunden
Lesen. Der RAM-Bedarf der freien Liste hängt von der Zersplitterung ab, nicht von der
Größe des Volumes. Beides wird in M3 gemessen und steht dann in `/now/vol`.

## Volumes und Namensraum

Die oberste Ebene des Namensraums ist eine Bindungstabelle, keine Verzeichnisebene eines
Volumes (angelehnt an RCP-OS):

| Pfad | Gebunden an |
|---|---|
| `/sys` | Boot-Volume: rcfs-Abbild im RAM, nur lesbar, Programme laufen am Ort |
| `/app` | Anwendungen auf dem System-Volume |
| `/usr` | Heimatverzeichnisse `/usr/<uid>` auf dem System-Volume |
| `/log` | Ringdateien auf dem System-Volume |
| `/tmp` | RAM-Volume, Obergrenze `tmp_max` |
| `/now` | virtuelle Tabellen ([09](09-mikro-welt.md#now--der-zustand-als-tabellen)) |
| `/vol/<name>` | weitere Datenträger, auch rcp-fs-Volumes von RCP-OS |

Das **Boot-Volume** baut `mkrcfs` aus `/sys` und `/app` des Quellbaums. Es legt die
ELF-Segmente jedes Programms auf 4-KiB-Grenzen, damit der Lader Code-Seiten direkt aus dem
Abbild einblenden kann, ohne zu kopieren.

## Offene Punkte

1. **Dateien über 4 GiB:** in v1 nicht möglich. Eine Formatrevision mit 64-Bit-Größe ist
   denkbar, bricht aber das 64-Byte-Layout der Familie.
2. **Umbenennen und Verschieben** fehlt, wie in rcp-fs.
3. **Unicode-Namen** über das Präfix `__`: Entscheidung gemeinsam mit RCP-OS.
4. **TRIM** für SSDs beim Freigeben von Extents.
5. **Zeitstempel:** u32 bis zum Jahr 2106, wie in der Familie.
