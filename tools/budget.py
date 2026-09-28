#!/usr/bin/env python3
"""budget.py - prüft das Kernel-Image gegen kernel/core/budget.h (eine Wahrheit).

Liest die ladbaren Segmente der ELF-Datei, rundet auf Seiten wie der Kernel
selbst und gibt die GATE-Zeile aus. Exit 1, wenn eine harte Grenze reißt;
ein überschrittenes Ziel ist eine Warnung.
Aufruf: budget.py <kernel.elf> <budget.h> <zeilen>
"""
import re
import struct
import sys

PAGE = 4096


def page_up(n):
    return (n + PAGE - 1) // PAGE * PAGE


def segments(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit("budget: keine ELF64-Datei: %s" % path)
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    for i in range(phnum):
        ptype, flags, _off, _va, _pa, filesz, memsz, _al = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phentsize)
        if ptype == 1:  # PT_LOAD
            yield flags, filesz, memsz


def main():
    elf, header, lines = sys.argv[1], sys.argv[2], int(sys.argv[3])
    budget = {m.group(1): int(m.group(2)) for m in
              re.finditer(r"#define\s+RC_BUDGET_(\w+)\s+(\d+)", open(header).read())}
    code = data = image = 0
    for flags, filesz, memsz in segments(elf):
        image += page_up(filesz)
        if flags & 2:            # beschreibbar: Daten und bss
            data += page_up(memsz)
        else:                    # ausführbar oder nur lesbar: Code und Konstanten
            code += page_up(memsz)
    kib = 1024
    rows = [
        ("code+const", "%d KiB" % (code // kib), code, budget["CODE_TARGET_KIB"] * kib, False),
        ("data+bss", "%d KiB" % (data // kib), data, budget["DATA_TARGET_KIB"] * kib, False),
        ("image", "%d KiB" % (image // kib), image, budget["IMAGE_TARGET_KIB"] * kib, False),
        ("image", "%d KiB" % (image // kib), image, budget["IMAGE_LIMIT_KIB"] * kib, True),
        ("lines", "{:,}".format(lines), lines, budget["LINES_TARGET"], False),
    ]
    failed = False
    for name, text, value, limit, hard in rows:
        unit = "{:,}".format(limit) if name == "lines" else "%d KiB" % (limit // kib)
        state = "ok" if value <= limit else ("OVER LIMIT" if hard else "over target")
        print("budget  %-10s %9s  of %s %s  %s" % (
            name, text, unit, "limit" if hard else "target", state))
        failed |= hard and value > limit
    print("GATE FAILED" if failed else "GATE OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
