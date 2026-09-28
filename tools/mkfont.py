#!/usr/bin/env python3
"""mkfont - baut die CP437-Konsolenschrift des Kernels aus einer PSF2-Schrift.

Eingabe: eine PSF1- oder PSF2-Datei mit Unicode-Tabelle (z. B. spleen-8x16.psfu.gz).
Ausgabe: eine C-Datei mit 256 Zeichen zu je 16 Zeilen (8 Pixel breit),
in CP437-Reihenfolge: ein Byte ist ein Zeichen (docs/09-mikro-welt.md).

Zeichen, die in der Quelle fehlen, werden als Rahmen gezeichnet und im
Kopf der erzeugten Datei aufgelistet - nichts wird verschwiegen.
"""
import gzip
import struct
import sys

# CP437: 0x00-0x1F und 0x7F sind Grafikzeichen, der Rest folgt dem Python-Codec.
CP437_LOW = [
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
]


# Eigene Ergänzungen im Stil der Quelle (2-Pixel-Striche, Versalien in den
# Zeilen 2-11, Mittellänge 5-11). Sie füllen Lücken der Quelle; lesbar und
# änderbar direkt hier: "#" ist ein gesetztes Pixel, jede Zeile 8 Pixel.
EXTRA = {
    0x25BA: ["", "", "", "", "#", "###", "#####", "#######", "#####", "###", "#"],        # ►
    0x25C4: ["", "", "", "", "......#", "....###", "..#####", "#######",
             "..#####", "....###", "......#"],                                               # ◄
    0x221F: ["", "", "", "", "", "##", "##", "##", "##", "##", "#######", "#######"],       # ∟
    0x03B1: ["", "", "", "", "", ".###.##", "##.###", "##..##", "##..##", "##..##",
             "##.###", ".###.##"],                                                           # α
    0x03A3: ["", "", "#######", "##", ".##", "..##", "...##", "...##", "..##", ".##",
             "##", "#######"],                                                               # Σ
    0x03C3: ["", "", "", "", "", ".######", "##...##", "##...##", "##...##", "##...##",
             "##...##", ".#####"],                                                           # σ
    0x03C4: ["", "", "", "", "", "#######", "...##", "...##", "...##", "...##", "...##",
             "....###"],                                                                     # τ
    0x0398: ["", "", ".#####", "##...##", "##...##", "##...##", "#######", "##...##",
             "##...##", "##...##", "##...##", ".#####"],                                     # Θ
    0x03B4: ["", "", ".#####", "##", ".##", "..###", ".##.##", "##...##", "##...##",
             "##...##", "##...##", ".#####"],                                                # δ
    0x03C6: ["", "", "", "...#", "...#", ".#####", "##.#.##", "##.#.##", "##.#.##",
             "##.#.##", ".#####", "...#", "...#", "...#"],                                   # φ
    0x03B5: ["", "", "", "", "", ".######", "##", "##", ".####", "##", "##", ".######"],    # ε
}


def art(rows):
    out = bytearray(16)
    for r, line in enumerate(rows):
        for c, ch in enumerate(line.ljust(8, ".")[:8]):
            if ch == "#":
                out[r] |= 0x80 >> c
    return bytes(out)


def cp437_to_unicode(code):
    if code < 0x20:
        return CP437_LOW[code]
    if code == 0x7F:
        return 0x2302
    return ord(bytes([code]).decode("cp437"))


def read_psf(path):
    """Liest PSF1 oder PSF2 samt Unicode-Tabelle: (glyphen, {codepoint: index})."""
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    table = {}
    if raw[:2] == b"\x36\x04":                      # PSF1
        mode, size = raw[2], raw[3]
        count = 512 if mode & 1 else 256
        width, height, hsize = 8, size, 4
        glyphs = [raw[hsize + i * size: hsize + (i + 1) * size] for i in range(count)]
        if mode & 2:
            pos = hsize + count * size
            for index in range(count):
                seq = False
                while True:
                    (uni,) = struct.unpack_from("<H", raw, pos)
                    pos += 2
                    if uni == 0xFFFF:
                        break
                    if uni == 0xFFFE:
                        seq = True
                    elif not seq:
                        table.setdefault(uni, index)
    else:                                              # PSF2
        magic, _ver, hsize, flags, count, size, height, width = struct.unpack_from("<8I", raw)
        if magic != 0x864AB572:
            sys.exit("mkfont: weder PSF1 noch PSF2: %s" % path)
        glyphs = [raw[hsize + i * size: hsize + (i + 1) * size] for i in range(count)]
        if flags & 1:
            pos = hsize + count * size
            for index in range(count):
                end = raw.index(b"\xff", pos)
                entry = raw[pos:end]
                pos = end + 1
                # Nur Einzelzeichen, keine Sequenzen (0xFE leitet Sequenzen ein).
                for ch in entry.split(b"\xfe")[0].decode("utf-8", "replace"):
                    table.setdefault(ord(ch), index)
    if width != 8 or height != 16:
        sys.exit("mkfont: erwartet 8x16, gefunden %dx%d" % (width, height))
    return glyphs, table


def main():
    if len(sys.argv) != 3:
        sys.exit("aufruf: mkfont.py <quelle.psfu[.gz]> <ziel.c>")
    src, dst = sys.argv[1], sys.argv[2]
    glyphs, table = read_psf(src)
    box = bytes([0x00, 0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x42,
                 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00, 0x00, 0x00])
    rows, missing, added = [], [], []
    for code in range(256):
        uni = cp437_to_unicode(code)
        if uni in (0x0000, 0x00A0):
            rows.append(bytes(16))
        elif uni in table:
            rows.append(glyphs[table[uni]])
        elif uni in EXTRA:
            rows.append(art(EXTRA[uni]))
            added.append("0x%02X (U+%04X)" % (code, uni))
        else:
            rows.append(box)
            missing.append("0x%02X (U+%04X)" % (code, uni))
    out = []
    out.append("/* Generiert von tools/mkfont.py - nicht von Hand bearbeiten.")
    out.append(" *")
    out.append(" * CP437-Konsolenschrift, 8 x 16 Pixel, abgeleitet aus Spleen 8x16")
    out.append(" * (c) 2018-2024 Frederic Cambus, BSD-2-Clause, siehe LICENSES/spleen.txt.")
    out.append(" *")
    if added:
        out.append(" * Selbst gezeichnet (EXTRA in tools/mkfont.py), weil die Quelle sie nicht hat:")
        for i in range(0, len(added), 4):
            out.append(" *   " + ", ".join(added[i:i + 4]))
    if missing:
        out.append(" * In der Quelle fehlen %d Zeichen, sie erscheinen als Rahmen:" % len(missing))
        for i in range(0, len(missing), 4):
            out.append(" *   " + ", ".join(missing[i:i + 4]))
    else:
        out.append(" * Alle 256 Zeichen sind vorhanden.")
    out.append(" */")
    out.append("#include \"font.h\"")
    out.append("")
    out.append("const uint8_t rc_font8x16[256][16] = {")
    for code, glyph in enumerate(rows):
        body = ", ".join("0x%02x" % b for b in glyph)
        out.append("    [0x%02x] = { %s }," % (code, body))
    out.append("};")
    open(dst, "w").write("\n".join(out) + "\n")
    print("mkfont: %d Zeichen, davon %d selbst gezeichnet, %d fehlen"
          % (256 - len(missing), len(added), len(missing)))
    for m in missing:
        print("  fehlt: " + m)


if __name__ == "__main__":
    main()
