#!/usr/bin/env python3
"""Build the port's patches for the ROM graphics that carry English text.

Some of the game's text is drawn into graphics (the pause screen's item
names, the file select menus, GAME OVER...). For a port language
(port/port_lang.h) those graphics are patched in VRAM right after the game
decompresses the English ones: the patch rearranges columns of letters taken
from the English graphic itself and draws the few letters it lacks. So the
binary only carries the patch (column copies and a handful of small glyphs of
our own), never the ROM's art, and the same patch works on every ROM whose
English graphic has the same layout.

The graphics and the words are described in port/lang/<lang>_gfx.py. This
script:
  - reads each English graphic from the US and EU ROMs and checks they match,
  - cuts the English words into letters (columns between blank columns),
  - lays out each translated word in its slot (left-aligned where the English
    word starts, or centred), checking that it fits,
  - writes port/generated/lang_<lang>_gfx.c (op lists, see port_lang_gfx.c),
  - and with --preview DIR writes PNGs of each graphic before and after.

Usage:
    python tools/lang/gen_lang_gfx.py [--lang pt_br] [--rom-dir DIR] [--preview DIR]
"""

import argparse
import importlib.util
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

REGIONS = {"eu": ("BMXP", "mzm_eu_baserom.gba"), "us": ("BMXE", "mzm_us_baserom.gba")}
REGION_CASE = {"eu": "PORT_ROM_REGION_EU", "us": "PORT_ROM_REGION_US"}

# Ops, as written to the C file (port_lang_gfx.c reads them).
OP_END, OP_COPY, OP_FILL, OP_GLYPH = 0, 1, 2, 3


# --------------------------------------------------------------------------
# ROM access


def find_rom(region, rom_dir):
    name = REGIONS[region][1]
    for d in (rom_dir, os.environ.get("MZM_ROM_DIR"), ROOT, os.path.join(ROOT, "..", "mzm")):
        if d and os.path.isfile(os.path.join(d, name)):
            return os.path.join(d, name)
    sys.exit(f"gen_lang_gfx: {name} not found (pass --rom-dir or set MZM_ROM_DIR)")


def symbol_addresses():
    """{region: {symbol: GBA address}} from port/generated/*_rom.c."""
    result = {"eu": {}, "us": {}, "jp": {}}
    case_re = re.compile(r"case PORT_ROM_REGION_(EU|US|JP):")
    sym_re = re.compile(r"p_(\w+) = .*Port_ResolveRomData\(0x([0-9a-fA-F]+)u\)")
    gen = os.path.join(ROOT, "port", "generated")
    for name in os.listdir(gen):
        if not name.endswith("_rom.c"):
            continue
        region = None
        with open(os.path.join(gen, name), encoding="utf-8") as f:
            for line in f:
                m = case_re.search(line)
                if m:
                    region = m.group(1).lower()
                    continue
                m = sym_re.search(line)
                if m and region:
                    result[region].setdefault(m.group(1), int(m.group(2), 16))
    return result


def lz77(rom, offset):
    if rom[offset] != 0x10:
        raise ValueError(f"no LZ77 data at 0x{offset:x}")
    size = rom[offset + 1] | rom[offset + 2] << 8 | rom[offset + 3] << 16
    o = offset + 4
    out = bytearray()
    while len(out) < size:
        flags = rom[o]
        o += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                v = rom[o] << 8 | rom[o + 1]
                o += 2
                for _ in range((v >> 12) + 3):
                    out.append(out[-((v & 0xFFF) + 1)])
            else:
                out.append(rom[o])
                o += 1
    return bytes(out)


# --------------------------------------------------------------------------
# A graphic as 4bpp tiles


