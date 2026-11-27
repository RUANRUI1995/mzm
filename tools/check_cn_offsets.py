#!/usr/bin/env python3
"""Sanity-check the CN symbol offsets in port/generated/*_rom.c.

The iQue release (BMXC) has no .map, so tools/gen_cn_region_case.py recovers
its addresses instead of reading them: most are found by searching the US
symbol's bytes inside the CN ROM, the rest by carrying the shift of the
neighbouring database.json resource across. This script checks the result
against the invariants that do not need the ROM, so CI catches a generator
regression (a file left without a CN case, a symbol dropped, an address that
cannot be in an 8MB ROM) without anyone shipping a copyrighted ROM.

What is checked:
  1. every file whose US case binds symbols also has a CN case
  2. the CN case binds exactly the symbols the US case binds
  3. every CN address is inside the GBA ROM window [0x08000000, 0x09000000)
  4. one US address never maps to two different CN addresses
  5. the offsets are reported: a correct mapping moves whole segments, so the
     number of distinct shifts should stay far below the symbol count

What is NOT checked (needs the ROM): whether the bytes at a recovered address
are the right ones. That is what the pointer-table check in the generator's
docstring and a run on hardware are for.

Exits 1 on the first class of failure found, printing every offending file.
"""

import glob
import os
import re
import sys

GENERATED = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "port", "generated", "*_rom.c")

LINE = re.compile(r"^\s*(\w+) = .*Port_ResolveRomData\((0x[0-9a-f]+)u\)", re.M)
CASE = "case PORT_ROM_REGION_%s: {(.*?)\n\\s*break;"

ROM_START = 0x08000000
ROM_END = 0x09000000  # the iQue ROM is 8MB, like the retail release


def read_case(src, region):
    m = re.search(CASE % region, src, re.S)
    return dict(LINE.findall(m.group(1))) if m else None


def main():
    errors = []
    files = 0
    symbols = 0
    us_to_cn = {}
    shifts = {}
    empty_us = 0

    for path in sorted(glob.glob(GENERATED)):
        name = os.path.basename(path)
        with open(path, encoding="utf-8", newline="") as f:
            src = f.read()

        us = read_case(src, "US")
        cn = read_case(src, "CN")
        if not us:
            # No US binding => nothing to recover. Skip, do not report.
            empty_us += 1
            continue
        files += 1

        if cn is None:
            errors.append("%s: US binds %d symbols but there is no CN case" % (name, len(us)))
            continue

        missing = sorted(set(us) - set(cn))
        extra = sorted(set(cn) - set(us))
        if missing:
            errors.append("%s: CN case is missing %d symbol(s): %s"
                          % (name, len(missing), ", ".join(missing[:4])))
        if extra:
            errors.append("%s: CN case binds %d symbol(s) the US case does not: %s"
                          % (name, len(extra), ", ".join(extra[:4])))

        for sym, addr in cn.items():
            value = int(addr, 16)
            if not (ROM_START <= value < ROM_END):
                errors.append("%s: %s resolves to 0x%08X, outside the 8MB ROM window"
                              % (name, sym, value))
                continue
            symbols += 1
            base = us.get(sym)
            if base is None:
                continue
            prev = us_to_cn.setdefault(base, value)
            if prev != value:
                errors.append("%s: US 0x%08X maps to both 0x%08X and 0x%08X (%s)"
                              % (name, int(base, 16), prev, value, sym))
            shift = value - int(base, 16)
            shifts[shift] = shifts.get(shift, 0) + 1

    if errors:
        print("CN offset check FAILED (%d problem(s)):" % len(errors))
        for e in errors:
            print("  " + e)
        return 1

    print("CN offset check passed")
    print("  files with a US binding: %d (%d without, skipped)" % (files, empty_us))
    print("  symbols bound in both:   %d" % symbols)
    if shifts:
        print("  distinct shifts:         %d (min 0x%X, max 0x%X)"
              % (len(shifts), min(shifts), max(shifts)))
        top = sorted(shifts.items(), key=lambda kv: -kv[1])[:3]
        print("  largest segments:        " + ", ".join("0x%X (%d symbols)" % kv for kv in top))
        if len(shifts) > symbols / 4:
            # A byte search that matched the wrong place shows up here: the
            # offsets stop clustering into a handful of segment shifts.
            print("  WARNING: shifts are not clustered; check the generator's matches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
