/* The bottom screen's Chinese.
 *
 * The UI is written in English all the way through, in `? :` chains and per
 * language tables of its own. Rather than restructure those, every string is
 * handed to Port_BottomUi_Zh on its way to the screen and swapped for the
 * entry below when there is one. A string this file does not have yet stays
 * English, so a half translated screen still reads.
 *
 * Keys are the exact literal the UI passes, which means the same English can
 * only mean one thing here: "OFF" is the off state of every toggle and of the
 * beam column, so it maps to 关 in both. */

#include <string.h>

#include "port_bottom_ui_zh.h"

static bool sZhActive = false;

static const struct {
    const char* en;
    const char* zh;
} kZh[] = {
    /* ---- Areas (the map header and the collectibles table) ----
     *
     * The names are the ones the iQue build itself prints: they are read out
     * of its text block (the Location_Brinstar..Location_Starship entries) and
     * rendered with its own font, not translated again here. That is why
     * CRATERIA is a transliteration rather than 环形山. */
    { "BRINSTAR",   "布林斯塔" },
    { "KRAID",      "克雷德" },
    { "NORFAIR",    "诺尔飞亚" },
    { "RIDLEY",     "利德雷" },
    { "TOURIAN",    "泰利昂" },
    { "CRATERIA",   "格雷特亚" },
    { "CHOZODIA",   "乔佐迪亚" },
    { "DEBUG",      "调试" },
    { "UNKNOWN",    "未知" },

    /* ---- Map tab ---- */
    { "SAMUS",     "跟随" },
    { "! TARGET",  "! 目标" },
    { "WARP: ON",  "传送: 开" },
    { "WARP: OFF", "传送: 关" },

    /* ---- Collectibles modal ---- */
    { "COLLECTIBLES BY AREA", "按地区统计道具" },
    { "AREA",      "地区" },
    { "TOT",       "合计" },

    /* ---- Status tab ---- */
    { "SAMUS ARAN - STATUS & EQUIPMENT", "萨姆斯 - 状态与装备" },
    { "BEAMS & BOMBS", "光束与炸弹" },
    { "SUITS & MISC",  "服装与装备" },
    { "UNKNOWN ITEM",  "未知物品" },
    { "PARALYZER",     "麻痹枪" },

    /* Beams and bombs. Named as the iQue build announces them (its
     * Message_LongBeam..Message_Bomb strings): 蓄光束, not 蓄力光束. */
    { "LONG BEAM",     "长光束" },
    { "ICE BEAM",      "冰冻光束" },
    { "WAVE BEAM",     "波动光束" },
    { "PLASMA BEAM",   "等离子光束" },
    { "CHARGE BEAM",   "蓄光束" },
    { "BOMBS",         "炸弹" },

    /* Suits and the movement items, again the build's own wording:
     * Message_VariaSuit 防护服, Message_SpeedBooster 加速器,
     * Message_ScrewAttack 团身旋击, Message_PowerGrip 强力抓. The two names the
     * build only shows after the Ruins Test (Gravity, Space Jump) follow the
     * manual. Four characters is the widest that fits: the card lists them two
     * across, so a name starts at 170 or 242 and the state column sits fifty
     * pixels further right. */
    { "HIGH JUMP",     "高跳" },
    { "SPEED BOOSTER", "加速器" },
    { "SPACE JUMP",    "连跳" },
    { "SCREW ATTACK",  "团身旋击" },
    { "VARIA SUIT",    "防护服" },
    { "GRAVITY SUIT",  "重力服" },
    { "MORPH BALL",    "变形球" },
    { "POWER GRIP",    "强力抓" },

    /* Per-item state. Two characters at most: the state sits in a 26 pixel
     * column next to the name. */
    { "OK",        "开" },
    { "OFF",       "关" },
    { "AUTO",      "自动" },

    { "DOWNLOADED MAPS:", "已下载地图:" },
    { "ITEMS",            "道具" },

    /* ---- Options tab ---- */
    { "DISPLAY",    "显示" },
    { "LANGUAGE, ASPECT, STYLE, FPS", "语言、比例、画质、帧率" },
    { "CONTROLS",   "操作" },
    { "REMAPPABLE BUTTONS & C-STICK", "可改按键与 C 摇杆" },
    { "RESTART GAME", "重启游戏" },
    { "UPDATES",      "更新" },

    /* ---- Display settings modal ---- */
    { "DISPLAY SETTINGS", "显示设置" },
    { "LANGUAGE",       "语言" },
    { "ASPECT",         "画面比例" },
    { "DISPLAY STYLE",  "显示方式" },
    { "SHOW FPS",       "帧率显示" },
    { "AUTO-HIDE HUD",  "隐藏HUD" },
    { "HIDE SPOILERS",  "隐藏剧透" },
    { "GBA COLOR",      "GBA 颜色" },
    { "GBA GRID",       "GBA 网格" },
    { "VIGNETTE",       "暗角" },
    { "HUD POSITION",   "HUD 位置" },
    { "GBA FRAME",      "GBA 边框" },
    { "FRAME PACING",   "帧节奏" },

    /* The GBA's own text language, which the game proper reads as well. */
    { "0: JAPANESE (KANJI)",    "0: 日文(汉字)" },
    { "1: JAPANESE (HIRAGANA)", "1: 日文(假名)" },
    { "2: ENGLISH",             "2: 英文" },
    { "3: DEUTSCH",             "3: 德文" },
    { "4: FRANÇAIS",            "4: 法文" },
    { "5: ITALIANO",            "5: 意大利文" },
    { "6: ESPAÑOL",             "6: 西班牙文" },
    /* The bottom screen's own Chinese, offered as the stop after the six GBA
     * languages (see Port_Config_CycleLanguage). */
    { "7: CHINESE",             "7: 中文" },

    /* Values: aspect ratio, display style, fps position, GBA effects. */
    { "ORIGINAL (3:2)", "原始(3:2)" },
    { "ORIGINAL (1:1)", "原始(1:1)" },
    { "WIDE",           "宽屏" },
    { "WIDE + AI",      "宽屏+AI" },
    { "PIXEL PERFECT (1:1)", "像素精确" },
    { "SCALED (SHARP)",      "缩放(锐利)" },
    { "BLUR (SMOOTH)",       "模糊(平滑)" },
    { "SCALED",              "缩放" },
    { "BOTTOM-LEFT",  "左下角" },
    { "BOTTOM-RIGHT", "右下角" },
    { "TOP-LEFT",     "左上角" },
    { "TOP-RIGHT",    "右上角" },
    { "LOW",          "低" },
    { "MED",          "中" },
    { "HIGH",         "高" },
    { "ON",           "开" },
    { "LOCKED",       "不可用" },
    { "TOP BORDER",   "上边框" },
    { "DEFAULT",      "默认" },
    { "ADAPTIVE",     "自适应" },
    { "LOCK 30",      "锁定 30" },

    /* ---- Save states tab ---- */
    { "SAVE STATES",       "存档" },
    { "SAVE STATE",        "存档" },
    { "ESTADO",            "存档" },
    { "NEW SAVE STATE",    "新建存档" },
    { "NO SAVE STATES YET", "暂无存档" },
    { "TAP A STATE FOR ITS DETAILS", "点按存档查看详情" },
    { "OTHER BUILD",       "不同版本" },
    { "SAVED BY ANOTHER BUILD: CANNOT LOAD", "由不同版本保存：无法读取" },
    { "SAVING: ONLY DURING GAMEPLAY", "保存：仅在游戏进行中" },
    { "LOAD",              "读取" },
    { "OVERWRITE",         "覆盖" },
    { "OVERWRITE: IN GAMEPLAY ONLY", "覆盖：仅在游戏进行中" },
    { "DELETE",            "删除" },
    { "TAP TWICE TO CONFIRM", "点按两次确认" },
    { "NO IMAGE",          "无图像" },
    { "NO STATS",          "无数据" },
    { "ROOM",              "房间" },
    { "SALA",              "房间" },
    { "TIME",              "时间" },
    { "JUEGO",             "时间" },
    { "BACK",              "返回" },
    { "OK?",               "确定？" },

    /* ---- Button remapping modal ---- */
    { "BUTTON REMAPPING (TAP TO CHANGE)", "按键设置（点按修改）" },
    { "BUTTON A:", "A 键:" },
    { "BUTTON B:", "B 键:" },
    { "BUTTON X:", "X 键:" },
    { "BUTTON Y:", "Y 键:" },
    { "BUTTON L:", "L 键:" },
    { "BUTTON R:", "R 键:" },
    { "BUTTON ZL:", "ZL 键:" },
    { "BUTTON ZR:", "ZR 键:" },
    { "START:", "START:" },
    { "SELECT:", "SELECT:" },
    { "C-STICK:", "C 摇杆:" },
    { "AIM ONLY",      "仅瞄准" },
    { "MOVEMENT ONLY", "仅移动" },
    { "ALL",           "全部" },
    { "AUTO BOMB JUMP:",   "自动炸弹跳:" },
    { "AUTO SPACE JUMP:",  "自动连跳:" },
    { "CHARGE PULLS DROPS:", "蓄力吸取掉落:" },
    { "NOT IN HARDCORE",   "硬核模式不可用" },
    { "RESET DEFAULTS",    "恢复默认" },
    /* Action names shown as the mapped value of each row (with their key suffix). */
    { "NONE",              "无" },
    { "RAPID FIRE",        "连射" },
    { "QUICK MORPH",       "快速变形" },
    { "FIRE (B)",          "射击 (B)" },
    { "JUMP (A)",          "跳跃 (A)" },
    { "MISSILES (SELECT)", "导弹 (SELECT)" },
    { "AIM (L)",           "瞄准 (L)" },
    { "ARM WEAPON (R)",    "使用武器 (R)" },
    { "PAUSE (START)",     "暂停 (START)" },

    /* ---- Shared chrome ---- */
    { "CLOSE", "关闭" },
};

const char* Port_BottomUi_Zh(const char* en) {
    if (!sZhActive || !en) return en;
    for (unsigned i = 0; i < sizeof(kZh) / sizeof(kZh[0]); ++i) {
        if (strcmp(kZh[i].en, en) == 0) return kZh[i].zh;
    }
    return en;
}

bool Port_BottomUi_ZhActive(void) {
    return sZhActive;
}

void Port_BottomUi_ZhSetActive(bool on) {
    sZhActive = on;
}
