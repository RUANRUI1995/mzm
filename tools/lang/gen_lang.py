#!/usr/bin/env python3
"""Build the port's own translations of the game text.

Reads port/lang/<lang>.txt (format described at the top of that file) and
writes port/generated/lang_<lang>.c: one encoded u16 string per entry and per
ROM region, plus a PortLangTexts table per region that port/port_lang.c hands
to the game in place of the English text.

The strings are encoded with the game's own charmap (charmap.txt, through
tools/preproc.py). Centring ({CENTER}) is worked out from the font widths of
the ROM. Port languages are offered with the EU ROM only (its own languages
are five; the US and JP ROMs carry one), so only the EU ROM is read. The ROMs are only read for
those widths and nothing of them ends up in the output.

The script also checks every line against the space the game has for it: the
widest line any official language of the EU ROM (EN, DE, FR, IT, ES) uses
for the same text, and the most lines any of them uses. Lines that would not fit are reported and make the script fail
(--allow-overflow turns that into warnings).

Usage:
    python tools/lang/gen_lang.py [--lang pt_br] [--rom-dir DIR]

The ROM is looked for in --rom-dir, $MZM_ROM_DIR, the repository root and
its sibling "mzm" checkout, as mzm_eu_baserom.gba.
"""

import argparse
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from preproc import CharMap  # noqa: E402

CHAR_NEW_LINE = 0xFE00
CHAR_NEW_PAGE = 0xFD00
CHAR_TERMINATOR = 0xFF00
CHAR_GAP = 0x8000
CHAR_COLOR = 0x8100
CHAR_INDENT = 0x8300

# Text kinds: the enum prefix in the translation file, the name the game uses
# for the kind in text_data.c, and the field of PortLangTexts.
KINDS = [
    ("MESSAGE_", "Message", "message"),
    ("LT_", "Location", "location"),
    ("DESCRIPTION_TEXT_", "Description", "description"),
    ("STORY_TEXT_", "Story", "story"),
    ("FILE_SCREEN_TEXT_", "FileScreen", "fileScreen"),
]

LANGUAGES = ["English", "German", "French", "Italian", "Spanish"]

# Region -> (ROM game code, ROM file name, sCharacterWidths address, languages
# the region's ROM carries).
REGIONS = {
    "eu": ("BMXP", "mzm_eu_baserom.gba", 0x0840E5E4, LANGUAGES),
}
WIDTH_COUNT = 1184


def kind_of(enum_name):
    for prefix, kind, field in KINDS:
        if enum_name.startswith(prefix):
            return kind, field
    return None, None


# --------------------------------------------------------------------------
# Inputs


def find_rom(region, rom_dir):
    _, file_name, _, _ = REGIONS[region]
    dirs = [rom_dir, os.environ.get("MZM_ROM_DIR"), ROOT, os.path.join(ROOT, "..", "mzm")]
    for d in dirs:
        if d and os.path.isfile(os.path.join(d, file_name)):
            return os.path.join(d, file_name)
    sys.exit(f"gen_lang: {file_name} not found (pass --rom-dir or set MZM_ROM_DIR)")


def load_widths(region, rom_dir):
    code, _, address, _ = REGIONS[region]
    with open(find_rom(region, rom_dir), "rb") as f:
        rom = f.read()
    if rom[0xAC:0xB0].decode("ascii", "replace") != code:
        sys.exit(f"gen_lang: the {region.upper()} ROM does not have game code {code}")
    offset = address - 0x08000000
    return rom[offset:offset + WIDTH_COUNT]


def parse_text_data():
    """Return ({region: {symbol: text}}, {enum: name}) from src/data/text_data.c.

    symbol is e.g. "sEnglishText_Message_EnergyTankAcquired", name is
    "Message_EnergyTankAcquired". Only #ifdef REGION_EU blocks are evaluated;
    every other conditional keeps both branches (the strings are the same)."""
    path = os.path.join(ROOT, "src", "data", "text_data.c")
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")

    texts = {}
    names = {}
    pattern = re.compile(r'const u16 (s(\w+?)Text_(\w+))\[\] = INCTEXT\("(.*)"\);')
    enum_pattern = re.compile(r"\[(\w+)\]\s*=\s*sEnglishText_(\w+)")
    for region in REGIONS:
        stack = []
        found = {}
        for line in lines:
            s = line.strip()
            if s.startswith("#if"):
                stack.append((region == "eu") if s.startswith("#ifdef REGION_EU") else None)
                continue
            if s.startswith("#else"):
                if stack and stack[-1] is not None:
                    stack[-1] = not stack[-1]
                continue
            if s.startswith("#endif"):
                stack.pop()
                continue
            if any(v is False for v in stack):
                continue
            m = pattern.match(s)
            if m:
                found.setdefault(m.group(1), m.group(4))
            m = enum_pattern.search(s)
            if m:
                names.setdefault(m.group(1), m.group(2))
        texts[region] = found
    return texts, names


