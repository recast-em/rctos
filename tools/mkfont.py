#!/usr/bin/env python3
"""mkfont - baut die Konsolenschrift des Kernels aus raw_assets/charmap01.png.

Die PNG-Datei ist die eine Wahrheit, wie in RCP-OS: ein Raster von 16 x 16
Zeichen zu je 8 x 8 Pixeln in CP437-Reihenfolge, helle Pixel sind gesetzt.
Ausgabe: eine C-Datei mit 256 Zeichen zu je 8 Zeilen (Bit 7 = linkes Pixel).
Ohne Bildbibliothek: PNG (8 Bit, Graustufen/RGB/RGBA, ohne Interlace) wird
hier selbst entpackt.
"""
import struct
import sys
import zlib

CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("mkfont: keine PNG-Datei: %s" % path)
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or ctype not in CHANNELS or interlace:
                sys.exit("mkfont: nur 8-Bit-PNG ohne Palette und Interlace")
        elif kind == b"IDAT":
            idat += chunk
    bpp = CHANNELS[ctype]
    stride, raw, rows, prev, p = width * bpp, zlib.decompress(idat), [], None, 0
    prev = bytearray(width * bpp)
    for _ in range(height):
        filt, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if filt == 1:
                line[i] = (line[i] + a) & 255
            elif filt == 2:
                line[i] = (line[i] + b) & 255
            elif filt == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif filt == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, bpp, rows


def main():
    if len(sys.argv) != 3:
        sys.exit("aufruf: mkfont.py <charmap01.png> <ziel.c>")
    width, height, bpp, rows = read_png(sys.argv[1])
    if (width, height) != (128, 128):
        sys.exit("mkfont: erwartet 128 x 128 Pixel (16 x 16 Zeichen zu 8 x 8)")

    def ink(x, y):
        px = rows[y][x * bpp:(x + 1) * bpp]
        alpha = px[3] if bpp == 4 else px[1] if bpp == 2 else 255
        return alpha > 127 and sum(px[:3 if bpp >= 3 else 1]) / (3 if bpp >= 3 else 1) > 127

    out = ["/* Generiert von tools/mkfont.py aus raw_assets/charmap01.png - nicht von Hand",
           " * bearbeiten. Die Zeichensatz-Schrift der Familie (RCP-OS, Recaster), CP437,",
           " * 8 x 8 Pixel; Bit 7 ist das linke Pixel. */",
           '#include "font.h"', "", "const uint8_t rc_font8x8[256][8] = {"]
    for code in range(256):
        cx, cy = code % 16 * 8, code // 16 * 8
        glyph = [sum(0x80 >> x for x in range(8) if ink(cx + x, cy + y)) for y in range(8)]
        out.append("    [0x%02x] = { %s }," % (code, ", ".join("0x%02x" % b for b in glyph)))
    out.append("};")
    open(sys.argv[2], "w").write("\n".join(out) + "\n")
    print("mkfont: 256 Zeichen aus %s" % sys.argv[1])


if __name__ == "__main__":
    main()
