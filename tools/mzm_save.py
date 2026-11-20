#!/usr/bin/env python3
"""Look at and edit mzm.sav, the GBA's save file (32 KB, struct Sram of
include/structs/save_file.h), for test runs of the PC build (platform/pc).

    python3 tools/mzm_save.py show SAVE
    python3 tools/mzm_save.py warp SAVE OUT --area norfair --door 5 [--file 0] [--items all]
    python3 tools/mzm_save.py doors --area norfair

`warp` makes a file resume in another room: it sets the area and room of a
door, puts Samus where that door lets her out (as RoomLoad does in
src/room.c), aims the camera at her and marks the file as saved, then writes
the checksums back (main file and its copy). `--items all` gives every item.
The room's events and the items collected are left as they were, so the rest
of the game state is a fresh one: good enough to look at other areas, not a
real playthrough.

The field offsets come from devkitARM's gcc (DWARF of struct Sram, as
tools/gen_sram_byteswap.py reads it); the door table from the ROM
(mzm_eu_baserom.gba, sAreaDoorsPointers in mzm_eu.map).
"""
import argparse
import os
import re
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_rom_type_map as romtypes  # noqa: E402

ROOT = romtypes.ROOT
PROBE = '#include "types.h"\n#include "structs/save_file.h"\nstruct Sram probe;\n'
AREAS = ["brinstar", "kraid", "norfair", "ridley", "tourian", "crateria", "chozodia"]
DOOR_SIZE = 12
BLOCK_SIZE = 64
SAVE_FILE_SIZE = 0x1220

# include/constants/samus.h
SPOSE_STANDING = 1
STANDING_MIDAIR = 2
KEY_RIGHT = 0x10


class Layout:
    """Offsets of struct Sram's members, by path ("files[1].samusData.xPosition")."""

    def __init__(self):
        gcc = romtypes.devkitarm_bin("gcc")
        readelf = romtypes.devkitarm_bin("readelf")
        with tempfile.TemporaryDirectory() as workdir:
            source = os.path.join(workdir, "sram_probe.c")
            with open(source, "w", encoding="utf-8") as f:
                f.write(PROBE)
            dwarf, _ = romtypes.compile_file(source, "REGION_EU", workdir, gcc, readelf)
        self.dies = romtypes.parse_dwarf(dwarf)
        self.types = romtypes.Types(self.dies)
        self.root = romtypes.variables(self.dies)["probe"]

    def strip(self, offset):
        return self.types.strip(self.dies.get(offset))

    def field(self, path):
        """(offset, size, signed) of a scalar member."""
        die = self.strip(self.root)
        offset = 0
        for name, index in re.findall(r"(\w+)((?:\[\d+\])*)", path):
            member = next((m for m in die.children if m.tag == "DW_TAG_member" and romtypes.attr_name(m) == name),
                          None)
            if member is None:
                sys.exit("no member %r in %s" % (name, path))
            offset += romtypes.attr_int(member, "DW_AT_data_member_location") or 0
            die = self.strip(romtypes.attr_ref(member, "DW_AT_type"))
            for i in re.findall(r"\[(\d+)\]", index):
                if die.tag != "DW_TAG_array_type":
                    sys.exit("%s is not an array" % name)
                element = romtypes.attr_ref(die, "DW_AT_type")
                offset += int(i) * self.types.layout(element)[0]
                die = self.strip(element)
        size = romtypes.attr_int(die, "DW_AT_byte_size") or 0
        encoding = die.attrs.get("DW_AT_encoding", "")
        return offset, size, "signed" in encoding and "unsigned" not in encoding