def parse_translation(path):
    """Return [(enum, [lines], source line number)]."""
    entries = []
    current = None
    with open(path, encoding="utf-8") as f:
        for number, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if line.startswith("#"):
                continue
            m = re.fullmatch(r"\[(\w+)\]\s*", line)
            if m:
                current = (m.group(1), [], number)
                entries.append(current)
                continue
            if not line.strip():
                continue
            if current is None:
                sys.exit(f"{path}:{number}: text before the first [ENTRY]")
            current[1].append(line)
    return entries


# --------------------------------------------------------------------------
# Measuring and encoding


def split_lines(codes):
    lines = [[]]
    for c in codes:
        if c in (CHAR_NEW_LINE, CHAR_NEW_PAGE):
            lines.append([])
        else:
            lines[-1].append(c)
    return lines


def line_width(codes, widths):
    """Pixels from the left of the box to the end of the line."""
    x = 0
    end = 0
    for c in codes:
        high = c & 0xFF00
        if high == CHAR_GAP:
            x += c & 0xFF
        elif high == CHAR_INDENT:
            x = c & 0xFF
        elif high >= 0x8000:
            continue
        else:
            x += widths[c] if c < len(widths) else 10
        end = max(end, x)
    return end


def leading_gap(codes):
    gap = 0
    for c in codes:
        high = c & 0xFF00
        if high == CHAR_GAP:
            gap += c & 0xFF
        elif high == CHAR_COLOR:
            continue
        else:
            break
    return gap


def text_width(codes, widths):
    """Width of the visible text of a line, without its leading GAPs."""
    return line_width(codes, widths) - leading_gap(codes)


def english_layout(english_codes, widths):
    """Per-line centring box widths of the English text (None where a line
    is not centred)."""
    boxes = []
    for codes in split_lines(english_codes):
        gap = leading_gap(codes)
        width = text_width(codes, widths)
        boxes.append(2 * gap + width if gap and width else None)
    return boxes


