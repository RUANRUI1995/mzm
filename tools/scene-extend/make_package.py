#!/usr/bin/env python3
"""Delivery packages for the generative AI: one folder per scene with the scene's background
picture in a bigger canvas and a README the AI reads (docs/3ds-wide-scene-extension-plan.md,
section 8b).

    make_package.py DUMPS_DIR OUT_DIR [SCENE ...]

DUMPS_DIR holds the screen dumps named in SCENES below (`<dir>/<n>-io.bin`, ...: the files
`mzm-dump-NN-*` of DEBUG -> SCREEN DUMP, renamed per capture). OUT_DIR gets `mzm-<scene>/`
for each scene (default: all of them) with:

  1-template-WxH.png        the canvas: the picture at its place, transparent everywhere else
  1-template-WxH-black.png  the same with black instead of transparent
  2-original-picture.png    the picture alone
  3-final-composite.png     what the player sees (every layer and sprite), at its place
  4-do-not-paint.png        magenta = picture; the other pixels are what the game draws over it
  README-AI.md              the task, the numbers and the rules

Coordinates are BG pixels of the scene's background BG (its own scroll space). A scene that
streams its graphics (the title) is put together from the rows each dump's frame shows, the
only ones VRAM is sure to hold the picture in.

All of it is the game's art: keep OUT_DIR out of the repository.
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gba_dump import Dump, backdrop, bg_frame, render_bg, render_sprites  # noqa: E402

# picture: BG rect (x0, y0, x1, y1) of the picture; canvas: BG rect of the canvas.
# dumps: captures to take the picture from; final: the one the composite shows.
# frame_only: take from each dump only what its frame shows (streamed scenes).
SCENES = {
    'closeup-landing': dict(
        name="Samus's close-up when she lands on Zebes (in-game cutscene, Brinstar's first room)",
        bg=0, dumps=['closeup/1'], final='closeup/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="A close-up of Samus in her Power Suit, filling the screen: the round red and orange "
              "helmet with its dark green visor, her face seen through it (eyes closed, calm), and her "
              "big yellow shoulder pads cut by the left and right edges. Behind her, a dark jungle of "
              "green alien plants. The canvas is the 400x240 screen with the picture in the middle.",
        sides=[
            "Left and right: her yellow shoulder pads and the red of her suit, which the original "
            "cuts at its edges, continue outwards naturally (same shapes, colours and shading), and "
            "past them more of the dark green jungle behind her. Do not add another helmet or face.",
            "Above: more of the dark jungle with green plants, darker towards the top edge.",
            "Below: her suit (chest and shoulders) continues down to the bottom edge of the canvas, "
            "in the same reds, oranges and yellows, a bit darker as it goes down.",
            "Same pixel-art look as the original: hard 1-pixel edges, the same limited colours, no "
            "soft gradients or glow.",
        ],
        over="nothing (the whole picture is background)"),
    'title': dict(
        name="Metroid: Zero Mission title screen",
        bg=3, dumps=['title/01', 'title/02', 'title/03'], final='title/03', frame_only=True,
        picture=(0, 0, 240, 360), canvas=(-80, -40, 320, 400),
        about="When the title appears the game scrolls this tall background down, from deep space "
              "(stars, a blue nebula, purple haze) to the rocky surface of planet Zebes at the bottom, "
              "where it stops and the logo shows. The canvas is the whole scroll with room around it, "
              "so that at the end the logo stays in the middle of the 400x240 screen.",
        sides=[
            "Left and right, along the whole height: more of the same space (stars of the same sizes "
            "and colours, the blue nebula and the purple haze continuing past the edges), and at the "
            "bottom more of the rocky ground, at the same height and with the same horizon line.",
            "Above: more deep space, darker, with stars.",
            "Below: the rocky ground goes on down to the bottom edge of the canvas (more rocks, a bit "
            "darker as it goes down). No horizon or sky down there.",
        ],
        over="the METROID ZERO MISSION logo, the menu text (JUGAR, Idioma, (c) 2004 Nintendo) and "
             "the sparkles: another layer and sprites the game draws over the background"),
    'intro-space': dict(
        name="intro, Samus's ship in deep space",
        bg=0, dumps=['intro-110/1'], final='intro-110/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="Deep space: a dark starfield with blue and purple nebulae. Samus's gunship flies "
              "towards the camera in the middle (a sprite, not part of the background).",
        sides=["All around: more of the starfield and nebulae, stars of the same sizes and "
               "colours; the nebulae continue past the edges and fade into black space."],
        over="Samus's gunship (sprites)"),
    'file-select': dict(
        name="file select (\"Datos de Samus\")",
        bg=3, dumps=['file-select/1'], final='file-select/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="Backdrop of the save-file menu: the face of a Chozo statue, its hand at the brow, "
              "in dark teal and green, over a wall of carved square spiral glyphs (a circuit-like "
              "pattern in the same teal); his shoulders and body fill the lower part. The whole picture is shaded with fine horizontal "
              "scanlines that belong to the art. The canvas is the 400x240 screen with the "
              "picture in the middle.",
        sides=[
            "Left and right: in the top half the wall of carved glyphs goes on (the same square "
            "spirals and channels, same size and spacing, same dark teal). Lower down, the "
            "statue's shoulders and rocky body, which the original cuts at its left and right edges "
            "and at the bottom, continue naturally outwards. Do not add another face.",
            "Above: more of the glyph wall. Below: the statue's body and the dark rocks under it "
            "go on down to the bottom edge, darker as they go down.",
            "Keep the horizontal scanlines of the original, same thickness and spacing, across "
            "the added areas.",
        ],
        over="the menu (DATOS DE SAMUS, the three save files with their times and areas, COPIAR / "
             "BORRAR / OPCIONES): another layer and sprites the game draws over the background"),
    'intro-cockpit': dict(
        name="intro, Samus in her ship's cockpit",
        bg=0, dumps=['intro-160/1'], final='intro-160/1',
        picture=(0, -8, 256, 168), canvas=(-72, -40, 328, 200),
        about="Samus in her orange Power Suit at the controls of her ship while it flies at great "
              "speed: the picture is full of long diagonal speed streaks in red, orange and yellow, "
              "sloping up to the right, with some dark metal hull at the right. The game shakes this "
              "picture by a pixel, so the canvas keeps its real size (256x176). What matters in the "
              "new area is the feeling of speed and motion, not the ship.",
        sides=[
            "Almost everything you add should be speed and motion: long, hard-edged diagonal streaks "
            "at the same slope, thickness and spacing as the original's, in the same reds, oranges "
            "and yellows, brighter and sparser away from Samus and darker in the shadows. They run "
            "off the edges of the canvas in the same direction, as if the whole scene were rushing "
            "past.",
            "Do not draw the ship or the cockpit in detail: no panels, controls, windows or seats. "
            "Where the original shows the dark hull at the right, let it fade into dark streaked "
            "shadow instead of adding more machinery. Where Samus's body or arm cannon is cut at "
            "the edge, continue only what is needed so the cut is invisible; no second Samus.",
            "Same pixel-art look as the original: hard 1-pixel edges, no soft glow, no sparks, "
            "no smooth gradients.",
        ],
        over="nothing (the whole picture is background)"),
    'intro-zebes': dict(
        name="intro, planet Zebes from orbit",
        bg=0, dumps=['intro-600/1'], final='intro-600/1',
        picture=(0, 0, 256, 160), canvas=(-88, -40, 328, 200),
        about="Planet Zebes seen from orbit: its yellow-green surface curves across the picture with "
              "a strip of starry space above the horizon. The game pans this picture 16 px to the "
              "left while Samus's ship dives towards the planet with a red flame trail (sprites), so "
              "the canvas is 16 px wider than the screen.",
        sides=[
            "Left and right: the planet's curve and surface continue (same colours, same curvature, "
            "so the horizon stays one smooth arc), with the space above it.",
            "Above: more starry space above the horizon. Below: more of the planet's surface.",
        ],
        over="Samus's ship and its red flame trail (sprites)"),
    'intro-motherbrain': dict(
        name="intro, Mother Brain's tank",
        bg=0, dumps=['intro-960/1'], final='intro-960/1',  # VRAM holds the whole map at once
        picture=(0, 0, 240, 256), canvas=(-80, 0, 320, 256),
        about="Mother Brain's glass tank in Tourian, all red: the tank and its rim at the top, the "
              "organic machinery, pipes and cables around it, the tank's base below. The game "
              "scrolls this picture up from its bottom to its top; it is as tall as the canvas, so "
              "only the sides are missing.",
        sides=["Left and right, along the whole height: more of the red machinery, pipes and "
               "cables of the room around the tank. Do not add another tank."],
        over="nothing (the whole picture is background)"),
    # Cutscenes 7, 10 and 11 (gCurrentCutscene). Dumps taken from a save state with the
    # cutscene poked in (docs/3ds-debug-tools.md, "Playing any cutscene"); each stage loads its
    # whole maps, so one dump holds the picture unless frame_only says otherwise. `bgs`: the
    # layers drawn together into the picture (they scroll as one); `see_through`: a front layer
    # whose empty parts are magenta (what is behind it shows there).
    'ridley-alert': dict(
        name="Ridley in space, red alert on the pirate mothership (cutscene 7, stage 2)",
        bg=3, bgs=[1, 3], dumps=['c07/s2a/1'], final='c07/s2a/1',
        picture=(0, 0, 240, 320), canvas=(-80, -40, 320, 360),
        about="Inside the space pirates' mothership during a red alert: a purple organic wall that "
              "curves away into the dark, covered with rows of oval red-and-pink alarm lights, and "
              "Ridley (black, skeletal, a long beak, red eyes) crouched on the right, gripping the "
              "wall with his claws, his long tail hanging down. The game pans this tall picture up "
              "from its bottom half to its top half, so the canvas is the whole height with room "
              "around it.",
        sides=[
            "Left and right, along the whole height: more of the curved purple wall with its rows "
            "of oval red lights, the same size and spacing, darker towards the edges. The "
            "original's right part is in deep shadow: keep that side dark, with only a few dim "
            "lights. Where Ridley's body is cut at the right edge, continue only what is needed so "
            "the cut is invisible; do not add another Ridley.",
            "Above: the top of the wall, darker. Below: the lower wall and lights, darker as they "
            "go down; Ridley's tail may continue a little if the original cuts it.",
        ],
        over="nothing (the whole picture is background)"),
    'ridley-suspicious': dict(
        name="Ridley in space, Ridley grows suspicious (cutscene 7, stage 3)",
        bg=3, bgs=[2, 3], dumps=['c07/s3/1'], final='c07/s3/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="Inside the mothership: a purple ribbed wall with a row of glowing red oval lights "
              "across the lower half, and Ridley's head and long neck in profile on the right "
              "(black skin with red edges, a long pointed beak, a red eye). The canvas is the "
              "400x240 screen with the picture in the middle.",
        sides=[
            "Left: more of the purple ribbed wall and the row of red oval lights, at the same "
            "height and spacing.",
            "Right: Ridley's neck and shoulders, cut by the right edge, continue naturally (same "
            "black skin, red edges and ribbing), with the wall behind him.",
            "Above: the upper part of the ribbed wall, darker. Below: the lower wall, darker, and "
            "Ridley's neck going down to the bottom edge.",
        ],
        over="nothing (the whole picture is background)"),
    'tourian-corridor': dict(
        name="entering Tourian, the corridor (cutscene 10, background layer)",
        bg=3, dumps=['c10/b/1'], final='c10/b/1',
        picture=(0, 0, 256, 160), canvas=(-80, -40, 404, 200),
        about="A dark metal corridor in Tourian seen in perspective: a row of big purple and grey "
              "cylinders along the back wall, a ribbed ceiling, and a bluish floor with a soft dark "
              "shadow in the middle. The game pans this picture slowly while the foreground pipes "
              "pass in front, so the canvas is wider than the screen: 80 px to the left, 148 px to "
              "the right, 40 px above and below.",
        sides=[
            "Left and right: the corridor goes on: more of the same cylinders along the back wall, "
            "the same ceiling and the same floor, at the same heights and perspective, so the floor "
            "line and the ceiling line stay straight across the whole canvas.",
            "Above: more of the ribbed ceiling, darker. Below: more of the bluish floor plating, "
            "darker towards the bottom edge.",
        ],
        over="the pipes in front (another layer) and the Metroids and the dead space pirate "
             "(sprites)"),
    'tourian-pipes': dict(
        name="entering Tourian, the pipes in the foreground (cutscene 10, front layer)",
        bg=1, dumps=['c10/b/1'], final='c10/b/1', see_through=True, paint_x=(0, 256),
        picture=(0, 0, 256, 160), canvas=(-80, -40, 336, 200),
        amounts="Only two bands: 40 px above and 40 px below the original, exactly as wide as the "
                "original (x=80..335 in the canvas). The canvas's left and right parts are already "
                "magenta (see-through in the game): leave them as they are.",
        about="A FOREGROUND layer, not a full picture: thick grey-blue metal pipes with bolts and "
              "rings seen up close (vertical pipes on the left, a big pipe curving across the "
              "bottom and up on the right) and a dark area at the top right that is part of the "
              "layer. MAGENTA (255,0,255) means see-through: there the game shows the corridor "
              "behind, which is another layer. The game slides this layer sideways in front of the "
              "corridor, so only its top and bottom are missing.",
        sides=[
            "Above: the vertical pipes continue straight up to the top edge of the canvas, the "
            "same width, colours, rings and bolts; the dark area at the top right continues up "
            "too, as dark as it is.",
            "Below: the big curved pipe and the pipes at the bottom continue down to the bottom "
            "edge, the same thickness and shading.",
            "Inside those two bands, wherever there is no pipe and no dark area (for example "
            "where the original shows magenta at its top or bottom edge), paint MAGENTA "
            "(255,0,255) exactly, no other colour. Hard edges against the magenta: no "
            "anti-aliasing or shadows into it.",
            "The pure green (0,255,0) in the -green template only marks the two bands to paint; "
            "no green may be left in the result.",
        ],
        over="the Metroids and the dead space pirate (sprites)"),
    'ruins-wall-left': dict(
        name="before the ruins test, Chozo wall painting, first view (cutscene 11, stage 1)",
        bg=3, dumps=['c11/s1a/1'], final='c11/s1a/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="An ancient Chozo wall painting on cracked golden-brown plaster: two bird-headed "
              "Chozo figures in teal and orange standing in profile, framed by red bands and "
              "red-and-gold panels at the top and bottom. The canvas is the 400x240 screen with the "
              "picture in the middle.",
        sides=[
            "Left and right: more of the same wall painting: the cracked golden plaster, the red "
            "frame bands and the panels at the top and bottom continue. A third Chozo figure may "
            "appear only partly at an edge, in the same style; no text.",
            "Above and below: more rows of the red-and-gold panels and the plaster, a bit darker "
            "towards the edges.",
        ],
        over="nothing (the whole picture is background)"),
    'ruins-wall-right': dict(
        name="before the ruins test, Chozo wall painting, second view (cutscene 11, stage 1)",
        bg=2, dumps=['c11/s1b/1'], final='c11/s1b/1',
        picture=(256, 0, 496, 160), canvas=(176, -40, 576, 200),
        about="An ancient Chozo wall painting on cracked golden-brown plaster: two bird-headed "
              "Chozo figures in teal and white kneeling in profile, each holding up a glowing "
              "golden orb, with red bands and panels around them. The canvas is the 400x240 screen "
              "with the picture in the middle.",
        sides=[
            "Left and right: more of the same wall painting: plaster, red bands and panels "
            "continue; another kneeling figure may appear only partly at an edge, in the same "
            "style; no text.",
            "Above and below: more rows of the red-and-gold panels and the plaster, a bit darker "
            "towards the edges.",
        ],
        over="nothing (the whole picture is background)"),
    'ruins-wall-rise': dict(
        name="before the ruins test, the Chozo wall the view travels down (cutscene 11, stage 2)",
        bg=3, bgs=[2, 3], dumps=['c11/s2a/1'], final='c11/s2a/1',  # before the grey ramp; the map wraps
        picture=(0, 0, 240, 320), canvas=(-80, -40, 320, 360),
        about="A huge carved Chozo wall in the ruins, in faded colours: a big oval frame with a "
              "Chozo statue in orange holding a white glowing orb, beams of light going down, round "
              "blue glyphs and small Chozo figures on both sides, and rubble at the foot of the "
              "wall. At the bottom of the picture, in grey, a small child and a tall cloaked figure "
              "walk towards the wall in a misty light. The game moves the view down this tall "
              "picture, so the canvas is the whole height with room around it. During the scene "
              "the game turns every colour grey, so shapes matter more than colours.",
        sides=[
            "Left and right, along the whole height: more of the carved wall: the round blue "
            "glyphs, small Chozo figures and carved bands continue at the same sizes and spacing, "
            "darker towards the edges. Do not add a second oval frame or statue.",
            "Above: the top of the wall, darker. Below: more of the misty grey ground and rubble "
            "where the two figures walk; no more figures.",
        ],
        over="nothing (the whole picture is background)"),
    'ruins-greyvoice': dict(
        name="before the ruins test, young Samus and Grey Voice (cutscene 11, stage 2)",
        bg=1, dumps=['c11/s2c/1'], final='c11/s2c/1',
        picture=(0, 0, 320, 160), canvas=(-80, -40, 400, 200),
        about="A grey pencil-like drawing: young Samus (a child with short hair) on the left holds "
              "the hand of Grey Voice, a tall old Chozo in a heavy hooded cloak, on the right; soft "
              "rays of light come from the upper right. The game fades this picture in while "
              "panning it, so the canvas is 80 px wider than the 320 px picture on each side, plus "
              "40 px above and below.",
        sides=[
            "Left: more of the soft grey background and light; the child's back and arm continue "
            "if the edge cuts them.",
            "Right: Grey Voice's cloak continues where the edge cuts it, then more of the soft grey "
            "light and rays.",
            "Above: the top of Grey Voice's hood continues if cut, and more soft light. Below: the "
            "child's and the cloak's lower parts continue down, the same sketchy grey shading.",
        ],
        over="nothing (the whole picture is this drawing)"),
    'ruins-young': dict(
        name="before the ruins test, young Samus's face (cutscene 11, stage 3)",
        bg=0, dumps=['c11/s3a/1'], final='c11/s3a/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="A close-up of young Samus (a child) drawn in grey pencil on a white, slightly "
              "speckled background: her messy short hair, big eyes, and the collar of her shirt at "
              "the bottom. The game dissolves this picture into adult Samus (another picture of the "
              "same size). The canvas is the 400x240 screen with the picture in the middle.",
        sides=[
            "Left and right: more of the white speckled background, with the ends of her hair "
            "strands where the edges cut them.",
            "Above: the top of her hair continues, sketchy grey strands, then white. Below: her "
            "shoulders and shirt continue down to the bottom edge in the same grey pencil "
            "shading.",
        ],
        over="nothing (the whole picture is background)"),
    'ruins-adult': dict(
        name="before the ruins test, adult Samus's face (cutscene 11, stage 3)",
        bg=3, bgs=[2, 3], dumps=['c11/s3a/1'], final='c11/s3a/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="A close-up of adult Samus with her eyes closed: long golden-blonde hair framing her "
              "face, red lips, the blue collar of her Zero Suit at the bottom, in front of a white "
              "background with a soft mint-green glow around her silhouette. The canvas is the "
              "400x240 screen with the picture in the middle.",
        sides=[
            "Left and right: her long blonde hair continues outwards and down where the edges cut "
            "it (same yellows, orange shading and black outlines), then the mint-green glow and "
            "the white background.",
            "Above: the top of her head and hair, then the green glow and white. Below: her "
            "shoulders and the blue suit continue down to the bottom edge, the same blues and "
            "outlines.",
        ],
        over="nothing (the whole picture is background)"),
    # When she opens her eyes the game fades in another drawing of her (BG1 over BG2, its hair
    # and shoulders drawn differently too), so its margins are a picture of their own, drawn and
    # blended with BG1.
    'ruins-adult-open': dict(
        name="before the ruins test, adult Samus opens her eyes (cutscene 11, stage 3)",
        bg=1, bgs=[1, 3], dumps=['c11/s3b/1'], final='c11/s3b/1',
        picture=(0, 0, 240, 160), canvas=(-80, -40, 320, 200),
        about="A close-up of adult Samus with her eyes open (blue-green eyes, a determined look): "
              "long golden-blonde hair framing her face, red lips, the blue collar of her Zero Suit at "
              "the bottom, in front of a white background with a soft mint-green glow around her "
              "silhouette. The canvas is the 400x240 screen with the picture in the middle.",
        sides=[
            "Left and right: her long blonde hair continues outwards and down where the edges cut "
            "it (same yellows, orange shading and black outlines), then the mint-green glow and "
            "the white background.",
            "Above: the top of her head and hair, then the green glow and white. Below: her "
            "shoulders and the blue suit continue down to the bottom edge, the same blues and "
            "outlines.",
        ],
        over="nothing (the whole picture is background)"),
}

README = """# Extend a Game Boy Advance background: {name}