class Sheet:
    def __init__(self, data):
        self.data = bytearray(data)

    def get(self, tile, x, y):
        b = self.data[tile * 32 + y * 4 + x // 2]
        return (b >> ((x & 1) * 4)) & 15

    def set(self, tile, x, y, v):
        i = tile * 32 + y * 4 + x // 2
        if x & 1:
            self.data[i] = (self.data[i] & 0x0F) | (v << 4)
        else:
            self.data[i] = (self.data[i] & 0xF0) | v


class Slot:
    """A rectangle of tiles: `rows` tile runs of `width` tiles each, given by
    the first tile of every run (the runs need not be adjacent in the sheet)."""

    def __init__(self, firsts, width):
        self.firsts = list(firsts)
        self.width = width

    @property
    def px(self):
        return self.width * 8

    @property
    def height(self):
        return len(self.firsts) * 8

    def column(self, sheet, x):
        tile_off, cx = divmod(x, 8)
        col = []
        for first in self.firsts:
            for y in range(8):
                col.append(sheet.get(first + tile_off, cx, y))
        return tuple(col)

    def put_column(self, sheet, x, col, mask=0xFFFF):
        tile_off, cx = divmod(x, 8)
        i = 0
        for first in self.firsts:
            for y in range(8):
                if i >= 16 or mask & (1 << i):
                    sheet.set(first + tile_off, cx, y, col[i])
                i += 1


# --------------------------------------------------------------------------
# Fonts: letters cut out of English slots


class Font:
    def __init__(self, name, spec, sheet, slots):
        self.name = name
        self.ink_rows = spec.get("ink_rows")  # rows that decide blank columns
        self.space = spec.get("space", 3)
        self.gap = spec.get("gap", 1)
        self.letters = {}  # char -> ("copy", slot_name, x, w) or ("own", columns)
        self.spec = spec
        bg_slot, bg_x = spec["bg_from"]
        self.bg = slots[bg_slot].column(sheet, bg_x)
        for source in spec["sources"]:
            slot_name, text = source[0], source[1]
            if len(source) > 2 and source[2] is not None:
                # letters that touch: their widths, from the slot's left edge
                # (or from the column given after them)
                x = source[3] if len(source) > 3 else 0
                for c, w in zip([c for c in text if c != " "], source[2]):
                    self.letters.setdefault(c, ("copy", slot_name, x, w))
                    x += w
            else:
                # (slot, text[, None, x0, x1]): cut between blank columns,
                # optionally only within columns x0..x1
                lo, hi = (source[3], source[4]) if len(source) > 3 else (0, None)
                self._cut(sheet, slots[slot_name], slot_name, text, lo, hi)
        for ch, art in spec.get("own", {}).items():
            self.letters[ch] = ("own", self._own_columns(art, spec))

    def blank(self, col, bg):
        rows = self.ink_rows or range(len(col))
        return all(col[r] == bg[r] for r in rows)

    def _cut(self, sheet, slot, slot_name, text, lo=0, hi=None):
        bg = self.bg
        end_x = hi if hi is not None else slot.px - self.spec.get("border", 0)
        runs = []
        x = lo
        while x < end_x:
            if not self.blank(slot.column(sheet, x), bg):
                start = x
                while x < end_x and not self.blank(slot.column(sheet, x), bg):
                    x += 1
                runs.append((start, x - start))
            else:
                x += 1
        chars = [c for c in text if c != " "]
        merge = self.spec.get("merge", {})
        # Some letters are drawn as two blank-separated parts (") and so on);
        # the spec names how many runs such a letter takes.
        i = 0
        for c in chars:
            n = merge.get(c, 1)
            if i + n > len(runs):
                raise SystemExit(f"font {self.name}: '{text}' in {slot_name} has too few letters "
                                 f"({len(runs)} runs)")
            start = runs[i][0]
            end = runs[i + n - 1][0] + runs[i + n - 1][1]
            self.letters.setdefault(c, ("copy", slot_name, start, end - start))
            i += n
        if i != len(runs) and not self.spec.get("allow_extra_runs"):
            raise SystemExit(f"font {self.name}: '{text}' in {slot_name} has {len(runs)} runs, "
                             f"expected {i}")

    def _own_columns(self, art, spec):
        """ASCII art (a list of rows, from row `own_top` of the slot) ->
        columns. '.' takes the background colour of that row; other
        characters go through spec['palette'], or are hex palette indices."""
        rows = [r.rstrip() for r in art]
        width = max(len(r) for r in rows)
        top = spec.get("own_top", 0)
        cols = []
        for x in range(width):
            col = list(self.bg)
            for y, r in enumerate(rows):
                ch = r[x] if x < len(r) else "."
                if ch != ".":
                    col[top + y] = spec["palette"][ch] if ch in spec.get("palette", {}) else int(ch, 16)
            cols.append(tuple(col))
        return cols

    def width(self, ch):
        kind = self.letters[ch]
        return kind[3] if kind[0] == "copy" else len(kind[1])


# --------------------------------------------------------------------------
# Layout


def layout(word, font, avail):
    """[(x, char)] with x relative to the text start, and the total width."""
    out = []
    x = 0
    prev_letter = False
    for ch in word:
        if ch == " ":
            x += font.space
            prev_letter = False
            continue
        if ch not in font.letters:
            raise SystemExit(f"font {font.name} has no letter '{ch}' (word '{word}')")
        if prev_letter:
            x += font.gap
        out.append((x, ch))
        x += font.width(ch)
        prev_letter = True
    return out, x


def build_graphic(name, spec, sheets, fonts_spec):
    """Return (ops, en_sheet, pt_sheet, problems) for one graphic."""
    sheet = sheets
    slots = {k: Slot(*v) for k, v in spec["slots"].items()}
    fonts = {k: Font(k, v, sheet, slots) for k, v in spec["fonts"].items()}
    out = Sheet(sheet.data)
    ops = []
    glyphs = []
    problems = []
    for key, (font_name, word, *opts) in spec["words"].items():
        # "SLOT@tag" places a second word in the same slot (with x0/x1)
        slot_name = key.split("@")[0]
        opts = opts[0] if opts else {}
        slot = slots[slot_name]
        font = fonts[font_name]
        # pixel rows of the slot the font may change (the rest stay as they are)
        mask = sum(1 << r for r in font.spec["rows"]) if "rows" in font.spec else 0xFFFF
        # The text area: from the first non-background column of the English
        # word (or opts['x0']) to the last column before the slot's right
        # border (columns at the end that differ from the background).
        x0 = opts.get("x0")
        if x0 is None:
            x0 = next((x for x in range(slot.px) if not font.blank(slot.column(sheet, x), font.bg)), 0)
        border = opts.get("border", font.spec.get("border", 0))
        if "x1" in opts:
            right = opts["x1"]
            border = 0
        else:
            right = slot.px - border
        placed, width = layout(word, font, right - x0)
        if opts.get("align") == "center":
            x0 = max(0, x0 + (right - x0 - width) // 2)
        if x0 + width > right:
            problems.append(f"{name}: '{word}' is {width} px, {slot_name} has {right - x0}")
        # background over the text area, then the letters
        ops.append((OP_FILL, slot_name, x0, font.spec["bg_from"][0], font.spec["bg_from"][1], right - x0, mask))
        for x in range(x0, right):
            slot.put_column(out, x, font.bg, mask)
        if border:
            # the English word may run into the border: put back a clean one
            bg_slot = slots[font.spec["bg_from"][0]]
            ops.append((OP_COPY, slot_name, slot.px - border, font.spec["bg_from"][0], bg_slot.px - border, border, mask))
            for i in range(border):
                slot.put_column(out, slot.px - border + i, bg_slot.column(sheet, bg_slot.px - border + i), mask)
        for dx, ch in placed:
            kind = font.letters[ch]
            if kind[0] == "copy":
                _, src_slot, sx, w = kind
                ops.append((OP_COPY, slot_name, x0 + dx, src_slot, sx, w, mask))
                for i in range(w):
                    slot.put_column(out, x0 + dx + i, slots[src_slot].column(sheet, sx + i), mask)
            else:
                cols = kind[1]
                glyphs.append(cols)
                ops.append((OP_GLYPH, slot_name, x0 + dx, len(glyphs) - 1, len(cols), mask))
                for i, c in enumerate(cols):
                    slot.put_column(out, x0 + dx + i, c, mask)
    return ops, glyphs, slots, out, problems


# --------------------------------------------------------------------------
# Output


def render_png(sheet, path, tiles_wide=16, scale=4):
    from PIL import Image
    pal = [(0, 0, 0), (255, 255, 255), (200, 200, 200), (150, 150, 150), (100, 100, 100), (255, 80, 80),
           (80, 255, 80), (80, 80, 255), (255, 255, 80), (255, 80, 255), (80, 255, 255), (255, 160, 0),
           (160, 0, 255), (0, 160, 255), (160, 255, 0), (120, 60, 0)]
    n = len(sheet.data) // 32
    rows = (n + tiles_wide - 1) // tiles_wide
    img = Image.new("RGB", (tiles_wide * 8, rows * 8))
    for t in range(n):
        for y in range(8):
            for x in range(8):
                img.putpixel(((t % tiles_wide) * 8 + x, (t // tiles_wide) * 8 + y), pal[sheet.get(t, x, y)])
    img.resize((img.width * scale, img.height * scale), Image.NEAREST).save(path)


def c_ops(name, ops, glyphs, slots):
    slot_ids = {k: i for i, k in enumerate(slots)}
    lines = [f"static const PortLangGfxSlot sSlots_{name}[] = {{"]
    for k, s in slots.items():
        firsts = ", ".join(str(f) for f in s.firsts)
        lines.append(f"    {{ {len(s.firsts)}, {s.width}, {{ {firsts} }} }}, /* {k} */")
    lines.append("};")
    glyph_words = []
    offsets = []
    for cols in glyphs:
        offsets.append(len(glyph_words))
        for col in cols:
            # two pixels per byte, low nibble first, like the tiles
            for i in range(0, len(col), 2):
                glyph_words.append(col[i] | (col[i + 1] << 4))
    if glyph_words:
        lines.append(f"static const u8 sGlyphs_{name}[] = {{")
        for i in range(0, len(glyph_words), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in glyph_words[i:i + 16]) + ",")
        lines.append("};")
    lines.append(f"static const u16 sOps_{name}[] = {{")
    for op in ops:
        if op[0] == OP_FILL:
            _, d, dx, s, sx, n, m = op
            lines.append(f"    PLG_FILL, {slot_ids[d]}, {dx}, {slot_ids[s]}, {sx}, {n}, 0x{m:04X},")
        elif op[0] == OP_COPY:
            _, d, dx, s, sx, n, m = op
            lines.append(f"    PLG_COPY, {slot_ids[d]}, {dx}, {slot_ids[s]}, {sx}, {n}, 0x{m:04X},")
        else:
            _, d, dx, g, n, m = op
            lines.append(f"    PLG_GLYPH, {slot_ids[d]}, {dx}, {offsets[g]}, {n}, 0, 0x{m:04X},")
    lines.append("    PLG_END")
    lines.append("};")
    return lines, bool(glyph_words)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--lang", default="pt_br")
    parser.add_argument("--rom-dir")
    parser.add_argument("--preview")
    parser.add_argument("--only")
    args = parser.parse_args()

    spec_path = os.path.join(ROOT, "port", "lang", args.lang + "_gfx.py")
    module_spec = importlib.util.spec_from_file_location("lang_gfx", spec_path)
    lang_gfx = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(lang_gfx)

    addrs = symbol_addresses()
    roms = {}
    for region in REGIONS:
        with open(find_rom(region, args.rom_dir), "rb") as f:
            roms[region] = f.read()
        if roms[region][0xAC:0xB0].decode("ascii", "replace") != REGIONS[region][0]:
            sys.exit(f"gen_lang_gfx: wrong game code in the {region.upper()} ROM")

    out = [
        f"/* Generated by tools/lang/gen_lang_gfx.py from port/lang/{args.lang}_gfx.py. Do not edit. */",
        "",
        '#include "port_lang_gfx.h"',
        "",
    ]
    table = []
    problems = []
    for name, spec in lang_gfx.GRAPHICS.items():
        if args.only and name != args.only:
            continue
        sheets = {}
        for region in REGIONS:
            addr = addrs[region].get(spec["symbol"])
            if addr is None:
                sys.exit(f"gen_lang_gfx: {spec['symbol']} has no {region.upper()} address")
            sheets[region] = lz77(roms[region], addr - 0x08000000)
        if sheets["eu"] != sheets["us"]:
            problems.append(f"{name}: the EU and US English graphics differ; the patch is EU-only")
            regions = ["eu"]
        else:
            regions = ["eu", "us"]
        ops, glyphs, slots, result, probs = build_graphic(name, spec, Sheet(sheets["eu"]), None)
        problems.extend(probs)
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            render_png(Sheet(sheets["eu"]), os.path.join(args.preview, f"{name}_en.png"), spec.get("preview_wide", 16))
            render_png(result, os.path.join(args.preview, f"{name}_{args.lang}.png"), spec.get("preview_wide", 16))
        lines, has_glyphs = c_ops(name, ops, glyphs, slots)
        out.extend(lines)
        out.append("")
        table.append((spec["id"], name, len(sheets["eu"]), regions, has_glyphs))

    prefix = "gPortLangGfx" + args.lang.title().replace("_", "")
    out.append(f"const PortLangGfxPatch {prefix}[PORT_LANG_GFX_COUNT] = {{")
    for gid, name, size, regions, has_glyphs in table:
        mask = " | ".join(f"(1u << {REGION_CASE[r]})" for r in regions)
        glyphs = f"sGlyphs_{name}" if has_glyphs else "NULL"
        out.append(f"    [{gid}] = {{ {size}, {mask}, sSlots_{name}, {glyphs}, sOps_{name} }},")
    out.append("};")
    out.append("")

    for p in problems:
        print(p, file=sys.stderr)
    if any("px," in p for p in problems):
        sys.exit(1)
    path = os.path.join(ROOT, "port", "generated", f"lang_{args.lang}_gfx.c")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"gen_lang_gfx: {len(table)} graphics -> {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    main()
