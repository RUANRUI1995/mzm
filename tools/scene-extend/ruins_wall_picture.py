#!/usr/bin/env python3
"""ruins_wall_picture.py OUT.png [EXTRACTED_DIR]: rebuild cutscene 11 stage 2's whole wall picture (240x320) from the game's data:
BG3 = Chozo wall (256x256 map, tiles at char 0); at scroll 96 the game loads the young Samus +
Grey Voice tiles over char 0 and turns BG2 on (map 0x1E, same Y scroll). Rows 0..255: wall with
its own tiles; rows 256..319: BG2 over the wrapped wall, both with the new tiles.

The same as RuinsWallArt in platform/3ds/source/port_scene_game_art.c. EXTRACTED_DIR is the
decomp's include/extracted (default: this checkout's). OUT.png is the game's art: keep it out of
the repo (make_package.py reads it from DUMPS_DIR as ruins-wall-rise's `original`)."""
import os
import re
import struct
import sys

import numpy as np
from PIL import Image
ROOT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'include', 'extracted')
D = os.path.join(ROOT, 'data', 'cutscenes', 'before_ruins_test') + os.sep
def words(name, fmt='<I'):
    vals = [int(v) for v in re.findall(r'(\d+)u?', open(D + name).read())]
    return b''.join(struct.pack(fmt, v) for v in vals)
def lz77(src):
    size = struct.unpack_from('<I', src)[0] >> 8
    s, out = 4, bytearray()
    while len(out) < size:
        flags = src[s]; s += 1
        for bit in range(7, -1, -1):
            if len(out) >= size: break
            if not flags & (1 << bit):
                out.append(src[s]); s += 1
            else:
                b0, b1 = src[s], src[s + 1]; s += 2
                n, dist = (b0 >> 4) + 3, ((b0 & 15) << 8 | b1) + 1
                for _ in range(n): out.append(out[-dist])
    return bytes(out)
wall_gfx = lz77(words('chozo_wall_background.gfx.lz.inc'))
young_gfx = lz77(words('young_samus_and_grey_voice.gfx.lz.inc'))
wall_tt = lz77(words('chozo_wall_background.tt.inc'))
young_tt = lz77(words('young_samus_and_grey_voice.tt.inc'))
pal = struct.unpack('<128H', words('chozo_wall.pal.inc', '<H')[:256])
print('sizes gfx', len(wall_gfx), len(young_gfx), 'tt', len(wall_tt), len(young_tt), file=sys.stderr)
chars_a = bytearray(0x10000); chars_a[:len(wall_gfx)] = wall_gfx
chars_b = bytearray(chars_a); chars_b[:len(young_gfx)] = young_gfx
def code(chars, tt, x, y):
    tx, ty = x >> 3, (y >> 3) & 31
    e = struct.unpack_from('<H', tt, (ty * 32 + tx) * 2)[0]
    px, py = x & 7, y & 7
    if e & 0x400: px = 7 - px
    if e & 0x800: py = 7 - py
    b = chars[(e & 0x3FF) * 32 + py * 4 + px // 2]
    v = (b >> 4) if px & 1 else (b & 15)
    return ((e >> 12) << 4) | v if v else 0
codes = np.zeros((320, 240), np.uint8)
diff = 0
for y in range(320):
    for x in range(240):
        if y < 256:
            c = code(chars_a, wall_tt, x, y)
            if y >= 96 and code(chars_b, wall_tt, x, y) != c: diff += 1
        else:
            c = code(chars_b, young_tt, x, y) or code(chars_b, wall_tt, x, y)
        codes[y, x] = c
print('rows 96..255 differing after the reload:', diff, file=sys.stderr)
rgb = np.array([((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3) for c in pal] + [(0, 0, 0)] * 128, np.uint8)
img = rgb[np.where(codes % 16 == 0, 0, codes)]
Image.fromarray(img).save(sys.argv[1])
