#!/usr/bin/env python3
"""Add the iQue (CN) case to the generated per-symbol ROM offset tables.

gen_port_rom_offsets.py builds port/generated/*_rom.c from each region's .map
file. There is no mzm_cn.map -- the iQue release is a separate localisation,
so it cannot be produced by the matching build. Its symbol addresses are
recovered here instead, from the addresses the US build already knows:

1. Search the US bytes at that address inside the CN ROM. Most data is
   byte-identical and the layout shifts in whole segments, so this is exact.
2. If that fails, the symbol is probably a pointer table: its contents hold
   absolute addresses, which differ between the two builds even though the
   table itself sits in the same place in the resource stream. Take the
   nearest database.json resource and carry its shift across. Nothing needs
   patching afterwards -- a pointer table in the loaded ROM already contains
   iQue addresses, because it *is* the iQue ROM.

Anything still unresolved is left out of the CN case, so the symbol stays
null and the failure is loud rather than silently reading the wrong bytes.

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


def resolve(us, cn, items, starts, off):
    """Return (cn_offset, method) or (None, reason)."""
    probe = us[off:off + PROBE]
    for cand in nearest_candidates(items, starts, off)[:2]:
        if cand < 0 or cand + PROBE > len(cn):
            continue
        if cn[cand:cand + PROBE] == probe:
            return cand, "match"
        for d in range(-TWEAK, TWEAK + 1):
            p = cand + d
            if p >= 0 and p + PROBE <= len(cn) and cn[p:p + PROBE] == probe:
                return p, "match+tweak"
    # Pointer table (or other address-bearing data): trust the resource shift.
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

    totals = {"match": 0, "match+tweak": 0, "shift": 0, "unresolved": 0}
    per_file_unresolved = {}
    for path in files:
        rows = []
        for indent, name, typ, addr, suffix in iter_symbols(path):
            off = int(addr, 16) - 0x08000000
            got, how = resolve(us, cn, items, starts, off)
            totals[how] += 1
            if got is None:
                per_file_unresolved.setdefault(path, []).append(name)
                continue
            rows.append((indent, name, typ, got, suffix))
        if not args.apply or not rows:
            continue
        src = open(path, encoding="utf-8").read()
        if "PORT_ROM_REGION_CN" in src:
            continue
        body = [f"{indent}{name} = {typ}Port_ResolveRomData(0x{got + 0x08000000:08x}u);{suffix}"
                for indent, name, typ, got, suffix in rows]
        case = ("        case PORT_ROM_REGION_CN: {\n"
                + "\n".join(body)
                + "\n            break;\n        }\n")
        src = src.replace("        default:", case + "        default:", 1)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(src)

    total = sum(totals.values())
    print(f"symbols {total}: " + ", ".join(f"{k} {v}" for k, v in totals.items()))
    if total:
        print(f"resolved {100.0 * (total - totals['unresolved']) / total:.1f}%")
    if per_file_unresolved:
        print(f"files with unresolved symbols: {len(per_file_unresolved)}")
        for p, names in sorted(per_file_unresolved.items(), key=lambda kv: -len(kv[1]))[:5]:
            print(f"  {os.path.basename(p)}: {len(names)} (e.g. {names[0]})")
    if not args.apply:
        print("dry run; pass --apply to write")


if __name__ == "__main__":
    sys.exit(main())