class Save:
    def __init__(self, path, layout):
        self.data = bytearray(open(path, "rb").read())
        if len(self.data) != 0x8000:
            sys.exit("%s: %d bytes, not a 32 KB GBA save" % (path, len(self.data)))
        self.layout = layout

    def get(self, path):
        offset, size, signed = self.layout.field(path)
        return int.from_bytes(self.data[offset:offset + size], "little", signed=signed)

    def set(self, path, value):
        offset, size, signed = self.layout.field(path)
        self.data[offset:offset + size] = (int(value) & ((1 << 8 * size) - 1)).to_bytes(size, "little")

    def seal(self, file_path):
        """Checksum of a struct SaveFile as SramWrite_HeaderAndGameInfo computes it."""
        start = self.layout.field(file_path + ".ZERO_MISSION_010_Text")[0]
        self.set(file_path + ".checksum", 0)
        self.set(file_path + ".notChecksum", -1)
        words = struct.unpack("<%dI" % (SAVE_FILE_SIZE // 4), bytes(self.data[start:start + SAVE_FILE_SIZE]))
        checksum = sum(words) & 0xFFFFFFFF
        self.set(file_path + ".checksum", checksum)
        self.set(file_path + ".notChecksum", ~checksum)

    def valid(self, file_path):
        start = self.layout.field(file_path + ".ZERO_MISSION_010_Text")[0]
        words = struct.unpack("<%dI" % (SAVE_FILE_SIZE // 4), bytes(self.data[start:start + SAVE_FILE_SIZE]))
        checksum = self.get(file_path + ".checksum") & 0xFFFFFFFF
        return sum(words) & 0xFFFFFFFF == checksum and checksum == ~self.get(file_path + ".notChecksum") & 0xFFFFFFFF


def map_symbol(name):
    with open(os.path.join(ROOT, "mzm_eu.map")) as f:
        for line in f:
            m = re.match(r"^\s+0x([0-9a-f]+)\s+(\w+)\s*$", line)
            if m and m.group(2) == name:
                return int(m.group(1), 16)
    sys.exit("%s not in mzm_eu.map" % name)


def doors_of(area):
    rom = open(os.path.join(ROOT, "mzm_eu_baserom.gba"), "rb").read()
    table = map_symbol("sAreaDoorsPointers") - 0x08000000
    pointers = struct.unpack("<8I", rom[table:table + 32])
    start, end = pointers[area] - 0x08000000, pointers[area + 1] - 0x08000000
    doors = []
    for at in range(start, end - DOOR_SIZE + 1, DOOR_SIZE):
        kind, room, x_start, x_end, y_start, y_end, dest, x_exit, y_exit = struct.unpack("<8Bb", rom[at:at + 9])
        x_exit = x_exit - 256 if x_exit > 127 else x_exit
        doors.append(dict(type=kind, room=room, xStart=x_start, xEnd=x_end, yStart=y_start, yEnd=y_end,
                          dest=dest, xExit=x_exit, yExit=y_exit))
    return doors


def area_number(text):
    if text.isdigit():
        return int(text)
    if text.lower() not in AREAS:
        sys.exit("area: one of %s or a number" % ", ".join(AREAS))
    return AREAS.index(text.lower())


def cmd_show(args):
    save = Save(args.save, Layout())
    for n in range(3):
        f = "files[%d]" % n
        print("file %c: %s, saved %d, area %d room %d door %d, Samus at (%d, %d), energy %d/%d, missiles %d/%d" % (
            "ABC"[n], "valid" if save.valid(f) else "no valid checksum", save.get(f + ".hasSaved"),
            save.get(f + ".currentArea"), save.get(f + ".currentRoom"), save.get(f + ".LastDoorUsed"),
            save.get(f + ".samusData.xPosition"), save.get(f + ".samusData.yPosition"),
            save.get(f + ".equipment.currentEnergy"), save.get(f + ".equipment.maxEnergy"),
            save.get(f + ".equipment.currentMissiles"), save.get(f + ".equipment.maxMissiles")))


def cmd_doors(args):
    for i, d in enumerate(doors_of(area_number(args.area))):
        print("door %3d: room %2d, type 0x%02x, blocks x %d-%d y %d-%d, exit (%d, %d), to door %d" % (
            i, d["room"], d["type"], d["xStart"], d["xEnd"], d["yStart"], d["yEnd"], d["xExit"], d["yExit"],
            d["dest"]))


def cmd_warp(args):
    area = area_number(args.area)
    doors = doors_of(area)
    if not 0 <= args.door < len(doors):
        sys.exit("area %d has doors 0-%d" % (area, len(doors) - 1))
    door = doors[args.door]
    save = Save(args.save, Layout())
    f = "files[%d]" % args.file
    # Where RoomLoad puts Samus when she comes through the door (src/room.c).
    x = door["xStart"] * BLOCK_SIZE + (door["xExit"] + 8) * 4
    y = (door["yEnd"] + 1) * BLOCK_SIZE + door["yExit"] * 4 - 1
    save.set(f + ".hasSaved", 1)
    save.set(f + ".currentArea", area)
    save.set(f + ".currentRoom", door["room"])
    save.set(f + ".LastDoorUsed", args.door)
    save.set(f + ".samusData.xPosition", x)
    save.set(f + ".samusData.yPosition", y)
    save.set(f + ".samusData.pose", SPOSE_STANDING)
    save.set(f + ".samusData.standingStatus", STANDING_MIDAIR)
    save.set(f + ".samusData.direction", KEY_RIGHT)
    # The screen is 240x160 pixels, a pixel 4 units; the scrolling clamps it to the room.
    save.set(f + ".camera.xPosition", max(0, x - 120 * 4))
    save.set(f + ".camera.yPosition", max(0, y - 100 * 4))
    if args.items == "all":
        for field, value in (("maxEnergy", 1299), ("currentEnergy", 1299), ("maxMissiles", 250),
                             ("currentMissiles", 250), ("maxSuperMissiles", 30), ("currentSuperMissiles", 30),
                             ("maxPowerBombs", 18), ("currentPowerBombs", 18), ("beamBombs", 0x9F),
                             ("beamBombsActivation", 0x9F), ("suitMisc", 0xFF), ("suitMiscActivation", 0xFF)):
            save.set(f + ".equipment." + field, value)
    elif save.get(f + ".equipment.maxEnergy") == 0:
        save.set(f + ".equipment.maxEnergy", 99)
        save.set(f + ".equipment.currentEnergy", 99)
    save.seal(f)
    copy = "filesCopy[%d]" % args.file
    start = save.layout.field(f + ".ZERO_MISSION_010_Text")[0]
    copy_start = save.layout.field(copy + ".ZERO_MISSION_010_Text")[0]
    save.data[copy_start:copy_start + SAVE_FILE_SIZE] = save.data[start:start + SAVE_FILE_SIZE]
    open(args.out, "wb").write(save.data)
    print("%s: file %c in area %d room %d through door %d, Samus at (%d, %d)" % (
        args.out, "ABC"[args.file], area, door["room"], args.door, x, y))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("show")
    p.add_argument("save")
    p.set_defaults(run=cmd_show)
    p = sub.add_parser("doors")
    p.add_argument("--area", required=True)
    p.set_defaults(run=cmd_doors)
    p = sub.add_parser("warp")
    p.add_argument("save")
    p.add_argument("out")
    p.add_argument("--area", required=True)
    p.add_argument("--door", type=int, required=True)
    p.add_argument("--file", type=int, default=0, choices=(0, 1, 2))
    p.add_argument("--items", choices=("keep", "all"), default="keep")
    p.set_defaults(run=cmd_warp)
    args = ap.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
