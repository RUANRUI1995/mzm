#include "port_scene_pictures.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

/* The pictures loaded: a scene shows one, a cutscene stage up to one per layer it extends
 * (port_gpu_renderer.c, CutscenePictures). */
enum { SLOTS = 4 };
typedef struct Slot {
    const PortScenePictureDef* def;
    C3D_Tex tex;
    uint16_t lut[256]; /* code -> RGBA5551, as painted last */
    bool lutValid;
    bool gameArt;
    bool withAi; /* loaded with its generated parts */
    bool used;   /* asked for since the last PortScenePic_ReleaseUnused */
    /* The texels of each code (code 0, transparent, left out): texels[first[c]
     * .. first[c + 1]), so a palette change repaints only the colours it moved
     * (the title animates part of its palette every frame). */
    uint32_t* texels;
    uint32_t first[257];
} Slot;
static Slot sSlots[SLOTS];

extern bool Port_Config_GetWideAi(void);

static Slot* SlotOf(const PortScenePictureDef* def) {
    for (int i = 0; i < SLOTS; ++i)
        if (def && sSlots[i].def == def) return &sSlots[i];
    return NULL;
}

bool PortScenePic_HasGameArt(const PortScenePictureDef* def) {
    const Slot* s = SlotOf(def);
    return s != NULL && s->gameArt;
}

const PortScenePictureDef* PortScenePic_Find(int gameMode, int bg) {
    int key = -1;
    for (int i = 0; i < gPortScenePictureCount; ++i) {
        const PortScenePictureDef* def = &gPortScenePictures[i];
        if (def->gameMode != gameMode || def->bg != bg) continue;
        if (key < 0) key = PortScenePic_SceneKey(gameMode);
        if (def->sceneKey != key) continue;
        if (!Port_Config_GetWideAi() && !PortScenePic_HasGameArtSource(def)) return NULL;
        return def;
    }
    return NULL;
}

int PortScenePic_FindAll(int gameMode, const PortScenePictureDef** out, int max) {
    if (!Port_Config_GetWideAi()) return 0;
    const int key = PortScenePic_SceneKey(gameMode);
    int n = 0;
    for (int i = 0; i < gPortScenePictureCount && n < max; ++i) {
        const PortScenePictureDef* def = &gPortScenePictures[i];
        if (def->gameMode == gameMode && def->sceneKey == key) out[n++] = def;
    }
    return n;
}

bool PortScenePic_StageHasPictures(int gameMode) {
    const PortScenePictureDef* found[1];
    return PortScenePic_FindAll(gameMode, found, 1) > 0 || PortScenePic_RepeatBgs(gameMode) != 0;
}

int PortScenePic_BgMask(const PortScenePictureDef* def) { return def->bgMask ? def->bgMask : 1 << def->bg; }

/* Standard LZ4 block, as port_gba_bezel.c unpacks the bezel. */
bool PortScenePic_Lz4Decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    size_t s = 0, d = 0;
    while (d < dstLen && s < srcLen) {
        const uint8_t token = src[s++];
        size_t lit = token >> 4;
        if (lit == 15) {
            uint8_t b;
            do { if (s >= srcLen) return false; b = src[s++]; lit += b; } while (b == 255);
        }
        if (s + lit > srcLen || d + lit > dstLen) return false;
        memcpy(dst + d, src + s, lit);
        d += lit;
        s += lit;
        if (d >= dstLen) break;
        if (s + 2 > srcLen) return false;
        const size_t offset = (size_t)src[s] | ((size_t)src[s + 1] << 8);
        s += 2;
        if (offset == 0 || offset > d) return false;
        size_t match = (token & 15u) + 4;
        if (match == 19) {
            uint8_t b;
            do { if (s >= srcLen) return false; b = src[s++]; match += b; } while (b == 255);
        }
        if (d + match > dstLen) return false;
        for (size_t i = 0; i < match; ++i, ++d) dst[d] = dst[d - offset];
    }
    return d == dstLen;
}

static void Free(Slot* s) {
    if (s->def) C3D_TexDelete(&s->tex);
    free(s->texels);
    memset(s, 0, sizeof(*s));
}

void PortScenePic_Release(void) {
    for (int i = 0; i < SLOTS; ++i) Free(&sSlots[i]);
}

void PortScenePic_ReleaseUnused(void) {
    for (int i = 0; i < SLOTS; ++i) {
        if (sSlots[i].def && !sSlots[i].used) Free(&sSlots[i]);
        sSlots[i].used = false;
    }
}

