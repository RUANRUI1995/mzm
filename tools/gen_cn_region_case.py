#!/usr/bin/env python3
"""Add the iQue (CN) case to the generated per-symbol ROM offset tables.

gen_port_rom_offsets.py builds port/generated/*_rom.c from each region's .map
file. There is no mzm_cn.map -- the iQue release is a separate localisation,
so it cannot be produced by the matching build. Its symbol addresses are
recovered here instead, from the addresses the US build already knows.

The recovery runs in passes, most reliable first:

1. Search the US bytes at that address inside the CN ROM. Most data is byte
   identical and the layout shifts in whole segments, so this is exact.
2. A table the localisation moved wholesale rather than shifted. The text
   pointer tables were relocated to the tail of the ROM while the text they
   point at stayed put, so the table has one shift and its targets another.
   Those segments are listed in MOVED, with the evidence for each.
3. Pointer tables. Their contents hold absolute addresses, which differ
   between the two builds even though the table itself keeps its shape, so the
   bytes never match: the shift has to come from the anchors instead. The
   nearest byte-matched symbols on either side bracket it, and the table's own
   shape (which entries are equal to which) confirms the guess. Nothing needs
   patching afterwards -- a pointer table in the loaded ROM already holds iQue
   addresses, because it *is* the iQue ROM.
4. Everything else falls back to the shift of the nearest database.json
   resource, which is how the non-pointer data was placed all along.

A pointer table that no shift explains is left out of the CN case, so the
symbol stays null and the failure is loud rather than silently reading the
wrong bytes. Those symbols are written to tools/cn_unresolved.txt.

    python3 tools/gen_cn_region_case.py --us <us.gba> --cn <cn.gba> --apply
"""
import argparse
import bisect
import glob
import json
import os
import re
import struct
import sys

PROBE = 24
TWEAK = 8

# How much of a table to compare when checking a guess. Sixteen words is long
# enough that the equality pattern is distinctive and short enough that a table
# with a couple of entries edited still matches.
SHAPE_WORDS = 16
SHAPE_MIN_PTRS = 6

UNRESOLVED_LIST = os.path.join("tools", "cn_unresolved.txt")

# Segments the localisation moved instead of shifting, as (us_start, us_end,
# shift, why). `shift` is cn - us for the table itself.
#
# The text pointer tables: in the US build they sit at 0x760420..0x760AA0,
# right behind the location names they index. In the iQue build the same runs
# of pointers appear at 0x79F620..0x79FCA0 -- the same lengths (1058, 376, 34,
# 35, 416, 52 entries), in the same order, with the same equality pattern in
# every table -- but the text they point at moved down by 0x2220C, not by the
# table's own shift. So the tables were re-emitted at the end of the ROM rather
# than carried along with their data.
MOVED = [
    (0x760420, 0x760AA0, 0x3F200, "text pointer tables moved to the tail of the ROM"),
]

LINE = re.compile(r'^(\s*)(\w+) = (.*?)Port_ResolveRomData\((0x[0-9a-f]+)u\);(.*)$')


def load_ranges(db_path):
    db = json.load(open(db_path, encoding="utf-8"))
    items = []
    for e in db:
        us = e.get("addr", {}).get("us")
        cn = e.get("addr", {}).get("cn")
        if not us or not cn:
            continue
        count = e["count"]
        if isinstance(count, dict):
            count = count.get("us")
        if count is None:
            continue
        items.append((int(us, 16), int(cn, 16), int(count, 16) * e["size"], e["path"]))
    items.sort()
    return items, [i[0] for i in items]


def nearest_candidates(items, starts, off):
    """CN addresses implied by the resource before and after `off`."""
    j = bisect.bisect_right(starts, off) - 1
    out = []
    if j >= 0:
        out.append((abs(off - items[j][0]), items[j][1] + (off - items[j][0])))
    if j + 1 < len(items):
        out.append((abs(items[j + 1][0] - off), items[j + 1][1] - (items[j + 1][0] - off)))
    out.sort()
    return [c for _, c in out]


def ladder(anchors, anchor_offs, off):
    """Shifts of the byte-matched symbols bracketing `off`, nearest first.

    The localisation moves whole segments, so the shift barely changes between
    one anchor and the next; the ones on either side are the best available
    guess for anything in between.
    """
    j = bisect.bisect_left(anchor_offs, off)
    out = []
    if j > 0:
        out.append(anchors[j - 1][1])
    if j < len(anchors):
        out.append(anchors[j][1])
    return out


def shape(buf, off):
    """Equality pattern of SHAPE_WORDS words at `off`, or None if not a table.

    Returns None when the words do not look like a table of addresses at all,
    which is what keeps ordinary data out of the pointer-table path.
    """
    if off < 0 or off + 4 * SHAPE_WORDS > len(buf):
        return None
    vals = struct.unpack_from("<%dI" % SHAPE_WORDS, buf, off)
    if sum(1 for v in vals if 0x08000000 <= v < 0x09000000) < SHAPE_MIN_PTRS:
        return None
    ids = {}
    return tuple(ids.setdefault(v, len(ids)) for v in vals)


