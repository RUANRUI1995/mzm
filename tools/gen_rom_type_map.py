#!/usr/bin/env python3
"""Generate the ROM type map: where every typed data object of the ROM is, and
which of its bytes form 2- and 4-byte scalars. A big-endian port (the Wii U,
issue #39) swaps those scalars once, when the ROM is loaded, into a copy that C
code reads natively (docs/wiiu-big-endian-plan.md, part B and C).

How:
  - Every src/data/*.c the ROM is built from (the files the .map lists with a
    .rodata section) is preprocessed like the decomp's Makefile does
    (tools/preproc.py, charmap.txt, the region's define) and compiled with
    devkitARM's gcc -g, only to read the types back from DWARF (readelf -wi).
  - The objects of each file are laid out in the order gcc emits them, each at
    its type's natural alignment, starting from the file's .rodata address in
    the .map; every global symbol re-anchors at its real address from the .map.
    (gcc over-aligns some arrays; agbcc, which built the ROM, does not; agbcc
    rounds structs to 4 bytes, which -mstructure-size-boundary=32 copies.)
    Where the layout reaches a global at the wrong address, the statics placed
    since the previous global are reported and left out.
  - Objects made only of bytes (graphics, compressed data, text) need nothing
    and are left out.

Output: a C file with three tables (types, scalar ops, objects) for one region.

    python3 tools/gen_rom_type_map.py --region eu --map mzm_eu.map \\
        --out port/generated/byteswap/rom_type_map_eu.c

Needs devkitARM (DEVKITARM or C:/devkitPro/devkitARM, /opt/devkitpro/devkitARM)
and mzm_<region>_baserom.gba: the region's assets are extracted into a
temporary directory (database.json), whatever include/extracted holds.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import OrderedDict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ROM_BASE = 0x08000000


def devkitarm_bin(tool):
    for base in (os.environ.get("DEVKITARM"), "C:/devkitPro/devkitARM", "/opt/devkitpro/devkitARM"):
        if base:
            path = os.path.join(base, "bin", "arm-none-eabi-" + tool)
            for candidate in (path, path + ".exe"):
                if os.path.exists(candidate):
                    return candidate
    found = shutil.which("arm-none-eabi-" + tool)
    if not found:
        sys.exit("devkitARM's arm-none-eabi-%s not found (set DEVKITARM)" % tool)
    return found


# ---- The .map ---------------------------------------------------------------------

SECTION_RE = re.compile(r"^\s*(\.rodata|\.data)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(src/data/\S+\.o)\s*$")
SYMBOL_RE = re.compile(r"^\s+0x([0-9a-f]+)\s+([A-Za-z_]\w*)\s*$")


def read_map(path):
    """{object file: {section: (address, size, {global symbol: address})}} for the
    .rodata and .data of src/data objects that the ROM holds (.data too: the
    non-const tables of these files are ROM data as well)."""
    files = OrderedDict()
    current = None
    with open(path) as f:
        for line in f:
            m = SECTION_RE.match(line)
            if m:
                section, address, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
                current = None
                if size and address >= ROM_BASE:
                    files.setdefault(obj, OrderedDict())[section] = (address, size, {})
                    current = (obj, section)
                continue
            m = SYMBOL_RE.match(line)
            if m and current:
                files[current[0]][current[1]][2][m.group(2)] = int(m.group(1), 16)
                continue
            if line.strip() and not line.startswith("                "):
                current = None
    return files


# ---- DWARF, from readelf -wi --------------------------------------------------------

DIE_RE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: (\d+)(?: \((\w+)\))?")
ATTR_RE = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")


class Die:
    __slots__ = ("offset", "tag", "attrs", "children", "parent")

    def __init__(self, offset, tag, parent):
        self.offset, self.tag, self.attrs, self.children, self.parent = offset, tag, {}, [], parent


def parse_dwarf(text):
    dies = {}
    stack = []
    roots = []
    current = None
    for line in text.splitlines():
        m = DIE_RE.match(line)
        if m:
            depth, offset, abbrev, tag = int(m.group(1)), int(m.group(2), 16), int(m.group(3)), m.group(4)
            if abbrev == 0:
                current = None
                continue
            del stack[depth:]
            parent = stack[-1] if stack else None
            die = Die(offset, tag, parent)
            dies[offset] = die
            (parent.children if parent else roots).append(die)
            stack.append(die)
            current = die
            continue
        m = ATTR_RE.match(line)
        if m and current is not None:
            current.attrs[m.group(1)] = m.group(2).strip()
    return dies


def attr_name(die):
    value = die.attrs.get("DW_AT_name")
    if value is None:
        return None
    return value.split("): ")[-1] if value.startswith("(indirect string") else value


def attr_ref(die, name):
    value = die.attrs.get(name)
    if not value:
        return None
    m = re.search(r"<0x([0-9a-f]+)>", value)
    return int(m.group(1), 16) if m else None


def attr_int(die, name):
    value = die.attrs.get(name)
    if value is None:
        return None
    m = re.match(r"(-?0x[0-9a-f]+|-?\d+)", value)
    if m:
        return int(m.group(1), 0)
    m = re.search(r"DW_OP_plus_uconst: (\d+)", value)
    return int(m.group(1)) if m else None


def bitfield_first_bit(member):
    """A little-endian bitfield's first bit from the start of the struct, from
    DWARF 5's DW_AT_data_bit_offset or DWARF 4's DW_AT_bit_offset (counted from
    the most significant bit of a DW_AT_byte_size unit at the member location)."""
    data_bit_offset = attr_int(member, "DW_AT_data_bit_offset")
    if data_bit_offset is not None:
        return data_bit_offset
    bit_offset = attr_int(member, "DW_AT_bit_offset")
    unit = attr_int(member, "DW_AT_byte_size")
    bits = attr_int(member, "DW_AT_bit_size")
    if bit_offset is None or unit is None or bits is None:
        return None
    return (attr_int(member, "DW_AT_data_member_location") or 0) * 8 + unit * 8 - bit_offset - bits


class Types:
    """Layouts of DWARF types: size, natural alignment, and the scalar ops
    [(offset, width)] with width 2 or 4."""

    def __init__(self, dies):
        self.dies = dies
        self.cache = {}
        self.warnings = set()

    def strip(self, die):
        while die is not None and die.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type",
                                              "DW_TAG_restrict_type", "DW_TAG_atomic_type"):
            ref = attr_ref(die, "DW_AT_type")
            die = self.dies.get(ref) if ref is not None else None
        return die

    def layout(self, offset):
        """(size, align, ops) for the type DIE at offset."""
        if offset in self.cache:
            return self.cache[offset]
        die = self.strip(self.dies.get(offset))
        result = self._layout(die)
        self.cache[offset] = result
        return result

    def _layout(self, die):
        if die is None:
            return (0, 1, [])  # void
        tag = die.tag
        if tag in ("DW_TAG_base_type", "DW_TAG_enumeration_type"):
            size = attr_int(die, "DW_AT_byte_size") or 0
            return (size, size or 1, [(0, size)] if size in (2, 4) else [])
        if tag in ("DW_TAG_pointer_type", "DW_TAG_subroutine_type"):
            return (4, 4, [(0, 4)])
        if tag == "DW_TAG_array_type":
            elem_size, elem_align, elem_ops = self.layout(attr_ref(die, "DW_AT_type"))
            count = 1
            known = True
            for sub in die.children:
                if sub.tag != "DW_TAG_subrange_type":
                    continue
                n = attr_int(sub, "DW_AT_count")
                if n is None:
                    upper = attr_int(sub, "DW_AT_upper_bound")
                    n = upper + 1 if upper is not None else None
                if n is None:
                    known = False
                    n = 0
                count *= n
            ops = [(i * elem_size + o, w) for i in range(count) for (o, w) in elem_ops]
            return (elem_size * count if known else 0, elem_align, ops)
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            size = attr_int(die, "DW_AT_byte_size") or 0
            align = 1
            ops = {}
            members = [c for c in die.children if c.tag == "DW_TAG_member"]
            if tag == "DW_TAG_union_type" and members:
                # Swap as the largest member says; reported, as a union of
                # differently shaped scalars cannot be swapped right for all.
                shapes = set()
                for member in members:
                    shapes.add(tuple(self.layout(attr_ref(member, "DW_AT_type"))[2]))
                if len(shapes) > 1:
                    self.warnings.add("union %s: members disagree on scalar layout" % (attr_name(die) or "?"))
                members = [max(members, key=lambda m: self.layout(attr_ref(m, "DW_AT_type"))[0])]
            # Bitfields: the port declares them so that every byte holds the
            # GBA's bits on any host (include/structs, reversed per byte on
            # big-endian), so they are left alone, unless one crosses a byte
            # (union OamData), where the port keeps the unit's value instead
            # and the unit is swapped like a scalar.
            straddles = False
            for member in members:
                bits = attr_int(member, "DW_AT_bit_size")
                first = bitfield_first_bit(member)
                if bits is not None and first is not None and first // 8 != (first + bits - 1) // 8:
                    straddles = True
            for member in members:
                msize, malign, mops = self.layout(attr_ref(member, "DW_AT_type"))
                base = attr_int(member, "DW_AT_data_member_location") or 0
                align = max(align, malign)
                if "DW_AT_bit_size" in member.attrs:
                    if straddles and msize in (2, 4):
                        byte = (bitfield_first_bit(member) or 0) // 8
                        ops[byte - byte % msize] = msize
                    continue
                for (o, w) in mops:
                    ops[base + o] = w
            # agbcc aligns structs and unions to 4 bytes (see -mstructure-size-boundary);
            # a size that is not a multiple of 4 means PACKED, which keeps its own.
            if size % 4 == 0:
                align = max(align, 4)
            return (size, align, sorted(ops.items()))
        self.warnings.add("unhandled type tag %s" % tag)
        return (0, 1, [])


def variables(dies):
    """{name: type DIE offset} of the variables defined (not declared) in the unit."""
    result = {}
    for die in dies.values():
        if die.tag != "DW_TAG_variable" or die.parent is None or die.parent.tag != "DW_TAG_compile_unit":
            continue
        if "DW_AT_declaration" in die.attrs:
            continue
        spec = attr_ref(die, "DW_AT_specification")
        source = dies.get(spec) if spec is not None else die
        name = attr_name(die) or (attr_name(source) if source else None)
        type_ref = attr_ref(die, "DW_AT_type")
        if type_ref is None and source is not None:
            type_ref = attr_ref(source, "DW_AT_type")
        if name and type_ref is not None:
            result[name] = type_ref
    return result


# ---- Compiling one data file ----------------------------------------------------------

SYMTAB_RE = re.compile(r"^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+OBJECT\s+(LOCAL|GLOBAL)\s+\w+\s+(\S+)\s+(\S+)\s*$")


def compile_file(source, region_define, workdir, gcc, readelf):
    """(DWARF text, {section: [(name, offset, size, is_global)] in emission order})"""
    base = os.path.join(workdir, os.path.basename(source))
    # The sources hold UTF-8 (Japanese text); Python on Windows would read cp1252.
    env = dict(os.environ, PYTHONUTF8="1")
    pre = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "preproc.py"), source,
                          os.path.join(ROOT, "charmap.txt")], cwd=ROOT, capture_output=True,
                         encoding="utf-8", env=env)
    if pre.returncode != 0:
        raise RuntimeError("preproc failed: " + pre.stderr[-400:])
    code = pre.stdout
    for attempt in (0, 1):
        with open(base + ".c", "w", encoding="utf-8") as f:
            f.write(code)
        run = subprocess.run([gcc, "-x", "c", base + ".c", "-c", "-o", base + ".o", "-g", "-gdwarf-4", "-O0",
                              "-fno-common", "-fno-toplevel-reorder", "-nostdinc",
                              # agbcc (the ROM's compiler) rounds every struct up to 4 bytes.
                              "-mstructure-size-boundary=32",
                              "-I" + os.path.join(ROOT, "include"), "-I" + ROOT, "-D" + region_define, "-w"],
                             cwd=ROOT, capture_output=True, text=True)
        if run.returncode == 0:
            break
        if attempt == 0 and "static declaration of" in run.stderr:
            # agbcc accepted a static definition after an extern declaration;
            # gcc does not. Linkage does not change the types we are after.
            code = re.sub(r"^static\s+const\b", "const", code, flags=re.M)
            continue
        raise RuntimeError("gcc failed: " + run.stderr[-600:])
    dwarf = subprocess.run([readelf, "-wi", base + ".o"], capture_output=True, text=True).stdout
    sections = subprocess.run([readelf, "-S", "-W", base + ".o"], capture_output=True, text=True).stdout
    index_to_section = {}
    for line in sections.splitlines():
        m = re.match(r"^\s*\[\s*(\d+)\]\s+(\S+)", line)
        if m and m.group(2) in (".rodata", ".data"):
            index_to_section[m.group(1)] = m.group(2)
    symbols = {}
    symtab = subprocess.run([readelf, "-s", "-W", base + ".o"], capture_output=True, text=True).stdout
    for line in symtab.splitlines():
        m = SYMTAB_RE.match(line)
        if m and m.group(4) in index_to_section:
            symbols.setdefault(index_to_section[m.group(4)], []).append(
                (m.group(5), int(m.group(1), 16), int(m.group(2)), m.group(3) == "GLOBAL"))
    for entries in symbols.values():
        entries.sort(key=lambda s: s[1])
    return dwarf, symbols


# ---- Main ------------------------------------------------------------------------------

REGION_DEFINES = {"eu": "REGION_EU", "us": "REGION_US", "jp": "REGION_JP"}

# Typed ROM data the decomp defines in assembly (no src/data/*.c to compile),
# or declares as bytes but C code reads in wider units: (symbol, C definition
# of a stand-in with the type it is read as, named `probe`). Its address comes
# from the .map.
EXTRA_OBJECTS = [
    ("sSoundDataEntries", '#include "types.h"\n#include "structs/audio.h"\n#include "constants/audio.h"\n'
                          "const struct SoundEntry probe[SOUND_COUNT] = { 0 };\n"),
    # Declared u8; TextDraw*Character (src/text.c) reads it as u32 rows of 4bpp pixels.
    ("sCharactersGfx", "const unsigned int probe[176 * 1024 / 4] = { 0 };\n"),
]

ANY_SYMBOL_RE = re.compile(r"^\s+0x(08[0-9a-f]{6})\s+([A-Za-z_]\w*)\s*$")


def map_symbols(path):
    result = {}
    with open(path) as f:
        for line in f:
            m = ANY_SYMBOL_RE.match(line)
            if m:
                result[m.group(2)] = int(m.group(1), 16)
    return result


def extra_objects(region_define, workdir, gcc, readelf, map_path):
    """[(address, element size, element ops, count, name)] for EXTRA_OBJECTS."""
    addresses = map_symbols(map_path)
    result = []
    for (name, code) in EXTRA_OBJECTS:
        if name not in addresses:
            continue
        base = os.path.join(workdir, "extra_" + name)
        with open(base + ".c", "w", encoding="utf-8") as f:
            f.write(code)
        run = subprocess.run([gcc, "-x", "c", base + ".c", "-c", "-o", base + ".o", "-g", "-gdwarf-4", "-O0",
                              "-nostdinc", "-mstructure-size-boundary=32", "-I" + os.path.join(ROOT, "include"),
                              "-I" + ROOT, "-D" + region_define, "-w"], cwd=ROOT, capture_output=True, text=True)
        if run.returncode != 0:
            raise RuntimeError("extra object %s: %s" % (name, run.stderr[-400:]))
        dies = parse_dwarf(subprocess.run([readelf, "-wi", base + ".o"], capture_output=True, text=True).stdout)
        types = Types(dies)
        type_ref = variables(dies)["probe"]
        size, _align, ops = types.layout(type_ref)
        stripped = types.strip(dies.get(type_ref))
        element = (size, ops, 1)
        if stripped is not None and stripped.tag == "DW_TAG_array_type":
            esize, _ealign, eops = types.layout(attr_ref(stripped, "DW_AT_type"))
            if esize:
                element = (esize, eops, size // esize)
        result.append((addresses[name], element[0], element[1], element[2], name))
    return result


def extract_region_assets(region, workdir):
    """Write the region's extracted .inc files (tools/extractor.py's format)
    under workdir/extracted. Data files compile from workdir, so their
    `#include "extracted/..."` finds these before include/extracted, which may
    hold another region's assets (language graphics differ in size)."""
    import json
    with open(os.path.join(ROOT, "database.json")) as f:
        db = json.load(f)
    with open(os.path.join(ROOT, "mzm_%s_baserom.gba" % region), "rb") as f:
        rom = f.read()
    for entry in db:
        addr = entry["addr"].get(region)
        if addr is None:
            continue
        count = entry["count"]
        if isinstance(count, dict):
            count = count[region]
        unit = entry["size"]
        size = int(count, 16) * unit
        start = int(addr, 16)
        data = rom[start:start + size]
        path = os.path.join(workdir, "extracted", entry.get("dir", "data"), entry["path"] + ".inc")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(",".join(str(int.from_bytes(data[i:i + unit], "little")) + "u" for i in range(0, size, unit)))