def encode_entry(lines, charmap, widths, english_codes, region):
    boxes = english_layout(english_codes, widths) if english_codes else []
    known_boxes = [b for b in boxes if b]
    out = []
    for index, raw in enumerate(lines):
        text = raw
        center = text.startswith("{CENTER}")
        if center:
            text = text[len("{CENTER}"):]
        codes = charmap.convert_string(text)
        if center:
            box = boxes[index] if index < len(boxes) and boxes[index] else None
            if box is None and known_boxes:
                box = max(set(known_boxes), key=known_boxes.count)
            if box is not None:
                gap = max(0, (box - text_width(codes, widths) + 1) // 2)
                codes = [CHAR_GAP | gap] + codes if gap else codes
        if index:
            out.append(CHAR_NEW_LINE)
        out.extend(codes)
    return out


# Kinds whose texts each get a window of their own size (the file select
# pop-ups); every other kind draws all its texts in the same box.
PER_ENTRY_KINDS = {"FileScreen"}


def measure_room(texts, charmap, widths):
    """Return {(kind, name or None): (max text width, max line count)}.

    Widths leave out a line's leading GAPs: centred lines get theirs from
    {CENTER}, so what has to fit is the text itself.

    The room is what the official texts use: every language of both ROMs,
    each measured with its own region's font, since the box is the same size
    in pixels whichever font fills it. Kinds drawn in one shared box are
    measured over all their texts (key (kind, None)); per-entry kinds over the
    same text only (key (kind, name))."""
    room = {}
    pattern = re.compile(r"s(\w+?)Text_(\w+?)_(\w+)$")
    for region, (_, _, _, languages) in REGIONS.items():
        for symbol, text in texts[region].items():
            m = pattern.match(symbol)
            if not m or m.group(1) not in languages:
                continue
            kind = m.group(2)
            name = f"{kind}_{m.group(3)}"
            key = (kind, name if kind in PER_ENTRY_KINDS else None)
            lines = split_lines(charmap.convert_string(text))
            width = max(text_width(c, widths[region]) for c in lines)
            old_width, old_lines = room.get(key, (0, 0))
            room[key] = (max(old_width, width), max(old_lines, len(lines)))
    return room


# --------------------------------------------------------------------------
# Main


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--lang", default="pt_br")
    parser.add_argument("--rom-dir")
    parser.add_argument("--allow-overflow", action="store_true")
    args = parser.parse_args()

    source = os.path.join(ROOT, "port", "lang", args.lang + ".txt")
    output = os.path.join(ROOT, "port", "generated", "lang_" + args.lang + ".c")
    charmap = load_charmap()
    texts, names = parse_text_data()
    entries = parse_translation(source)
    widths = {region: load_widths(region, args.rom_dir) for region in REGIONS}
    room = measure_room(texts, charmap, widths)

    problems = []
    seen = set()
    arrays = []
    tables = {region: {} for region in REGIONS}
    for enum, lines, number in entries:
        kind, field = kind_of(enum)
        name = names.get(enum)
        if kind is None or name is None:
            problems.append(f"{source}:{number}: unknown entry [{enum}]")
            continue
        if enum in seen:
            problems.append(f"{source}:{number}: [{enum}] appears twice")
            continue
        seen.add(enum)
        for region, (_, _, _, languages) in REGIONS.items():
            w = widths[region]
            english = texts[region].get("sEnglishText_" + name)
            english_codes = charmap.convert_string(english) if english is not None else None
            codes = encode_entry(lines, charmap, w, english_codes, region)

            limit, max_lines = room[(kind, name if kind in PER_ENTRY_KINDS else None)]
            for i, line in enumerate(split_lines(codes)):
                width = text_width(line, w)
                if limit and width > limit:
                    problems.append(f"{source}:{number}: [{enum}] {region.upper()} line {i + 1} is "
                                    f"{width} px wide, the official texts use at most {limit} px")
            if max_lines and len(split_lines(codes)) > max_lines:
                problems.append(f"{source}:{number}: [{enum}] has {len(split_lines(codes))} lines, "
                                f"the official texts use at most {max_lines}")

            symbol = f"s{args.lang.title().replace('_', '')}_{region.upper()}_{enum}"
            body = ", ".join(f"0x{c:04X}" for c in codes + [CHAR_TERMINATOR])
            arrays.append(f"static const u16 {symbol}[] = {{ {body} }};")
            tables[region].setdefault(field, []).append((enum, symbol))

    for problem in problems:
        print(problem, file=sys.stderr)
    overflow = [p for p in problems if " px wide" in p or "lines, the official" in p]
    if len(overflow) < len(problems) or (overflow and not args.allow_overflow):
        sys.exit(1)

    table_prefix = "gPortLang" + args.lang.title().replace("_", "")
    out = [
        f"/* Generated by tools/lang/gen_lang.py from port/lang/{args.lang}.txt. Do not edit. */",
        "",
        '#include "port_lang_texts.h"',
        "",
    ]
    out.extend(arrays)
    for region in REGIONS:
        out.append("")
        out.append(f"const PortLangTexts {table_prefix}_{region.upper()} = {{")
        for _, _, field in KINDS:
            items = tables[region].get(field)
            if not items:
                continue
            out.append(f"    .{field} = {{")
            for enum, symbol in items:
                out.append(f"        [{enum}] = {symbol},")
            out.append("    },")
        out.append("};")
    out.append("")

    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"gen_lang: {len(seen)} entries -> {os.path.relpath(output, ROOT)}")


def load_charmap():
    # tools/preproc.py opens charmap.txt with the locale's encoding; read it
    # as UTF-8 here so the script works on Windows too.
    import builtins
    real_open = builtins.open

    def utf8_open(file, mode="r", *a, **kw):
        if "b" not in mode and "encoding" not in kw:
            kw["encoding"] = "utf-8"
        return real_open(file, mode, *a, **kw)

    builtins.open = utf8_open
    try:
        return CharMap.parse_charmap(os.path.join(ROOT, "charmap.txt"))
    finally:
        builtins.open = real_open


if __name__ == "__main__":
    main()
