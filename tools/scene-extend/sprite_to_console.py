#!/usr/bin/env python3
"""The AI's finished sprite frames -> platform/3ds/source/port_scene_sprites_data.c.

    sprite_to_console.py PACKAGE_DIR VARIANT [SPRITE ...]

PACKAGE_DIR holds the packages make_sprite_package.py wrote (mzm-<sprite>/); VARIANT is the
chosen variant's file prefix there (`variant-1` reads variant-1-A.png, variant-1-B.png, ...,
one per frame of make_sprite_package.SPRITES). SPRITE defaults to every sprite of that table.
The sprites of the data file that are not converted this time are kept as they are (their
package may not be on this machine).

Only the generated parts are kept: the pixels where the frame's template (1-template-*.png, the
game's own drawing) is transparent. The console draws the game's pieces from OBJ VRAM over them
(port_scene_sprites.c), so nothing of the game's art is shipped. A variant that changes a pixel
of the template is refused.

Each pixel is stored as its OBJ palette entry, looked up in the palette the scene loads into OBJ
(the `palette` of the sprite below: the build's extracted .pal.inc, made from your ROM), in the
bank of the frame's pieces, so the console paints it with the palette of each frame (fades).
The data is laid out as the GPU texture (8x8 Morton tiles, power-of-two size) and
LZ4-compressed, like to_console.py's pictures.
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from generate_bezel_asset import lz4_compress  # noqa: E402
from make_sprite_package import SPRITES  # noqa: E402
from to_console import pow2, swizzle  # noqa: E402

OUTPUT_C = os.path.join(ROOT, 'platform', '3ds', 'source', 'port_scene_sprites_data.c')
# Game mode (include/constants/game_state.h), scene key (PortScenePic_SceneKey: the intro's
# stage) and the OBJ palette the scene loads (src/intro.c IntroViewOfZebes: sIntroViewOfZebesPal).
SCENES = {
    'intro-zebes-flame': (1, 4, 'include/extracted/data/intro/view_of_zebes.pal.inc'),
    # Cutscene 7 stage 1 (key 7 * 16 + 1): src/cutscenes/ridley_in_space.c loads sRidleyInSpaceShipsPal.
    'ridley-mothership': (10, 7 * 16 + 1, 'include/extracted/data/cutscenes/ridley_in_space/ships.pal.inc'),
}


def read_palette(path):
    # include/extracted is made by the build; a worktree may not have it, the main checkout next
    # to it does (MZM_EXTRACTED_ROOT: another checkout that has it).
    for root in (os.environ.get('MZM_EXTRACTED_ROOT', ''), ROOT, os.path.join(ROOT, '..', 'mzm')):
        if root and os.path.exists(os.path.join(root, path)):
            break
    with open(os.path.join(root, path)) as f:
        values = [int(v) for v in re.findall(r'(\d+)u', f.read())]
    return [((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3) for c in values]


def convert(key, package_dir, variant):
    sp = SPRITES[key]
    mode, scene_key, pal_path = SCENES[key]
    pal = read_palette(pal_path)
    cx0, cy0, cx1, cy1 = sp['canvas']
    W, H = cx1 - cx0, cy1 - cy0
    tex_w, tex_h = pow2(W), pow2(H)
    folder = os.path.join(package_dir, f'mzm-{key}')
    frames = []
    for name, entries in sp['frames'].items():
        bank = entries[0][4]
        codes = {}
        for i in range(1, 16):
            if bank * 16 + i < len(pal):
                codes.setdefault(pal[bank * 16 + i], bank * 16 + i)
        tpl = Image.open(os.path.join(folder, f'1-template-{name}-{W}x{H}.png')).convert('RGBA')
        img = Image.open(os.path.join(folder, f'{variant}-{name}.png')).convert('RGBA')
        if img.size != (W, H):
            sys.exit(f'{variant}-{name}.png: {img.size[0]}x{img.size[1]}, the canvas is {W}x{H}')
        tp, ip = tpl.load(), img.load()
        data = bytearray(tex_w * tex_h)
        kept = 0
        for y in range(H):
            for x in range(W):
                if tp[x, y][3]:
                    if ip[x, y] != tp[x, y]:
                        sys.exit(f'{variant}-{name}.png changes the game\'s pixel at ({x}, {y})')
                    continue
                c = ip[x, y]
                if c[3] == 0 or c[:3] == (255, 0, 255):
                    continue
                if c[3] != 255 or c[:3] not in codes:
                    sys.exit(f'{variant}-{name}.png: ({x}, {y}) is {c}, not a colour of OBJ bank {bank}')
                data[swizzle(x, y, tex_w)] = codes[c[:3]]
                kept += 1
        packed = lz4_compress(bytes(data))
        print(f'{key} {name}: {kept} generated pixels, {len(data)} -> {len(packed)} bytes')
        frames.append(packed)
    ax, ay = sp['anchor']
    return dict(key=key, mode=mode, scene_key=scene_key, canvas=(cx0 - ax, cy0 - ay, W, H),
                tex=(tex_w, tex_h), frames=frames)


def existing():
    """The data file's sprites: key -> (its arrays' C text, its table row)."""
    if not os.path.exists(OUTPUT_C):
        return {}
    text = open(OUTPUT_C, encoding='utf-8').read()
    out = {}
    for row in re.findall(r'^    \{ "([^"]+)".*$', text, re.M):
        name = row.replace('-', '_')
        blocks = re.findall(r'^static const [^\n]*_' + re.escape(name) + r'(?:_\d+)?\[.*?;\n', text, re.M | re.S)
        line = re.search(r'^    \{ "' + re.escape(row) + r'".*$', text, re.M).group(0)
        out[row] = (''.join(b + ('\n' if b.startswith('static const uint8_t kPacked') else '') for b in blocks), line)
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sprites = [convert(key, sys.argv[1], sys.argv[2]) for key in sys.argv[3:] or SCENES]
    kept = {k: v for k, v in existing().items() if k not in {s['key'] for s in sprites}}
    if kept:
        print('kept from the data file: ' + ', '.join(kept))
    with open(OUTPUT_C, 'w', newline='\n') as f:
        f.write('/* Generated by tools/scene-extend/sprite_to_console.py -- do not edit. The generated\n'
                ' * (AI-drawn) parts of the WIDE scene sprites; the game\'s own art is not in it. */\n')
        f.write('#include "port_scene_sprites.h"\n\n')
        for arrays, _ in kept.values():
            f.write(arrays + '\n')
        for s in sprites:
            name = s['key'].replace('-', '_')
            for i, packed in enumerate(s['frames']):
                f.write(f'static const uint8_t kPacked_{name}_{i}[{len(packed)}] = {{\n')
                for j in range(0, len(packed), 24):
                    f.write('    ' + ', '.join(str(b) for b in packed[j:j + 24]) + ',\n')
                f.write('};\n\n')
            n = len(s['frames'])
            f.write(f'static const uint8_t* const kFrames_{name}[{n}] = {{ '
                    + ', '.join(f'kPacked_{name}_{i}' for i in range(n)) + ' };\n')
            f.write(f'static const size_t kSizes_{name}[{n}] = {{ '
                    + ', '.join(f'sizeof(kPacked_{name}_{i})' for i in range(n)) + ' };\n\n')
        f.write('const PortSceneSpriteDef gPortSceneSprites[] = {\n')
        for _, line in kept.values():
            f.write(line + '\n')
        for s in sprites:
            name = s['key'].replace('-', '_')
            dx, dy, w, h = s['canvas']
            tw, th = s['tex']
            f.write(f'    {{ "{s["key"]}", {s["mode"]}, {s["scene_key"]}, {dx}, {dy}, {w}, {h}, {tw}, {th}, '
                    f'{len(s["frames"])}, kFrames_{name}, kSizes_{name} }},\n')
        f.write('};\n')
        f.write(f'const int gPortSceneSpriteCount = {len(kept) + len(sprites)};\n')
    print(f'wrote {OUTPUT_C}')


if __name__ == '__main__':
    main()