def byte_match(us, cn, items, starts, off):
    """Exact copy of the US bytes, near a resource's predicted address."""
    probe = us[off:off + PROBE]
    if len(probe) < PROBE:
        return None, None
    for cand in nearest_candidates(items, starts, off)[:2]:
        if cand < 0 or cand + PROBE > len(cn):
            continue
        if cn[cand:cand + PROBE] == probe:
            return cand, "match"
        for d in range(-TWEAK, TWEAK + 1):
            p = cand + d
            if p >= 0 and p + PROBE <= len(cn) and cn[p:p + PROBE] == probe:
                return p, "match+tweak"
    return None, None


def resolve(us, cn, items, starts, anchors, anchor_offs, off):
    """Return (cn_offset, method); cn_offset is None if it cannot be trusted."""
    got, how = byte_match(us, cn, items, starts, off)
    if got is not None:
        return got, how

    for lo, hi, shift, _why in MOVED:
        if lo <= off <= hi:
            return off + shift, "moved"

    want = shape(us, off)
    if want is not None:
        for shift in ladder(anchors, anchor_offs, off):
            p = off + shift
            if shape(cn, p) == want:
                return p, "shift+shape"
        for cand in nearest_candidates(items, starts, off):
            if shape(cn, cand) == want:
                return cand, "resource+shape"
        # A table nothing explains: guessing would read the wrong bytes.
        return None, "no-shape"

    cands = nearest_candidates(items, starts, off)
    if cands and 0 <= cands[0] < len(cn):
        return cands[0], "shift"
    return None, "unresolved"


def iter_symbols(path):
    """Yield (indent, name, type_prefix, us_gba_addr, suffix) for the US case."""
    in_us = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not in_us:
                if "case PORT_ROM_REGION_US:" in line:
                    in_us = True
                continue
            if "break;" in line:
                return
            m = LINE.match(line.rstrip("\n"))
            if m:
                yield m.groups()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--us", required=True)
    ap.add_argument("--cn", required=True)
    ap.add_argument("--db", default="database.json")
    ap.add_argument("--glob", default="port/generated/*_rom.c")
    ap.add_argument("--apply", action="store_true", help="write the files")
    ap.add_argument("--limit", type=int, default=0, help="only the first N files")
    args = ap.parse_args()

    us = open(args.us, "rb").read()
    cn = open(args.cn, "rb").read()
    items, starts = load_ranges(args.db)
    print(f"resource ranges: {len(items)}")

    files = sorted(glob.glob(args.glob))
    if args.limit:
        files = files[:args.limit]

    # Pass one: read every symbol and byte-match the ones that allow it. Those
    # matches are the anchors the second pass measures shifts against.
    symbols = []
    anchors = []
    for path in files:
        for indent, name, typ, addr, suffix in iter_symbols(path):
            off = int(addr, 16) - 0x08000000
            symbols.append((path, indent, name, typ, off, suffix))
            got, how = byte_match(us, cn, items, starts, off)
            if got is not None:
                anchors.append((off, got - off))
    anchors.sort()
    anchor_offs = [a for a, _ in anchors]
    print(f"symbols {len(symbols)}: byte-matched {len(anchors)}")

    # Pass two: place the rest, file by file.
    totals = {}
    unresolved = []
    per_file = {}
    for path, indent, name, typ, off, suffix in symbols:
        got, how = resolve(us, cn, items, starts, anchors, anchor_offs, off)
        totals[how] = totals.get(how, 0) + 1
        per_file.setdefault(path, []).append((indent, name, typ, got, suffix))
        if got is None:
            unresolved.append((path, name))

    if args.apply:
        for path, rows in per_file.items():
            rows = [r for r in rows if r[3] is not None]
            if not rows:
                continue
            with open(path, encoding="utf-8") as f:
                src = f.read()
            if "PORT_ROM_REGION_CN" in src:
                raise SystemExit(f"{path} already has a CN case; strip it first")
            body = [f"{indent}{name} = {typ}Port_ResolveRomData(0x{got + 0x08000000:08x}u);{suffix}"
                    for indent, name, typ, got, suffix in rows]
            case = ("        case PORT_ROM_REGION_CN: {\n"
                    + "\n".join(body)
                    + "\n            break;\n        }\n")
            src = src.replace("        default:", case + "        default:", 1)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(src)

        with open(UNRESOLVED_LIST, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Symbols the CN port has no trustworthy address for.\n"
                    "# Written by tools/gen_cn_region_case.py; read by\n"
                    "# tools/check_cn_offsets.py. They are left out of the CN case on\n"
                    "# purpose, so they stay null instead of reading the wrong bytes.\n")
            for path, name in unresolved:
                f.write(f"{name}  {os.path.basename(path)}\n")

    total = sum(totals.values())
    print(f"symbols {total}: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items())))
    bad = totals.get("no-shape", 0) + totals.get("unresolved", 0)
    if total:
        print(f"placed {100.0 * (total - bad) / total:.1f}%")
    if unresolved:
        print(f"left null: {len(unresolved)} -> {UNRESOLVED_LIST}")
        for path, name in unresolved[:8]:
            print(f"  {os.path.basename(path)}: {name}")
    if not args.apply:
        print("dry run; pass --apply to write")


if __name__ == "__main__":
    sys.exit(main())