Pixel-art background from Metroid: Zero Mission (Game Boy Advance, 1 pixel = 1 game pixel). It
has to be made bigger by drawing the missing area around it: the game is shown on a wider screen
(400x240 instead of the GBA's 240x160) and the new area must look like it was always part of the
picture.

{about}

## Important: continue the picture, do not frame it
This is outpainting: the result is ONE continuous picture that fills the whole canvas from edge to
edge, in which the original is just the middle part. Do NOT draw a border, frame, black outline,
shadow, gap or "picture inside a picture" around the original. Do NOT shrink, copy, tile or mirror
the original into the new area. Whatever crosses the original's edge (lines, streaks, shapes,
colours) has to carry on across it with no break, and the area next to the edge must look like
the same drawing, not like a different one.

## Files
- `1-template-{W}x{H}.png`: **the canvas to fill.** The original is at x={X}..{X2}, y={Y}..{Y2};
  everything else is transparent and is what you must draw. `1-template-{W}x{H}-{fill_name}.png` is
  the same canvas with {fill_name} instead of transparent, if your tool prefers it: the {fill_name} is
  not part of the picture, it is the area to fill (do not keep it as a frame).
- `1-template-{W}x{H}-extended.png`: **start from this one if the others make you draw a frame.** The
  new area is only the original's edge pixels stretched outwards, a rough hint of where the lines
  and colours continue. Repaint all of it as real detail; the original stays as it is.