GRAPHICS_RE = re.compile(r'\b(\w+)\s*\[[^\]]*\]\s*=\s*\{\s*#include\s+"[^"]*\.(?:gfx|lz)[^"]*"')


TILEMAP_RE = re.compile(r'\b(sTileset_\w+_Tilemap)\s*\[[^\]]*\]\s*=\s*\{\s*#include\s+"[^"]*\.tm\.inc"')


def tilemap_objects(source):
    """Names of the tilesets' tilemaps (sTileset_*_Tilemap, from extracted .tm
    files): u16 entries, though the decomp declares them as u8, which C code
    copies into u16 buffers (gTilemap) and reads natively. Other .tm objects
    (sDoorTransitionTilemap) are RLE byte streams and stay untyped."""
    with open(source, encoding="utf-8") as f:
        return set(TILEMAP_RE.findall(f.read()))


def graphics_objects(source):
    """Names of the objects of a data file filled from extracted graphics or
    compressed data (.gfx, .lz). Whatever their declared type (often u32),
    they are byte streams: copied by DMA and decompressed as bytes, so not
    swapped. C code that reads one in wider units lists it in EXTRA_OBJECTS."""
    with open(source, encoding="utf-8") as f:
        return set(GRAPHICS_RE.findall(f.read()))


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment if alignment > 1 else value


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--region", required=True, choices=sorted(REGION_DEFINES))
    ap.add_argument("--map", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", help="regex: just these object files (for testing)")
    ap.add_argument("--verbose", action="store_true", help="print where a layout disagrees with the map")
    args = ap.parse_args()

    gcc, readelf = devkitarm_bin("gcc"), devkitarm_bin("readelf")
    files = read_map(os.path.join(ROOT, args.map) if not os.path.isabs(args.map) else args.map)
    workdir = tempfile.mkdtemp(prefix="romtypes-")
    extract_region_assets(args.region, workdir)
    type_index = OrderedDict()   # ops tuple + size -> index
    objects = []                 # (address, count, type index, name)
    report = {"files": 0, "objects": 0, "byte_only": 0, "skipped_files": [], "warnings": set(),
              "unresolved": 0}

    try:
        for obj, map_sections in files.items():
            if args.only and not re.search(args.only, obj):
                continue
            source = os.path.join(ROOT, obj[:-2] + ".c")
            if not os.path.exists(source):
                report["warnings"].add("no source for %s" % obj)
                continue
            try:
                dwarf, section_symbols = compile_file(source, REGION_DEFINES[args.region], workdir, gcc, readelf)
                byte_streams = graphics_objects(source)
                tilemaps = tilemap_objects(source)
            except RuntimeError as e:
                report["skipped_files"].append("%s: %s" % (obj, str(e).splitlines()[0][:160]))
                continue
            report["files"] += 1
            dies = parse_dwarf(dwarf)
            types = Types(dies)
            var_types = variables(dies)
            for section, (start, total, globals_) in map_sections.items():
                symbols = section_symbols.get(section, [])
                pos = start
                consistent = True
                placed = []
                bad = set()        # statics in a stretch that ends at a misplaced global
                segment = 0        # index in placed where the current stretch starts
                for (name, _off, gcc_size, is_global) in symbols:
                    type_ref = var_types.get(name)
                    if type_ref is None:
                        report["unresolved"] += 1
                        continue
                    size, align, ops = types.layout(type_ref)
                    if size == 0:
                        size = gcc_size
                    # A top-level array is kept as (element, count), not expanded.
                    element = (size, ops, 1)
                    stripped = types.strip(dies.get(type_ref))
                    if stripped is not None and stripped.tag == "DW_TAG_array_type":
                        subranges = [c for c in stripped.children if c.tag == "DW_TAG_subrange_type"]
                        if len(subranges) == 1:
                            esize, _ealign, eops = types.layout(attr_ref(stripped, "DW_AT_type"))
                            if esize:
                                element = (esize, eops, size // esize)
                    if name in tilemaps:
                        element = (2, [(0, 2)], size // 2)
                    if name in globals_:
                        anchor = globals_[name]
                        if anchor != align_up(pos, align):
                            consistent = False
                            bad.update(range(segment, len(placed)))
                            if args.verbose:
                                print("  %s %s: %s at 0x%x in the map, 0x%x by layout (after %s)"
                                      % (obj, section, name, anchor, align_up(pos, align),
                                         placed[-1][0] if placed else "-"))
                        pos = anchor
                        segment = len(placed) + 1
                    else:
                        pos = align_up(pos, align)
                    placed.append((name, pos, element, name in globals_))
                    pos += size
                if pos > start + total:
                    consistent = False
                    bad.update(range(segment, len(placed)))
                if not consistent:
                    report["skipped_files"].append(
                        "%s %s: layout disagrees with the map before some globals; %d statics there left out"
                        % (obj, section, sum(1 for i in bad if not placed[i][3])))
                report["warnings"] |= {"%s: %s" % (obj, w) for w in types.warnings}
                for index, (name, address, (element_size, element_ops, count), anchored) in enumerate(placed):
                    if index in bad and not anchored:
                        continue
                    if not element_ops or count == 0 or name in byte_streams:
                        report["byte_only"] += 1
                        continue
                    key = (element_size, tuple(element_ops))
                    if key not in type_index:
                        type_index[key] = len(type_index)
                    objects.append((address, count, type_index[key], name))
                    report["objects"] += 1
        map_path = os.path.join(ROOT, args.map) if not os.path.isabs(args.map) else args.map
        for (address, element_size, element_ops, count, name) in extra_objects(
                REGION_DEFINES[args.region], workdir, gcc, readelf, map_path):
            key = (element_size, tuple(element_ops))
            if key not in type_index:
                type_index[key] = len(type_index)
            objects.append((address, count, type_index[key], name))
            report["objects"] += 1
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    # Two names for the same bytes (aliases) must be swapped once, not twice.
    objects.sort()
    sizes = [key[0] for key in type_index]
    unique = []
    for obj in objects:
        if unique and unique[-1][0] == obj[0] and unique[-1][2] == obj[2]:
            if obj[1] > unique[-1][1]:
                unique[-1] = obj
            continue
        if unique and unique[-1][0] + sizes[unique[-1][2]] * unique[-1][1] > obj[0]:
            report["warnings"].add("overlap, kept the first: %s and %s" % (unique[-1][3], obj[3]))
            continue
        unique.append(obj)
    objects = unique

    write_output(args, type_index, objects)
    print("%s: %d files, %d typed objects, %d byte-only, %d types, %d symbols without a type"
          % (args.region, report["files"], report["objects"], report["byte_only"], len(type_index),
             report["unresolved"]))
    for line in report["skipped_files"]:
        print("  skipped/partial: " + line)
    for line in sorted(report["warnings"]):
        print("  warning: " + line)


def write_output(args, type_index, objects):
    ops_flat = []
    type_rows = []
    for (size, ops), _ in type_index.items():
        type_rows.append((size, len(ops_flat), len(ops)))
        ops_flat.extend(ops)
    with open(args.out, "w", newline="\n") as f:
        f.write("/* Generated by tools/gen_rom_type_map.py --region %s. Do not edit.\n" % args.region)
        f.write(" * The typed data objects of the ROM and their 2- and 4-byte scalars, for\n")
        f.write(" * byte-swapping the ROM on a big-endian host (port/port_rom_swap.c). */\n")
        f.write("#include \"port_rom_type_map.h\"\n\n")
        f.write("static const PortRomScalar kOps[] = {\n")
        for (offset, width) in ops_flat:
            f.write("    { %d, %d },\n" % (offset, width))
        f.write("};\n\nstatic const PortRomType kTypes[] = {\n")
        for (size, first, count) in type_rows:
            f.write("    { %d, %d, %d },\n" % (size, first, count))
        f.write("};\n\nstatic const PortRomObject kObjects[] = {\n")
        for (address, count, tindex, name) in objects:
            f.write("    { 0x%08x, %d, %d }, /* %s */\n" % (address, count, tindex, name))
        f.write("};\n\nconst PortRomTypeMap gPortRomTypeMap_%s = {\n" % args.region)
        f.write("    kOps, kTypes, kObjects, sizeof(kObjects) / sizeof(kObjects[0]),\n};\n")


if __name__ == "__main__":
    main()
