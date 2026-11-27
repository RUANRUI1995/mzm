"""PT-BR words for the ROM graphics that carry English text.

Read by tools/lang/gen_lang_gfx.py (see its docstring); run it after editing:

    python tools/lang/gen_lang_gfx.py --preview <dir>

Each graphic:
  id       the PortLangGfxId it is applied for (port/port_lang_gfx.h)
  symbol   the English graphic in the ROM
  slots    name -> ([first tile of each tile row], width in tiles): where a
           word is drawn (found from the screen's tilemap)
  fonts    name -> how to cut the letters out of the English words:
             sources  [(slot, English text)] whose letters are reused
             bg_from  (slot, x) of a background column (no ink)
             ink_rows rows of the slot that tell ink from background
             border   columns at the right end of a slot to keep as they are
             gap      columns between letters; space: width of a space
             own      letters the English words lack, as ASCII art over the
                      ink rows ('.' = background, others through palette)
  words    slot -> (font, PT-BR text[, options]); slots not listed keep the
           English word (NORMAL, SUPER, VARIA, PLASMA... read the same).

Accents are left out where a font is only 5 pixels tall: there is no room
above the capitals, and the game's own Spanish graphics do the same ("MISIL").
"""

GRAPHICS = {
    # Pause screen, status page: the equipment headers and item names
    # (pause_screen.c, sMapScreenEquipmentNamesGfxPointers, VRAM 0x6000).
    "equipment_names": {
        "id": "PORT_LANG_GFX_EQUIPMENT_NAMES",
        "symbol": "sEquipmentNamesEnglishGfx",
        "slots": {
            "ENERGY": ([0], 6),
            "BEAM": ([6], 5),
            "MISSILE": ([11], 6),
            "BOMB": ([17], 5),
            "SUIT": ([22], 6),
            "MISC": ([28], 6),
            "LONG": ([54], 5),
            "CHARGE": ([59], 5),
            "ICE": ([64], 5),
            "WAVE": ([69], 5),
            "PLASMA": ([74], 5),
            "MNORMAL": ([79], 5),
            "SUPER": ([84], 5),
            "BNORMAL": ([89], 5),
            "POWER": ([96], 4),
            "VARIA": ([100], 5),
            "GRAVITY": ([105], 7),
            "MORPH": ([112], 7),
            "GRIP": ([119], 7),
            "SPEED": ([128], 7),
            "HIJUMP": ([135], 6),
            "SCREW": ([141], 7),
            "SPACE": ([148], 7),
            "PISTOL": ([155], 5),
        },
        "fonts": {
            "item": {
                "sources": [("LONG", "LONG"), ("CHARGE", "CHARGE"), ("ICE", "ICE"), ("WAVE", "WAVE"),
                            ("PLASMA", "PLASMA"), ("MNORMAL", "NORMAL"), ("SUPER", "SUPER"),
                            ("POWER", "POWER"), ("VARIA", "VARIA"), ("GRAVITY", "GRAVITY"),
                            ("MORPH", "MORPH BALL"), ("GRIP", "POWER GRIP"), ("SPEED", "SPEED BOOSTER"),
                            ("HIJUMP", "HI-JUMP"), ("SCREW", "SCREW ATTACK"), ("SPACE", "SPACE JUMP"),
                            ("PISTOL", "PISTOL")],
                "bg_from": ("LONG", 30),
                "ink_rows": [2, 3, 4, 5, 6],
                "border": 1,
                "gap": 1,
                "space": 3,
                "own_top": 2,
                "palette": {"#": 6},
                "own": {
                    "F": ["####",
                          "#...",
                          "###.",
                          "#...",
                          "#..."],
                    ".": [".", ".", ".", ".", "#"],
                    "Q": ["####",
                          "#..#",
                          "#..#",
                          "#.##",
                          "####"],
                },
            },
            # ENERGY: the header font one row lower, between 2-column borders.
            "energy": {
                "sources": [("ENERGY", "ENERGY", [7, 7, 7, 7, 7, 7], 2)],
                "bg_from": ("ENERGY", 44),
                "ink_rows": [3, 4, 5, 6, 7],
                "border": 2,
                "gap": 0,
                "own_top": 3,
                "own": {
                    "I": ["117", "157", "157", "147", "147"],
                    # the last letter loses its shadow column to fit, as in
                    # the game's own Italian ENERGIA
                    "a": ["111111", "157815", "151115", "147814", "147814"],
                },
            },
            "header": {
                # The letters touch (a 3-pixel bevelled stroke, then the next
                # letter), so their widths are given.
                "sources": [("BEAM", "BEAM", [7, 7, 7, 9]), ("MISSILE", "MISSILE", [9, 3, 7, 7, 3, 7, 7]),
                            ("BOMB", "BOMB", [7, 7, 9, 7]), ("SUIT", "SUIT", [7, 7, 3, 7])],
                "bg_from": ("BEAM", 36),
                "ink_rows": [2, 3, 4, 5, 6],
                "gap": 0,
                "space": 3,
                "own_top": 2,
                # digits are palette indices, '.' the background
                "own": {
                    "R": ["1111178",
                          "1571578",
                          "1511517",
                          "1578157",
                          "1478147"],
                    "J": ["....117",
                          "....157",
                          "....157",
                          "117.147",
                          "1411147"],
                },
            },
        },
        "words": {
            "ENERGY": ("energy", "ENERGIa", {"x0": 2}),
            "BEAM": ("header", "RAIO"),
            "MISSILE": ("header", "MISSIL"),
            "BOMB": ("header", "BOMBA"),
            "SUIT": ("header", "TRAJE"),
            "MISC": ("header", "OUTROS"),
            "LONG": ("item", "LONGO"),
            "CHARGE": ("item", "CARGA"),
            "ICE": ("item", "GELO"),
            "WAVE": ("item", "ONDAS"),
            "POWER": ("item", "PODER"),
            "GRAVITY": ("item", "GRAVIDADE"),
            "MORPH": ("item", "BOLA MORFO"),
            "GRIP": ("item", "PEGADA FIRME"),
            "SPEED": ("item", "ACELERADOR"),
            "HIJUMP": ("item", "SUPERSALTO", {"border": 0}),
            "SCREW": ("item", "ATAQUE GIRAT."),
            "SPACE": ("item", "SALTO ESPAC."),
            "PISTOL": ("item", "PISTOLA"),
        },
    },
    # Pause screen top bar: "L SLEEP ... STATUS R" and the looping "CHANGE
    # AREA" banner (sMapScreenMenuNamesGfxPointers, VRAM 0xC00). STATUS stays.
    "menu_names": {
        "id": "PORT_LANG_GFX_MENU_NAMES",
        "symbol": "sMenuNamesEnglishGfx",
        "slots": {
            "BAR": ([0], 11),
            "TICKER": ([11], 11),
        },
        "fonts": {
            "menu": {
                "sources": [("BAR", "SLEEP", None, 8, 33), ("BAR", "STATUS", None, 48, 72)],
                "bg_from": ("BAR", 8),
                "ink_rows": [3, 4, 5, 6, 7],
                "rows": [3, 4, 5, 6, 7],
                "gap": 1,
                "own_top": 3,
                "palette": {"b": 0xB, "c": 0xC},
                "own": {"R": ["bbc", "b.b", "bbc", "b.b", "b.b"]},
            },
            # The banner repeats every 44 pixels (two copies in 88), so the
            # PT-BR text keeps that period: TROCAR AREA + spaces = 44.
            "ticker": {
                "sources": [("TICKER", "ANGE AREA CH", None, 0, 44)],
                "bg_from": ("TICKER", 0),
                "ink_rows": [1, 2, 3, 4, 5],
                "rows": [0, 1, 2, 3, 4, 5, 6],
                "gap": 1,
                "space": 3,
                "own_top": 1,
                "own": {
                    "T": ["334", ".3.", ".3.", ".3.", ".3."],
                    "O": ["334", "3.4", "3.4", "3.4", "334"],
                },
            },
        },
        "words": {
            "BAR@sleep": ("menu", "ESPERA", {"x0": 8, "x1": 34, "align": "center"}),
            "TICKER": ("ticker", "TROCAR AREA TROCAR AREA", {"x0": 1, "x1": 88}),
        },
    },
}