- `2-original-picture.png`: the original alone ({w}x{h}), for reference.
- `3-final-composite.png`: what the player sees (the background plus everything the game draws on
  top), at its place in the canvas. Context only.
- `4-do-not-paint.png`: magenta is the background; the other pixels are {over}. Do not paint them.

## What to add
{amounts}

{sides}

## Rules
1. Do not change a single pixel inside the original rectangle. It is the game's own art and is
   drawn over yours; any change shows as a seam.
2. Use only colours that already appear in the original ({colours} colours). No anti-aliasing,
   gradients, blur, dithering the original does not use, or new hues.
3. Same pixel scale and style: hard 1-pixel edges, the same outlines and shading steps.
4. Continue what touches the original's edges so the seams are invisible. Add more of the same
   kind of elements; no new objects, characters or text.
5. Do not repeat or mirror the original.
6. Do not paint anything from `3-final-composite.png` or `4-do-not-paint.png` that is not
   background ({over}).
7. Output: a PNG exactly {W}x{H}, no transparency left, no scaling, no smoothing. Give 2 or 3
   variants as separate files.
"""


MAGENTA = (255, 0, 255, 255)


def layers_back_to_front(d, bgs):
    """The enabled BGs of `bgs` in the order the GBA stacks them (priority 3 at the back; on a
    tie the higher BG number is behind)."""
    return sorted((b for b in bgs if d.bg_enabled(b)), key=lambda b: (-(d.bg_cnt(b) & 3), -b))


def pixel(img, x, y):
    return img.getpixel((x % img.width, y % img.height))


def build(scene, sc, dumps_dir, out_dir):
    cx0, cy0, cx1, cy1 = sc['canvas']
    px0, py0, px1, py1 = sc['picture']
    W, H, w, h = cx1 - cx0, cy1 - cy0, px1 - px0, py1 - py0
    bg = sc['bg']

    # The picture, over the backdrop colour (what the GBA shows where the BG is transparent).
    pic = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    got = Image.new('1', (w, h), 0)
    layers = sc.get('bgs', [bg])
    see_through = sc.get('see_through', False)
    for name in sc['dumps']:
        d = Dump(os.path.join(dumps_dir, name))
        ox, oy = d.bg_scroll(bg)
        bw, bh = d.bg_size(bg)
        # Magenta where a see-through layer shows what is behind it; else the backdrop.
        base = Image.new('RGBA', (bw, bh), MAGENTA if see_through else backdrop(d))
        for b in layers_back_to_front(d, layers):
            if d.bg_scroll(b) != (ox, oy) or d.bg_size(b) != (bw, bh):
                sys.exit(f'{scene}: BG{b} does not scroll with BG{bg} in {name}')
            base.alpha_composite(render_bg(d, b))
        for y in range(h):
            for x in range(w):
                bx, by = px0 + x, py0 + y
                if sc.get('frame_only') and ((bx - ox) % bw >= 240 or (by - oy) % bh >= 160):
                    continue
                pic.putpixel((x, y), pixel(base, bx, by))
                got.putpixel((x, y), 1)
    missing = sum(1 for v in got.get_flattened_data() if not v)
    if missing:
        print(f'{scene}: {missing} picture pixels no dump showed (left transparent)')

    # The final frame as the player sees it, and the layers over the picture.
    d = Dump(os.path.join(dumps_dir, sc['final']))
    ox, oy = d.bg_scroll(bg)
    # The frame's top-left in BG coords: the copy (the BG wraps) whose centre is nearest the
    # picture's centre.
    bw, bh = d.bg_size(bg)
    fx = ox + round(((px0 + px1) / 2 - (ox + 120)) / bw) * bw
    fy = oy + round(((py0 + py1) / 2 - (oy + 80)) / bh) * bh
    at = (fx - cx0, fy - cy0)
    frame = Image.new('RGBA', (240, 160), backdrop(d))
    over = Image.new('RGBA', (240, 160), (0, 0, 0, 0))
    cnts = [(d.bg_cnt(b) & 3, -b, b) for b in range(4) if d.bg_enabled(b) and (d.dispcnt & 7) == 0]
    for _, _, b in sorted(cnts, reverse=True):
        layer = bg_frame(d, b)
        frame.alpha_composite(layer)
        if b not in layers:
            over.alpha_composite(layer)
    sprites = render_sprites(d)
    frame.alpha_composite(sprites)
    over.alpha_composite(sprites)

    folder = os.path.join(out_dir, f'mzm-{scene}')
    os.makedirs(folder, exist_ok=True)
    # paint_x: the only BG columns where a see-through layer gets new pixels; the canvas's other
    # columns are already see-through in the game (magenta in every template, nothing to draw).
    preset = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    if sc.get('paint_x'):
        ax0, ax1 = sc['paint_x']
        preset.paste(MAGENTA, (0, 0, max(0, ax0 - cx0), H))
        preset.paste(MAGENTA, (min(W, ax1 - cx0), 0, W, H))
    template = preset.copy()
    template.alpha_composite(pic, (px0 - cx0, py0 - cy0))
    template.save(os.path.join(folder, f'1-template-{W}x{H}.png'))
    ext = np.pad(np.array(pic.convert('RGB')), ((py0 - cy0, H - (py0 - cy0) - h),
                                                (px0 - cx0, W - (px0 - cx0) - w), (0, 0)), mode='edge')
    ext_img = Image.fromarray(ext).convert('RGBA')
    ext_img.alpha_composite(preset)
    ext_img.convert('RGB').save(os.path.join(folder, f'1-template-{W}x{H}-extended.png'))
    # The area to fill shown in a solid colour: black, or pure green for a layer whose own art is
    # partly black (the pipes), where black would read as part of the picture.
    fill, fill_name = ((0, 255, 0, 255), 'green') if see_through else ((0, 0, 0, 255), 'black')
    solid = Image.new('RGBA', (W, H), fill)
    solid.alpha_composite(preset)
    solid.alpha_composite(pic, (px0 - cx0, py0 - cy0))
    solid.convert('RGB').save(os.path.join(folder, f'1-template-{W}x{H}-{fill_name}.png'))
    pic.save(os.path.join(folder, '2-original-picture.png'))
    comp = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    comp.alpha_composite(pic, (px0 - cx0, py0 - cy0))
    comp.alpha_composite(frame, at)
    comp.save(os.path.join(folder, '3-final-composite.png'))
    dnp = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    dnp.paste(Image.new('RGBA', (w, h), (255, 0, 255, 255)), (px0 - cx0, py0 - cy0))
    dnp.alpha_composite(over, at)
    dnp.save(os.path.join(folder, '4-do-not-paint.png'))

    X, Y = px0 - cx0, py0 - cy0
    left, right, top, bottom = X, W - X - w, Y, H - Y - h
    parts = [f'{n} px {side}' for n, side in ((left, 'to the left'), (right, 'to the right'),
                                               (top, 'above'), (bottom, 'below')) if n > 0]
    colours = len({c for c in pic.get_flattened_data() if c[3] and c != MAGENTA})
    with open(os.path.join(folder, 'README-AI.md'), 'w') as f:
        f.write(README.format(
            name=sc['name'], about=sc['about'], fill_name=fill_name, W=W, H=H, X=X, X2=X + w - 1, Y=Y, Y2=Y + h - 1,
            w=w, h=h, over=sc['over'], colours=colours,
            amounts=sc.get('amounts') or 'Canvas {}x{}, original {}x{}: add {}.'.format(W, H, w, h, ', '.join(parts)),
            sides='\n'.join(f'- {s}' for s in sc['sides'])))
    print(f'{scene}: {folder} ({W}x{H}, picture {w}x{h} at {X},{Y}, {colours} colours)')


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    dumps_dir, out_dir = sys.argv[1], sys.argv[2]
    for scene in sys.argv[3:] or SCENES:
        build(scene, SCENES[scene], dumps_dir, out_dir)


if __name__ == '__main__':
    main()
