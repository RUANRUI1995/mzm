"""PT-BR words for the ROM graphics that carry English text (EU ROM: port
languages are offered with it only, see port/port_lang.h).

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
    # Pause map: names of the items the suit cannot identify yet
    # (sMapScreenUnknownItemsNamesGfxPointers, OBJ VRAM 0x11400). PLASMA stays.
    "unknown_items": {
        "id": "PORT_LANG_GFX_UNKNOWN_ITEMS",
        "symbol": "sMapScreenUnknownItemsNamesEnglishGfx",
        "slots": {"STRIP": ([0], 20)},
        "fonts": {
            "unk": {
                "sources": [("STRIP", "PLASMA", None, 7, 47), ("STRIP", "SPACE JUMP", None, 48, 103),
                            ("STRIP", "GRAVITY", None, 104, 159)],
                "bg_from": ("STRIP", 42),
                "ink_rows": [2, 3, 4, 5, 6],
                "rows": [2, 3, 4, 5, 6],
                "gap": 1,
                "space": 3,
                "own_top": 2,
                "own": {
                    "O": ["3333", "3..3", "3..3", "3..3", "3333"],
                    "D": ["333.", "3..3", "3..3", "3..3", "333."],
                    ".": [".", ".", ".", ".", "3"],
                },
            },
        },
        "words": {
            "STRIP@space": ("unk", "SALTO ESPAC.", {"x0": 48, "x1": 103}),
            "STRIP@gravity": ("unk", "GRAVIDADE", {"x0": 104, "x1": 159}),
        },
    },
    # File select: SAMUS DATA, OPTIONS, COPY, ERASE in the big 16-pixel font
    # (file_select.c, sFileSelectLargeTextGfxPointers, VRAM 0x4800). Each word
    # has its own colours (indices); letters taken from another word go
    # through a colour map learnt from the letters both share.
    "file_large": {
        "id": "PORT_LANG_GFX_FILE_SELECT_LARGE",
        "symbol": "sFileSelectLargeTextEnglishGfx",
        "slots": {
            "SAMUS": ([0, 32], 20),
            "OPTIONS": ([20, 52], 12),
            "COPY": ([64, 96], 10),
            "ERASE": ([74, 106], 11),
        },
        "fonts": {
            "large": {
                # in DATA the T touches its neighbours, so SAMUS DATA is given by hand
                "sources": [("SAMUS", "SAMUS DATA", [(34, 8), (43, 9), (53, 10), (64, 9), (74, 8), (88, 9), (98, 9),
                                                     (107, 10), (117, 9)]),
                            ("OPTIONS", "OPTIONS"), ("COPY", "COPY", [(0, 9), (10, 9), (20, 9), (29, 10)]),
                            ("ERASE", "ERASE")],
                "bg_from": ("SAMUS", 2),
                "gap": 1,
                "space": 6,
                "recolor": True,
                # SAMUS DATA is drawn one row higher with its own shapes: its
                # colours do not map onto the others
                "no_recolor_from": ["SAMUS"],
                # accented letters: base letter + [(x, y, index)] over it,
                # indices of the OPTIONS colours (fill d, shadow 8)
                "accent_colors": "OPTIONS",
                # SAMUS DATA has its own shapes and a row gradient; its O and
                # E are drawn here (as o and e, so the other words' O and E
                # are not used for it)
                "own": {
                    "o": [".5666667.", "544444448", "633888338", "6338.7338", "6228.7228", "6228.7228",
                          "6118.7118", "6228.7228", "6228.7228", "6338.7338", "633877338", "744444448",
                          ".7888888."],
                    "e": [".5666667", "54444448", "63388888", "6338....", "6228....", "62287778", "61111118",
                          "62288888", "6228....", "6338....", "63386667", "64444448", "7888888."],
                },
                "accents": {
                    "Ç": ("C", [(3, 14, 0xD), (4, 14, 0x8), (2, 15, 0xD), (3, 15, 0x8)]),
                    "Õ": ("O", [(3, 0, 0xD), (4, 0, 0xD), (7, 0, 0xD), (2, 1, 0xD), (5, 1, 0xD), (6, 1, 0xD)]),
                    # G: the C with a bar into its lower hook
                    "G": ("C", [(4, 8, 0x5), (5, 8, 0x6), (6, 8, 0x6), (7, 8, 0x7),
                                (4, 9, 0xD), (5, 9, 0xD), (6, 9, 0xD), (7, 9, 0xD), (8, 9, 0x8)]),
                },
            },
        },
        "words": {
            "SAMUS": ("large", "DADoS De SAMUS", {"x0": 0, "x1": 160, "align": "center"}),
            "OPTIONS": ("large", "OPÇÕES"),
            "COPY": ("large", "COPIAR"),
            "ERASE": ("large", "APAGAR"),
        },
    },
    # File select: EASY NORMAL HARD TIME ATTACK (file_select.c,
    # sFileSelectDifficultyTextGfxPointers, VRAM 0x3400). An outlined font
    # whose letters share their outline column; NORMAL and TIME ATTACK stay
    # (the game's Spanish keeps TIME ATTACK too).
    "file_difficulty": {
        "id": "PORT_LANG_GFX_FILE_SELECT_DIFFICULTY",
        "symbol": "sFileSelectDifficultyTextEnglishGfx",
        "slots": {"STRIP": ([0], 20)},
        "fonts": {
            "diff": {
                "sources": [("STRIP", "EASY", None, 0, 40), ("STRIP", "NORMAL", None, 40, 80),
                            ("STRIP", "HARD", None, 80, 120), ("STRIP", "TI", None, 120, 127)],
                "bg_from": ("STRIP", 30),
                "ink": [1],
                "ink_rows": [1, 2, 3, 4, 5, 6, 7],
                "pad": 1,
                "gap": -1,
                "own_top": 1,
                "own": {"F": ["666666", "611116", "616666", "61116.", "6166..", "616...", "666..."],
                        "C": ["666666", "611116", "616666", "616...", "616666", "611116", "666666"]},
                # the acute accent the game's Spanish uses (FACIL, DIFICIL)
                "accents": {
                    "Á": ("A", [(1, 0, 6), (2, 0, 6), (3, 0, 1), (4, 0, 6), (2, 1, 1)]),
                    "Í": ("I", [(0, 0, 6), (1, 0, 6), (2, 0, 1), (3, 0, 6), (1, 1, 1)]),
                },
            },
        },
        "words": {
            "STRIP@easy": ("diff", "FÁCIL", {"x0": 0, "x1": 40}),
            "STRIP@hard": ("diff", "DIFÍCIL", {"x0": 80, "x1": 120}),
        },
    },
    # File select OPTIONS menu entries (file_select.c,
    # sFileSelectOptionsTextGfxPointers, VRAM 0xC00; all regions). White
    # letters (4) with a shared outline (1) on 0.
    "file_options": {
        "id": "PORT_LANG_GFX_FILE_SELECT_OPTIONS",
        "symbol": "sFileSelectOptionsTextEnglishGfx",
        "slots": {
            "SOUND": ([0, 32], 5),
            "SOUNDTEST": ([5, 37], 11),
            "TAR": ([16, 48], 16),
            "GALLERY": ([64, 96], 16),
            "MFLINK": ([80, 112], 16),
            "MFGALLERY": ([128, 160], 16),
            "ORIGINAL": ([144, 176], 16),
        },
        "fonts": {
            "opt": {
                "sources": [("SOUND", "SOUND"), ("SOUNDTEST", "SOUND TEST"), ("TAR", "TIME ATTACK RECORDS"),
                            ("GALLERY", "GALLERY"), ("MFLINK", "METROID FUSION LINK"),
                            ("MFGALLERY", "METROID FUSION GALLERY"), ("ORIGINAL", "ORIGINAL METROID")],
                "bg_from": ("SOUND", 39),
                "ink": [4],
                "ink_rows": [5, 6, 7, 8, 9, 10, 11],
                "pad": 1,
                "gap": -1,
                "space": 4,
                "auto_outline": 1,
                "own_top": 5,
                "own": {"X": ["4...4", "4...4", ".4.4.", "..4..", ".4.4.", "4...4", "4...4"],
                        "P": ["4444", "4..4", "4..4", "4444", "4...", "4...", "4..."]},
                # (x, top row, ink art): the outline is added around it
                "accents": {"Ã": ("A", (1, 2, ["44.4", "4.44"]))},
            },
        },
        "words": {
            "SOUND": ("opt", "SOM", {"x0": 0}),
            "SOUNDTEST": ("opt", "TESTE DE SOM", {"x0": 0}),
            "TAR": ("opt", "RECORDES DE TEMPO", {"x0": 0}),
            "GALLERY": ("opt", "GALERIA", {"x0": 0}),
            "MFLINK": ("opt", "CONEXÃO METROID FUSION", {"x0": 0}),
            "MFGALLERY": ("opt", "GALERIA METROID FUSION", {"x0": 0}),
            "ORIGINAL": ("opt", "METROID ORIGINAL", {"x0": 0}),
        },
    },
    # Title screen menu: "START GAME" and "Language" (title_screen.c,
    # sTitleScreenMenuGfxPointers; EU only). The top and bottom halves of the
    # letters are two graphics, loaded at VRAM 0xE800 and 0xEC00. The EU ROM
    # has the other languages' versions, so JOGAR takes its J and O from the
    # French JOUER, and Idioma is the Spanish word as it is.
    "title_menu": {
        "id": "PORT_LANG_GFX_TITLE_MENU",
        "parts": [("sTitleScreenEnglishMenuGfx_Top", 0), ("sTitleScreenEnglishMenuGfx_Bottom", 0x400)],
        "headers": ["data/menus/title_screen_data.h"],
        "extra": {
            "ES": [("sTitleScreenSpanishMenuGfx_Top", 0), ("sTitleScreenSpanishMenuGfx_Bottom", 0x400)],
            "FR": [("sTitleScreenFrenchMenuGfx_Top", 0), ("sTitleScreenFrenchMenuGfx_Bottom", 0x400)],
        },
        "slots": {
            "MENU": ([0, 32], 20),
            "ES_MENU": ([0, 32], 20, "ES"),
            "FR_MENU": ([0, 32], 20, "FR"),
        },
        "fonts": {
            "menu": {
                # letters: 6 columns of ink (6, 7, 1) between shared shading
                # columns; in START the T and A touch, so it is given by hand
                "sources": [("MENU", "START GAME", [(12, 8), (19, 8), (25, 8), (32, 8), (39, 8), (53, 8), (60, 8),
                                                    (67, 9), (75, 8)]),
                            ("FR_MENU", "JOUER", None, 0, 95),
                            # the whole Spanish word Idioma, as one "letter"
                            ("ES_MENU", "W", [(109, 38)])],
                "bg_from": ("MENU", 90),
                "ink": [6, 7, 1],
                "pad": 1,
                "gap": -1,
            },
        },
        "words": {
            "MENU@start": ("menu", "JOGAR", {"x0": 12, "x1": 84, "align": "center"}),
            "MENU@language": ("menu", "W", {"x0": 96, "x1": 160, "align": "center"}),
        },
    },
    # GAME OVER prompt: TRY AGAIN? / YES (RETURN TO GAME) / NO (TO TITLE
    # SCREEN) (game_over.c, sGameOverTextPromptGfxPointers, VRAM 0xA800; all
    # regions). Three 16-pixel lines; the first in its own colours. The
    # answers keep the English left edge, where the cursor points.
    "game_over": {
        "id": "PORT_LANG_GFX_GAME_OVER",
        "symbol": "sGameOverTextPromptEnglishGfx",
        "slots": {
            "L1": ([0, 32], 32),
            "L2": ([64, 96], 32),
            "L3": ([128, 160], 32),
        },
        "fonts": {
            "go": {
                # in GAME the A and M touch, so line 2 is given by hand
                "sources": [("L1", "TRY AGAIN?", None, 0, 256),
                            ("L2", "YES (RETURN TO GAME)", [(48, 9), (56, 8), (64, 8), (80, 5), (88, 8), (96, 8),
                                                            (104, 9), (112, 8), (120, 8), (128, 8), (144, 9),
                                                            (152, 8), (168, 8), (176, 9), (183, 9), (192, 8),
                                                            (202, 5)]),
                            ("L3", "NO (TO TITLE SCREEN)", None, 0, 256)],
                "bg_from": ("L1", 10),
                "ink": [2, 3, 4, 7, 8, 9],
                "pad": 1,
                "gap": -1,
                "space": 8,
                "recolor": True,
                "color_groups": [["L2", "L3"]],
                "own_top": 3,
                # D and V in the first line's colours (ink 2/3/4, outline 5/6)
                "own": {
                    "D": ["5555555.", "52222255", "52555525", "535..535", "535..535", "535..535", "545..545",
                          "545..545", "646.6646", "64444466", "6666666."],
                    "V": ["555...555", "525...525", "525...525", "535...535", "535...535", "5535.5355",
                          ".545.545.", ".5545455.", "..64646..", "..66466..", "...666..."],
                    # E and O of the first line (written e and o in it): the
                    # other lines' E and O do not recolour cleanly
                    "e": ["5555555", "5222225", "5255555", "535....", "5355555", "5333335", "5455555",
                          "545....", "6466666", "6444446", "6666666"],
                    "o": [".555555.", "55222255", "52555525", "535..535", "535..535", "535..535", "545..545",
                          "545..545", "64666646", "66444466", ".666666."],
                },
                # the tilde gets an outline; D and V are drawn with theirs
                "auto_outline": 0xA,
                "outline_own": False,
                "accents": {"Ã": ("A", (2, 1, ["77.7", "7.77"]))},
            },
        },
        "words": {
            "L1": ("go", "TeNTAR De NoVo?", {"x0": 0, "x1": 256, "align": "center"}),
            "L2": ("go", "SIM (CONTINUAR)", {"x0": 48}),
            "L3": ("go", "NÃO (SAIR)", {"x0": 48}),
        },
    },
    # Ending statistics, the lines the game shows once the letters have
    # dropped in (sEndingImageText*Gfx at OBJ tile 0x80; drawn by the
    # sEndingImageOam_Line* sprites): CLEAR TIME, YOUR RATE FOR, COLLECTING
    # ITEMS IS. Spelt with the alphabet the same graphic carries for the
    # dropping letters (port_lang_ending.c spells those the same way).
    "ending_text": {
        "id": "PORT_LANG_GFX_ENDING_TEXT",
        "symbol": "sEndingImageTextEnglishGfx",
        "headers": ["data/ending_and_gallery_data.h"],
        # the Spanish and Italian versions lend the letters of "See you next
        # mission!" the English one lacks
        "extra": {
            "ES": [("sEndingImageTextSpanishGfx", 0)],
            "IT": [("sEndingImageTextItalianGfx", 0)],
        },
        "slots": {
            "NEXT": ([256, 288], 18),
            "ES_NEXT": ([256, 288], 32, "ES"),
            "IT_NEXT": ([320, 352], 32, "IT"),
            "CT": ([0, 32], 12),
            "YR": ([12, 44], 16),
            "COL": ([64, 96], 21),
            "A1": ([128, 160], 32),
            "A2": ([192, 224], 32),
        },
        "fonts": {
            "end": {
                "sources": [("A1", "ABCDEFGHIJLMNOPR", None, 0, 256), ("A2", "STVUXY", None, 0, 96)],
                "bg_from": ("CT", 94),
                "gap": -1,
                "space": 6,
                # the I is a single stroke: the next letter's outline would
                # cover its right edge
                "after": {"I": 1},
            },
            # "See you next mission!": a cursive font, ink 1/2 in outline 3
            "cursive": {
                "sources": [("NEXT", "See you next mission!", None, 0, 144),
                            ("ES_NEXT", "¡Hasta la próxima misión!", None, 0, 170),
                            ("IT_NEXT", "A", [(0, 10)])],
                "bg_from": ("NEXT", 143),
                "ink": [1, 2],
                "pad": 1,
                "gap": -1,
                "space": 4,
                "auto_outline": 3,
                "accents": {
                    "é": ("e", (2, 3, [".1", "1."])),
                    "ã": ("a", (1, 3, ["11.1", "1.11"])),
                },
            },
        },
        "words": {
            "CT": ("end", "TEMPO FINAL", {"x0": 0, "x1": 96, "align": "center"}),
            # no YOUR RATE FOR line, as in the game's Spanish
            "YR": ("end", "", {"x0": 0, "x1": 128}),
            "COL": ("end", "ITENS COLETADOS", {"x0": 0, "x1": 168, "align": "center"}),
            "NEXT": ("cursive", "Até a próxima missão!", {"x0": 0, "x1": 144, "align": "center"}),
        },
    },
    # MISSION ACCOMPLISHED after the escape: its letters are sprites over
    # this alphabet (chozodia_escape.c); the PT-BR frame (port_lang_ending.c)
    # spells MISSAO CUMPRIDA! with them and puts this tilde, drawn into a
    # free tile (0x1E), over the A.
    "mission_letters": {
        "id": "PORT_LANG_GFX_MISSION_LETTERS",
        "symbol": "sChozodiaEscapeMissionAccomplishedLettersGfx",
        "slots": {"TILDE": ([0x1E], 1)},
        "fonts": {
            "tilde": {
                "sources": [],
                "bg_from": ("TILDE", 0),
                "auto_outline": 1,
                "own_top": 1,
                "own": {"~": ["99.9", "9.99"]},
            },
        },
        "words": {"TILDE": ("tilde", "~", {"x0": 1, "x1": 8})},
    },
    # NEW RECORD sign of the ending (sEndingImageNumbersMisc*Gfx at OBJ tile
    # 0, tiles 0x16-0x1C in three rows). NOVO / RECORDE does not fit its 56
    # pixels, so it is laid out over 80 (tiles 0x16-0x1F; 0x1D-0x1F are free)
    # and port_lang_ending.c shows it with a wider frame. Built from the
    # Italian sign (NUOVO / RECORD!): its box edges and letters, each with
    # the column after it.
    "new_record": {
        "id": "PORT_LANG_GFX_NEW_RECORD",
        "symbol": "sEndingImageNumbersMiscEnglishGfx",
        "headers": ["data/ending_and_gallery_data.h"],
        "extra": {"IT": [("sEndingImageNumbersMiscItalianGfx", 0)]},
        "slots": {
            "SIGN": ([0x16, 0x36, 0x56], 10),
            "IT_SIGN": ([0x16, 0x36, 0x56], 7, "IT"),
        },
        "fonts": {
            # clears the top rows (outside the small box stays transparent)
            "blank": {"sources": [], "bg_from": ("IT_SIGN", 55), "rows": list(range(0, 10))},
            # the big box's top edge (row 9), with its two corners
            "edge": {
                "sources": [("IT_SIGN", "[]", [(0, 1), (54, 1)])],
                "bg_from": ("IT_SIGN", 4),
                "rows": [9],
            },
            # the big box: rows 9-19
            "bot": {
                "sources": [("IT_SIGN", "RECORD!", None, 2, 54), ("IT_SIGN", "[]", [(0, 2), (54, 1)])],
                "cut_bg_from": ("IT_SIGN", 1),
                "ink_rows": list(range(11, 17)),
                "pad_right": 1,
                "bg_from": ("IT_SIGN", 55),
                "rows": list(range(10, 24)),
                "gap": 0,
            },
            # the small box on top: rows 0-9 (row 9 opens the big box's edge)
            "top": {
                "sources": [("IT_SIGN", "NUOVO", None, 8, 47), ("IT_SIGN", "[]", [(6, 2), (48, 1)])],
                "cut_bg_from": ("IT_SIGN", 7),
                "ink_rows": list(range(2, 8)),
                "pad_right": 1,
                "bg_from": ("IT_SIGN", 7),
                "rows": list(range(0, 10)),
                "gap": 0,
            },
        },
        "words": {
            "SIGN@clear": ("blank", "", {"x0": 0, "x1": 80}),
            "SIGN@edge": ("edge", "[", {"x0": 10, "x1": 69}),
            "SIGN@edge2": ("edge", "]", {"x0": 68, "x1": 69}),
            "SIGN@bottom": ("bot", "[RECORDE]", {"x0": 0, "x1": 80, "align": "center"}),
            "SIGN@top": ("top", "[NOVO]", {"x0": 22, "x1": 57}),
        },
    },
}