static Slot* Load(const PortScenePictureDef* def, bool withAi) {
    /* Its old copy (loaded without or with the AI's parts), else a free slot, else the first
     * one not asked for this frame. */
    Slot* s = SlotOf(def);
    for (int i = 0; i < SLOTS && !s; ++i)
        if (!sSlots[i].def) s = &sSlots[i];
    for (int i = 0; i < SLOTS && !s; ++i)
        if (!sSlots[i].used) s = &sSlots[i];
    if (!s) return NULL;
    Free(s);
    const size_t n = (size_t)def->texWidth * (size_t)def->texHeight;
    uint8_t* codes = (uint8_t*)malloc(n);
    if (!codes) return NULL;
    const bool unpacked = def->packed ? PortScenePic_Lz4Decompress(def->packed, def->packedSize, codes, n)
                                      : (memset(codes, 0, n), true);
    if (!unpacked || !C3D_TexInit(&s->tex, (u16)def->texWidth, (u16)def->texHeight, GPU_RGBA5551)) {
        free(codes);
        return NULL;
    }
    /* Plain WIDE: none of the generated parts, only the game's own art below. */
    if (!withAi) memset(codes, 0, n);
    s->withAi = withAi;
    s->gameArt = PortScenePic_FillGameArt(def, codes) && !PortScenePic_UsesObjPalette(def);
    uint32_t count[256] = { 0 };
    for (size_t i = 0; i < n; ++i) ++count[codes[i]];
    s->first[0] = s->first[1] = 0;
    for (int c = 1; c < 256; ++c) s->first[c + 1] = s->first[c] + count[c];
    s->texels = (uint32_t*)malloc((s->first[256] ? s->first[256] : 1) * sizeof(uint32_t));
    if (!s->texels) {
        C3D_TexDelete(&s->tex);
        free(codes);
        memset(s, 0, sizeof(*s));
        return NULL;
    }
    uint32_t fill[256];
    memcpy(fill, s->first, sizeof(fill));
    for (size_t i = 0; i < n; ++i)
        if (codes[i]) s->texels[fill[codes[i]]++] = (uint32_t)i;
    /* Nothing of the codes is needed past this: transparent texels stay 0. */
    memset(s->tex.data, 0, n * sizeof(uint16_t));
    free(codes);
    /* 1:1 or the scaled style's 1.5x: NEAREST keeps the pixels hard, like the tiles. */
    C3D_TexSetFilter(&s->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&s->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    s->def = def;
    return s;
}

C3D_Tex* PortScenePic_Texture(const PortScenePictureDef* def, const uint16_t* bgPalette) {
    if (!def) return NULL;
    const bool withAi = Port_Config_GetWideAi();
    Slot* s = SlotOf(def);
    if ((!s || s->withAi != withAi) && !(s = Load(def, withAi))) return NULL;
    s->used = true;

    /* Same texel as the atlas (RGBA5551). Code 0 is transparent; N % 16 == 0 the backdrop. */
    uint16_t lut[256];
    lut[0] = 0;
    for (int i = 1; i < 256; ++i) {
        const uint16_t c = bgPalette[(i & 15) ? i : 0];
        lut[i] = (uint16_t)(((c & 31u) << 11) | (((c >> 5) & 31u) << 6) | (((c >> 10) & 31u) << 1) | 1u);
    }
    uint16_t* texels = (uint16_t*)s->tex.data;
    /* The texels repainted, lowest and highest (each code's list is in texture order), so
     * only that range is flushed: a palette cycle (the red alert's lights) moves a few colours
     * of a 512 KB texture every few frames. */
    uint32_t lo = UINT32_MAX, hi = 0;
    for (int c = 1; c < 256; ++c) {
        if (s->lutValid && lut[c] == s->lut[c]) continue;
        if (s->first[c + 1] == s->first[c]) continue;
        const uint16_t v = lut[c];
        for (uint32_t k = s->first[c]; k < s->first[c + 1]; ++k) texels[s->texels[k]] = v;
        if (s->texels[s->first[c]] < lo) lo = s->texels[s->first[c]];
        if (s->texels[s->first[c + 1] - 1] > hi) hi = s->texels[s->first[c + 1] - 1];
    }
    memcpy(s->lut, lut, sizeof(lut));
    s->lutValid = true;
    if (lo <= hi) GSPGPU_FlushDataCache(texels + lo, (size_t)(hi - lo + 1) * sizeof(uint16_t));
    return &s->tex;
}
