#!/usr/bin/env python3
"""Builds port/port_sram_legacy_map.inc: how to move a save written by the 3DS port before
issue #37 (the "legacy" layout: modern GCC struct alignment) to the GBA's layout the port uses now
(port/port_gba_struct_layout.h).

It walks struct Sram (include/structs/save_file.h) and every struct inside it, compiles a probe
that reports each member's offset and size twice with devkitARM's GCC -- once with
PORT_LEGACY_SRAM_LAYOUT defined (the old layout) and once without (the current one) -- and writes
the byte runs that move, merged where they are contiguous in both, plus where the checksummed
blocks are. Run it from the repository root; the output is checked in, so it only needs running
again if the save structs change.

    python tools/gen_sram_legacy_map.py [--check]

--check compares with the checked-in file instead of writing it (exit 1 if it differs).
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

NL = chr(10)
OUT = os.path.join("port", "port_sram_legacy_map.inc")


def find_gcc():
    roots = [os.environ.get("DEVKITARM", ""), "/opt/devkitpro/devkitARM", "C:/devkitPro/devkitARM"]
    for r in roots:
        for name in ("arm-none-eabi-gcc", "arm-none-eabi-gcc.exe"):
            p = os.path.join(r, "bin", name)
            if r and os.path.exists(p):
                return p
    sys.exit("devkitARM's arm-none-eabi-gcc not found (set DEVKITARM)")


def strip_comments(text):
    text = re.sub(r"/[*].*?[*]/", "", text, flags=re.S)
    return re.sub(r"//[^" + NL + "]*", "", text)


def parse_structs():
    found = {}
    for path in glob.glob("include/**/*.h", recursive=True):
        text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"[^A-Za-z0-9_]struct[ ]+([A-Za-z0-9_]+)[ ]*[{]", " " + text):
            name = m.group(1)
            if name in found:
                continue
            i = m.end() - 1
            depth = 1
            j = i
            while depth:
                c = text[j]
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                j += 1
            found[name] = text[i:j - 1]
    return found


def members_of(body):
    out = []
    lines = [l for l in body.split(NL) if not l.strip().startswith("#")]
    for decl in NL.join(lines).split(";"):
        decl = " ".join(decl.split())
        if not decl:
            continue
        if ":" in decl:
            sys.exit("bitfield in a save struct: the generator cannot map it: " + decl)
        # Array dimensions as <...>: square brackets in a character class read as a nested set.
        m = re.match(r"(.*?)([A-Za-z0-9_]+)[ ]*((?:<[^>]*>)*)$", decl.replace("[", "<").replace("]", ">"))
        if not m:
            sys.exit("cannot parse member: " + decl)
        typ = m.group(1).strip()
        sm = re.match(r"struct[ ]+([A-Za-z0-9_]+)", typ)
        out.append((m.group(2), sm.group(1) if sm else None))
    return out


def walk(structs):
    order, todo, members = [], ["Sram"], {}
    while todo:
        s = todo.pop(0)
        if s in members:
            continue
        if s not in structs:
            sys.exit("struct not found: " + s)
        members[s] = members_of(structs[s])
        order.append(s)
        todo += [t for _, t in members[s] if t]
    return order, members


def probe(gcc, order, members, legacy):
    keys, lines = [], ['#include "types.h"', '#include "structs/save_file.h"', "#include <stddef.h>",
                       "const unsigned probe_vals[] = {"]
    for s in order:
        keys.append((s, None, "size"))
        lines.append("sizeof(struct %s)," % s)
        for name, _ in members[s]:
            keys.append((s, name, "off"))
            lines.append("offsetof(struct %s, %s)," % (s, name))
            keys.append((s, name, "size"))
            lines.append("sizeof(((struct %s*)0)->%s)," % (s, name))
    lines.append("};")
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "probe.c")
        asm = os.path.join(tmp, "probe.s")
        open(src, "w").write(NL.join(lines) + NL)
        flags = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft", "-D__3DS__", "-DMZM_3DS",
                 "-include", "string.h", "-Iport/generated/shadow", "-I.", "-Iinclude", "-Iport",
                 "-Iport/generated", "-Iport/ppu/include", "-Iplatform/3ds/source", "-w", "-O0", "-S"]
        if legacy:
            flags.append("-DPORT_LEGACY_SRAM_LAYOUT")
        subprocess.run([gcc] + flags + ["-o", asm, src], check=True)
        vals = [int(l.split()[1], 0) for l in open(asm) if l.strip().startswith(".word")]
    if len(vals) != len(keys):
        sys.exit("probe returned %d values for %d keys" % (len(vals), len(keys)))
    return dict(zip(keys, vals))


def expand(struct, members, lo, ln, old_base, new_base, runs):
    for name, sub in members[struct]:
        o = old_base + lo[(struct, name, "off")]
        n = new_base + ln[(struct, name, "off")]
        if sub:
            count_o = lo[(struct, name, "size")] // lo[(sub, None, "size")]
            count_n = ln[(struct, name, "size")] // ln[(sub, None, "size")]
            assert count_o == count_n, (struct, name)
            for i in range(count_o):
                expand(sub, members, lo, ln, o + i * lo[(sub, None, "size")], n + i * ln[(sub, None, "size")], runs)
        else:
            size = lo[(struct, name, "size")]
            assert size == ln[(struct, name, "size")], (struct, name)
            runs.append([o, n, size])


def main():
    gcc = find_gcc()
    order, members = walk(parse_structs())
    lo = probe(gcc, order, members, True)
    ln = probe(gcc, order, members, False)
    runs = []
    expand("Sram", members, lo, ln, 0, 0, runs)
    merged = []
    for o, n, size in runs:
        if merged and merged[-1][0] + merged[-1][2] == o and merged[-1][1] + merged[-1][2] == n:
            merged[-1][2] += size
        else:
            merged.append([o, n, size])

    def block(arr, sub, count):
        rows = []
        for i in range(count):
            rows.append((lo[("Sram", arr, "off")] + i * lo[(sub, None, "size")],
                         ln[("Sram", arr, "off")] + i * ln[(sub, None, "size")]))
        return rows

    for s, f in (("SaveFile", "checksum"), ("SaveFile", "notChecksum"), ("SaveTimeAttack", "checksum"),
                 ("SaveTimeAttack", "notChecksum")):
        assert lo[(s, f, "off")] == ln[(s, f, "off")], (s, f)
    files = block("files", "SaveFile", 3) + block("filesCopy", "SaveFile", 3)
    attacks = block("timeAttackSaves", "SaveTimeAttack", 2)

    out = ["/* Generated by tools/gen_sram_legacy_map.py -- do not edit. The save layout the 3DS port",
           " * wrote before issue #37 (legacy) and the GBA's, which it uses now. */",
           "#define LEGACY_SRAM_SIZE %du" % lo[("Sram", None, "size")],
           "#define LEGACY_SAVE_FILE_SIZE %du" % lo[("SaveFile", None, "size")],
           "#define LEGACY_SAVE_TIME_ATTACK_SIZE %du" % lo[("SaveTimeAttack", None, "size")],
           "#define SAVE_FILE_CHECKSUM_OFFSET %du" % ln[("SaveFile", "checksum", "off")],
           "#define SAVE_TIME_ATTACK_CHECKSUM_OFFSET %du" % ln[("SaveTimeAttack", "checksum", "off")],
           "",
           "/* { legacy offset, current offset, bytes }: every byte of struct Sram that is not padding. */",
           "static const struct { u16 legacy, current, size; } sLegacySramRuns[] = {"]
    out += ["    { 0x%04X, 0x%04X, %d }," % tuple(r) for r in merged]
    out += ["};", "", "/* { legacy offset, current offset } of files[0..2], then filesCopy[0..2]. */",
            "static const struct { u16 legacy, current; } sLegacySaveFiles[] = {"]
    out += ["    { 0x%04X, 0x%04X }," % r for r in files]
    out += ["};", "", "/* { legacy offset, current offset } of timeAttackSaves[0..1]. */",
            "static const struct { u16 legacy, current; } sLegacyTimeAttacks[] = {"]
    out += ["    { 0x%04X, 0x%04X }," % r for r in attacks]
    out += ["};", ""]
    text = NL.join(out)

    if "--check" in sys.argv:
        current = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if current != text:
            sys.exit(OUT + " is out of date: run tools/gen_sram_legacy_map.py")
        print(OUT + " is up to date")
        return
    with open(OUT, "w", encoding="utf-8", newline=NL) as f:
        f.write(text)
    print("wrote %s: %d runs, legacy Sram %d bytes, current %d" % (OUT, len(merged), lo[("Sram", None, "size")],
                                                                 ln[("Sram", None, "size")]))


if __name__ == "__main__":
    main()
