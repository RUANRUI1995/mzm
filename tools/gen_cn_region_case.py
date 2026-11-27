#!/usr/bin/env python3
"""Add the iQue (CN) case to the generated per-symbol ROM offset tables.

gen_port_rom_offsets.py builds port/generated/*_rom.c from each region's .map
file. There is no mzm_cn.map -- the iQue release is a separate localisation,
so it cannot be produced by the matching build. Its symbol addresses are
recovered here instead, from the addresses the US build already knows.

The recovery runs in passes, most reliable first:

1. Search the US bytes at that address inside the CN ROM. Most data is byte
   identical and the layout shifts in whole segments, so this is exact.
   The candidates come from the nearest database.json resource *and* from the
   shift of the nearest byte-matched symbols: a block can sit far from any
   resource and still be bracketed by two symbols that already matched.

2. Pointer tables the localisation rewrote. The table itself keeps its shape
   (same length, same aliasing pattern) but its entries hold iQue addresses,
   so its bytes never match. Once the table's own address is known, its
   entries are the ROM's own statement of where each target now lives -- for
   text, which the localisation re-flowed entry by entry, this is the only
   reliable source. Those targets feed back in as anchors for everything
   nearby, which is why the passes below run until nothing new is learned.

3. A table the localisation moved wholesale rather than shifted. The text
   pointer tables were relocated to the tail of the ROM while the text they
   point at stayed put, so the table has one shift and its targets another.
   Those segments are listed in MOVED, with the evidence for each.

4. Everything else falls back to the shift of the nearest anchor or database
   resource, which is how the non-pointer data was placed all along.

A pointer table that no shift explains is left out of the CN case, so the
symbol stays null and the failure is loud rather than silently reading the
wrong bytes. Those symbols are written to tools/cn_unresolved.list.

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

# A table's entries are only trusted as ground truth when the two tables alias
# the same way. The text tables match exactly; the allowance exists for tables
# whose data was deduplicated slightly differently.
SHAPE_MAX_DIFF = 0.02
# ...and when this share of entries points at a symbol we know by name, which
# is what keeps an ordinary block of data from being mistaken for a table.
TABLE_MIN_COVER = 0.8
TABLE_MAX_WORDS = 8192

# .list, not .txt: tools/*.txt is ignored as build output.
UNRESOLVED_LIST = os.path.join("tools", "cn_unresolved.list")

# Segments the localisation moved instead of shifting, as (us_start, us_end,
# shift, why). `shift` is cn - us for the table itself.
#
# The text pointer tables: in the US build they sit at 0x760420..0x760AA0,
# right behind the location names they index. In the iQue build the same runs
# of pointers appear at 0x79F620..0x79FCA0 -- the same lengths (14, 202, 14,
# 186, 1, 2, 2 words), in the same order, aliasing exactly the same entries --
# but the text they point at moved down by a different amount. So the tables
# were re-emitted at the end of the ROM rather than carried along with their
# data.
MOVED = [
    (0x760420, 0x760AA0, 0x3F200, "text pointer tables moved to the tail of the ROM"),
]

# Pointer tables the localisation moved to the tail of the ROM, as
# (us_start, cn_start, why). Read entry by entry: entry i of the iQue table is
# where the target of entry i lives now, which is the only exact source for
# text the localisation re-flowed string by string.
#
# 0x760458 holds 407 words in both builds, both runs ending on the same
# non-pointer word (0x111100) followed by identical bytes, and the two runs
# alias exactly the same entries -- 388 of which are symbols the US build
# knows by name. The tables just before it (0x760420..0x760458) are gfx
# pointers and move with it, which is what MOVED covers.
TEXT_TABLES = [
    (0x760458, 0x79F658, "text pointer tables"),
]

LINE = re.compile(r'^(\s*)(\w+) = (.*?)Port_ResolveRomData\((0x[0-9a-f]+)u\);(.*)$')


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


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


def table_run(buf, off, limit=TABLE_MAX_WORDS):
    """Number of consecutive words at `off` that point into `buf`."""
    base = 0x08000000
    end = base + len(buf)
    n = 0
    while n < limit:
        if off + 4 * (n + 1) > len(buf):
            break
        v = u32(buf, off + 4 * n)
        if not (base <= v < end):
            break
        n += 1
    return n


def aliasing(vals):
    """Canonical equality class per position: index of its first occurrence."""
    first = {}
    return [first.setdefault(v, i) for i, v in enumerate(vals)]


def alias_diff(us_vals, cn_vals):
    """Fraction of positions where the two tables disagree about aliasing."""
    a = aliasing(us_vals)
    b = aliasing(cn_vals)
    bad_a = sum(1 for i, (x, y) in enumerate(zip(a, b)) if (x == i) != (y == i))
    # Compare class membership, not just "is this the first occurrence".
    same = 0
    n = len(us_vals)
    group = {}
    for i in range(n):
        group.setdefault(a[i], []).append(i)
    for members in group.values():
        if len(members) < 2:
            continue
        for i in members:
            for j in members:
                if i < j:
                    same += 1 if cn_vals[i] == cn_vals[j] else 0
    pairs = 0
    for members in group.values():
        k = len(members)
        if k > 1:
            pairs += k * (k - 1) // 2
    # Positions that alias in US but not in CN count against, plus vice versa.
    if pairs == 0:
        return 0.0 if bad_a == 0 else 1.0
    return 1.0 - same / pairs


def byte_match(us, cn, items, starts, off, anchors=None, anchor_offs=None):
    """Exact copy of the US bytes, near any address we have reason to expect."""
    probe = us[off:off + PROBE]
    if len(probe) < PROBE:
        return None, None
    cands = list(nearest_candidates(items, starts, off)[:2])
    if anchors:
        cands += [off + s for s in ladder(anchors, anchor_offs, off)]
    seen = set()
    for cand in cands:
        if cand in seen:
            continue
        seen.add(cand)
        if cand < 0 or cand + PROBE > len(cn):
            continue
        if cn[cand:cand + PROBE] == probe:
            return cand, "match"
        for d in range(-TWEAK, TWEAK + 1):
            p = cand + d
            if p >= 0 and p + PROBE <= len(cn) and cn[p:p + PROBE] == probe:
                return p, "match+tweak"
    return None, None


def unique_match(us, cn, off, probe_len=32):
    """The one place in the CN ROM where this block appears, if it is one.

    Last resort for data no resource and no anchor brackets -- the minimaps,
    which the localisation re-packed as a group. A block that appears more than
    once is left alone: there would be no way to tell the copies apart.
    """
    probe = us[off:off + probe_len]
    if len(probe) < PROBE:
        return None
    first = cn.find(probe)
    if first == -1:
        return None
    if cn.find(probe, first + 1) != -1:
        return None
    return first


def resolve(us, cn, items, starts, anchors, anchor_offs, off):
    """Return (cn_offset, method); cn_offset is None if it cannot be trusted."""
    got, how = byte_match(us, cn, items, starts, off, anchors, anchor_offs)
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


def propagate(us, cn, us_off, cn_off, sym_by_us, out):
    """Read a pointer table's entries as ground truth for its targets.

    Returns the number of targets learned, 0 if this is not a table we can
    trust. `out` maps us_off -> cn_off.

    The run length has to match in both builds: a run that keeps going in one
    and stops in the other is a block that merely looks like a table, and its
    entries are not the same thing on both sides.
    """
    n = table_run(us, us_off)
    if n < SHAPE_MIN_PTRS:
        return 0
    if table_run(cn, cn_off, n + 1) != n:
        return 0
    us_vals = struct.unpack_from("<%dI" % n, us, us_off)
    cn_vals = struct.unpack_from("<%dI" % n, cn, cn_off)
    named = [i for i, v in enumerate(us_vals) if (v - 0x08000000) in sym_by_us]
    if len(named) < TABLE_MIN_COVER * n:
        return 0
    if alias_diff(us_vals, cn_vals) > SHAPE_MAX_DIFF:
        return 0
    got = 0
    for i in named:
        out[us_vals[i] - 0x08000000] = cn_vals[i] - 0x08000000
        got += 1
    return got


def lz77_stream(buf, off, decode=False):
    """(uncompressed length, bytes consumed) if a GBA LZ77 stream starts here.

    The localisation re-drew every menu that carries words, so those graphics
    share no bytes with the US build. What survives is the stream's own header:
    byte 0 is 0x10 and bytes 1..3 hold the uncompressed size, which the
    re-draw did not change (the picture is the same size, only its pixels
    differ). Decoding the stream is what makes the header trustworthy; a
    random 0x10 byte almost never runs to exactly the declared length.
    """
    if off + 4 > len(buf) or buf[off] != 0x10:
        return None
    n = buf[off + 1] | (buf[off + 2] << 8) | (buf[off + 3] << 16)
    if n < 0x20 or n > 0x40000:
        return None
    p = off + 4
    out = bytearray() if decode else None
    done = 0
    while done < n:
        if p >= len(buf):
            return None
        flags = buf[p]
        p += 1
        for i in range(7, -1, -1):
            if done >= n:
                break
            if (flags >> i) & 1:
                if p + 1 >= len(buf):
                    return None
                b0, b1 = buf[p], buf[p + 1]
                p += 2
                length = (b0 >> 4) + 3
                dist = (((b0 & 0xF) << 8) | b1) + 1
                if dist > done:
                    return None
                if decode:
                    for _ in range(length):
                        out.append(out[-dist])
                        if len(out) >= n:
                            break
                done += length
            else:
                if decode:
                    out.append(buf[p])
                p += 1
                done += 1
    return (bytes(out) if decode else n), p - off


def lz77_index(cn, lengths):
    """Every valid stream in the CN ROM whose output length is in `lengths`."""
    offs = []
    sizes = {}
    pos = cn.find(b"\x10")
    while pos != -1:
        if pos + 3 < len(cn):
            n = cn[pos + 1] | (cn[pos + 2] << 8) | (cn[pos + 3] << 16)
            if n in lengths:
                got = lz77_stream(cn, pos)
                if got is not None:
                    offs.append(pos)
                    sizes[pos] = n
        pos = cn.find(b"\x10", pos + 1)
    return offs, sizes


def place_lz77_chains(us, cn, symbols, placements, index, sizes, min_run=3):
    """Recover the CN address of every compressed block from its neighbours.

    The blocks sit back to back in the ROM, in the same order as in the US
    build. A file's blocks therefore form one run whose sequence of output
    sizes is a fingerprint: 47 pause-screen blocks share only seven distinct
    sizes, yet the sequence occurs exactly once in 8 MiB.

    Only a unique match is taken. Two or more means the run is not distinctive
    enough to override a byte match, and nothing is changed.
    """
    by_file = {}
    for path, _i, name, _t, off, _s in symbols:
        got = lz77_stream(us, off)
        if got is not None:
            by_file.setdefault(path, []).append((off, got[0], name))

    won = 0
    for path, rows in by_file.items():
        rows.sort()
        if len(rows) < min_run:
            continue
        lens = [r[1] for r in rows]
        hits = [i for i in range(len(index) - len(lens) + 1)
                if all(sizes[index[i + k]] == lens[k] for k in range(len(lens)))]
        if len(hits) != 1:
            continue
        base = hits[0]
        for k, (off, _n, _name) in enumerate(rows):
            if placements.get(off, (None, None))[1] == "table":
                continue
            placements[off] = (index[base + k], "lz77")
            won += 1
    return won


def place_all(us, cn, items, starts, symbols, rounds=4):
    """Decide every symbol's CN offset, letting exact matches feed back."""
    sym_by_us = dict((off, name) for _p, _i, name, _t, off, _s in symbols)
    placements = {}
    verified = {}
    anchors = []
    anchor_offs = []
    rank = {"match": 0, "match+tweak": 0, "table": 1, "moved": 2,
            "shift+shape": 3, "resource+shape": 3, "shift": 4,
            "no-shape": 5, "unresolved": 5}

    for _round in range(rounds):
        learned = 0
        # Byte-exact copies, bracketed by whatever we know so far.
        for off in sym_by_us:
            got, how = byte_match(us, cn, items, starts, off, anchors, anchor_offs)
            if got is not None:
                prev = placements.get(off)
                if prev is None or rank[how] < rank[prev[1]]:
                    placements[off] = (got, how)
                    learned += 1
        anchors = sorted((o, p[0] - o) for o, p in placements.items()
                         if p[1] in ("match", "match+tweak"))
        anchor_offs = [a for a, _ in anchors]
        if not learned:
            break

    # The tables the localisation re-emitted at the tail of the ROM are the
    # one case a byte match cannot settle and whose targets changed on purpose:
    # text was re-flowed string by string, so the table is the ROM's own
    # statement of where each one landed. Read those first, then everything
    # else in the usual order.
    for us_start, cn_start, _why in TEXT_TABLES:
        n = propagate(us, cn, us_start, cn_start, sym_by_us, verified)
        if not n:
            print(f"warning: {_why} at 0x{us_start:06x} did not read as a table",
                  file=sys.stderr)

    # Verified targets win over byte matches: the localisation kept dead copies
    # of the original Japanese text, so a byte match there is a real copy of
    # the wrong string.
    for off, cn_off in verified.items():
        placements[off] = (cn_off, "table")

    for off in sym_by_us:
        if off in placements:
            continue
        placements[off] = resolve(us, cn, items, starts, anchors, anchor_offs, off)

    # Anything still placed on a guess gets one more chance: if the block
    # appears exactly once in the CN ROM, that occurrence is not a guess.
    for off, (got, how) in list(placements.items()):
        if rank[how] < rank["shift"]:
            continue
        hit = unique_match(us, cn, off)
        if hit is not None:
            placements[off] = (hit, "unique")

    # Compressed graphics last, because they are the one family no byte match
    # can help with: the localisation re-drew every menu that carries words.
    lengths = set()
    for _p, _i, _n, _t, off, _s in symbols:
        got = lz77_stream(us, off)
        if got is not None:
            lengths.add(got[0])
    if lengths:
        index, sizes = lz77_index(cn, lengths)
        print(f"lz77 streams in the CN ROM: {len(index)}")
        n = place_lz77_chains(us, cn, symbols, placements, index, sizes)
        print(f"blocks located by size chain: {n}")

    return placements, verified


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

    symbols = []
    for path in files:
        for indent, name, typ, addr, suffix in iter_symbols(path):
            symbols.append((path, indent, name, typ, int(addr, 16) - 0x08000000, suffix))
    print(f"symbols {len(symbols)}")

    placements, verified = place_all(us, cn, items, starts, symbols)
    print(f"table-verified targets {len(verified)}")

    unresolved = []
    per_file = {}
    totals = {}
    for path, indent, name, typ, off, suffix in symbols:
        got, how = placements.get(off, (None, "unresolved"))
        per_file.setdefault(path, []).append((indent, name, typ, got, suffix))
        totals[how] = totals.get(how, 0) + 1
        if got is None:
            unresolved.append((path, name))

    print(f"symbols {len(symbols)}: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items())))

    bad = totals.get("no-shape", 0) + totals.get("unresolved", 0)
    placed_n = sum(totals.values())
    if placed_n:
        print(f"placed {100.0 * (placed_n - bad) / placed_n:.1f}%")
    if unresolved:
        print(f"left null: {len(unresolved)} -> {UNRESOLVED_LIST}")
        for path, name in unresolved[:8]:
            print(f"  {os.path.basename(path)}: {name}")
    if not args.apply:
        print("dry run; pass --apply to write")
        return 0

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
