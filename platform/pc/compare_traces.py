#!/usr/bin/env python3
"""Compare two mzm-linux traces (--trace) and report the first divergence.

A trace line has three groups separated by '|', and a fourth when the
bottom-screen UI runs:

    values         frame keys mode sub area room x y energy fc16
    neutral hashes fb vram bgpal objpal oam
    host hashes    ewram iwram io
    bottom         bottom        (only when the bottom-screen UI runs)

The first two groups and the bottom screen's hash mean the same on any host
byte order. The host hashes cover raw memory whose layout follows the host,
so they only compare builds of the same byte order: --cross-endian ignores
them (e.g. x86 vs PowerPC).

Exit status: 0 identical, 1 diverged, 2 bad input.
"""
import argparse
import sys

GROUPS = [
    ("values", ["frame", "keys", "mode", "sub", "area", "room", "x", "y", "energy", "fc16"]),
    ("neutral hashes", ["fb", "vram", "bgpal", "objpal", "oam", "snd"]),
    ("host hashes", ["ewram", "iwram", "io"]),
    ("bottom", ["bottom"]),
]
HOST = 2  # index of the host-hash group


def read_trace(path):
    rows = []
    with open(path, encoding="ascii") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.split() for p in line.split("|")]
            if len(parts) == len(GROUPS) - 1:
                parts.append([])  # no bottom screen in this run
            if len(parts) != len(GROUPS) or any(len(p) not in (len(g[1]), 0 if g[0] == "bottom" else -1)
                                                for p, g in zip(parts, GROUPS)):
                # A run that crashed leaves its last line half written.
                rest = f.read().strip()
                if rest:
                    sys.exit(f"{path}:{lineno}: not a trace line")
                print(f"{path}: ignoring the incomplete last line {lineno}")
                break
            rows.append(parts)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("expected")
    ap.add_argument("actual")
    ap.add_argument("--cross-endian", action="store_true",
                    help="ignore the host-byte-order hashes (ewram, iwram, io)")
    ap.add_argument("--context", type=int, default=3, help="lines shown before the divergence")
    args = ap.parse_args()

    a = read_trace(args.expected)
    b = read_trace(args.actual)
    groups = [g for g in range(len(GROUPS)) if not (args.cross_endian and g == HOST)]

    for i, (ra, rb) in enumerate(zip(a, b)):
        diffs = []
        for g in groups:
            if len(ra[g]) != len(rb[g]):
                diffs.append(f"{GROUPS[g][0]}: present in only one trace")
                continue
            for name, va, vb in zip(GROUPS[g][1], ra[g], rb[g]):
                if va != vb:
                    diffs.append(f"{name}: {va} -> {vb}")
        if diffs:
            print(f"First divergence at frame {ra[0][0]} (line {i + 1}):")
            for d in diffs:
                print(f"  {d}")
            print("Context (expected):")
            for r in a[max(0, i - args.context):i + 1]:
                print("  " + " | ".join(" ".join(r[g]) for g in groups if r[g]))
            return 1

    if len(a) != len(b):
        print(f"Identical for {min(len(a), len(b))} frames, then one trace ends "
              f"({len(a)} vs {len(b)} frames).")
        return 1
    print(f"Identical: {len(a)} frames" + (" (host hashes ignored)" if args.cross_endian else "") + ".")
    return 0


if __name__ == "__main__":
    sys.exit(main())
