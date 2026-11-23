#include <stdlib.h>
#include <string.h>
#include "smz3_dungeonmaps.h"
#include "smz3_goalicons.h"

/* An icon is put together from pieces of a sprite sheet, some mirrored (the sheets often hold half of a symmetric body). */
typedef struct { uint8_t sheet, sx, sy, sw, sh, dx, dy, flip; } Piece;

/* A Super Metroid enemy's header sits in bank $A0 at its id: +0 size of its tiles, +2 palette (in the bank at +0x0C), +0x36
 * long pointer to the tiles (4 bits per pixel, uncompressed), laid out as the game loads them, 16 to a row. */
static const struct { const char *name; uint16_t id; uint8_t w, h, n; Piece p[4]; } SM_ICONS[] = {
    {"metroid_metroid", 0xDD7F, 40, 44, 4, {{0, 6, 32, 18, 31, 2, 11, 0}, {0, 6, 32, 18, 31, 20, 11, 1}, {0, 5, 0, 20, 27, 0, 0, 0}, {0, 5, 0, 20, 27, 20, 0, 1}}},
    {"metroid_zoomer", 0xDCFF, 24, 16, 1, {{0, 5, 0, 22, 16, 0, 0, 0}}},
    {"metroid_sidehopper", 0xD93F, 28, 28, 4, {{0, 35, 0, 13, 16, 0, 10, 0}, {0, 35, 0, 13, 16, 13, 10, 1}, {0, 3, 0, 10, 16, 3, 0, 0}, {0, 3, 0, 10, 16, 13, 0, 1}}},
    {"metroid_pirate", 0xF353, 16, 32, 1, {{0, 112, 0, 16, 32, 0, 0, 0}}},
};

/* A Link to the Past keeps its sprite sheets compressed, 3 bits per pixel, 128 x 32 pixels each, found through three tables
 * (bank, high and low byte of the address; the Japanese v1.0 ROM has them 0x40 bytes later). The colours are the light world's
 * sprite palettes: 4 rows of 15, each used as two halves of 7 (pal = row * 2 + half). */
static const struct { const char *name; uint8_t w, h, pal, n; Piece p[3]; } Z3_ICONS[] = {
    {"zelda_octorok", 16, 16, 1, 1, {{0x0C, 0, 0, 16, 16, 0, 0, 0}}},
    {"zelda_keese", 16, 16, 1, 1, {{0x1C, 0, 0, 16, 16, 0, 0, 0}}},
    {"zelda_rope", 16, 16, 0, 1, {{0x1C, 64, 16, 16, 16, 0, 0, 0}}},
    {"zelda_stalfos", 16, 16, 1, 1, {{0x1F, 0, 0, 16, 16, 0, 0, 0}}},
    {"zelda_moblin", 16, 16, 4, 1, {{0x17, 48, 0, 16, 16, 0, 0, 0}}},
    {"zelda_hinox", 16, 16, 1, 1, {{0x16, 0, 0, 16, 16, 0, 0, 0}}},
    {"zelda_lynel", 16, 16, 4, 1, {{0x14, 48, 0, 16, 16, 0, 0, 0}}},
    {"zelda_soldier", 16, 16, 2, 1, {{0x46, 0, 0, 16, 16, 0, 0, 0}}},
    {"boss_armos", 32, 32, 2, 1, {{0x1D, 0, 0, 32, 32, 0, 0, 0}}},
    {"boss_moldorm", 16, 16, 1, 1, {{0x1E, 0, 16, 16, 16, 0, 0, 0}}},
    {"boss_helmasaur", 40, 16, 0, 3, {{0x3E, 16, 0, 16, 16, 0, 0, 0}, {0x3E, 16, 0, 16, 16, 24, 0, 1}, {0x3E, 32, 0, 16, 16, 12, 0, 0}}},
    {"boss_arrghus", 16, 32, 2, 1, {{0x39, 96, 0, 16, 32, 0, 0, 0}}},
    {"boss_mothula", 48, 32, 4, 2, {{0x38, 0, 0, 24, 32, 0, 0, 0}, {0x38, 0, 0, 24, 32, 24, 0, 1}}},
    {"boss_blind", 16, 16, 0, 1, {{0x3B, 0, 0, 16, 16, 0, 0, 0}}},
    {"boss_kholdstare", 32, 32, 2, 1, {{0x3C, 0, 0, 32, 32, 0, 0, 0}}},
    {"boss_vitreous", 32, 32, 1, 1, {{0x3D, 64, 0, 32, 32, 0, 0, 0}}},
    {"boss_trinexx", 32, 32, 1, 1, {{0x40, 0, 0, 32, 32, 0, 0, 0}}},
};
#define Z3_SHEET_PTRS 0x4FF3   /* bank bytes; high bytes 0xDF later, low bytes 0x1BE later (US) */
#define Z3_SPRITE_PAL 0xDD218

static size_t lorom(uint32_t ea) { return (size_t)((ea >> 16) & 0x7F) * 0x8000 + (ea & 0x7FFF); }
static unsigned at(const uint8_t *r, size_t len, uint32_t ea) { size_t o = lorom(ea); return o < len ? r[o] : 0; }
static unsigned at16(const uint8_t *r, size_t len, uint32_t ea) { return at(r, len, ea) | at(r, len, ea + 1) << 8; }
static void bgr15(unsigned c, uint8_t *p) { p[0] = (uint8_t)((c & 31) * 255 / 31); p[1] = (uint8_t)((c >> 5 & 31) * 255 / 31); p[2] = (uint8_t)((c >> 10 & 31) * 255 / 31); p[3] = 255; }

static uint8_t sheet[128 * 128 * 4], icon[64 * 64 * 4], pv[2 + 64 * 64 * 4];

static void place(const Piece *q, int w, int h)
{
    for (int y = 0; y < q->sh; y++)
        for (int x = 0; x < q->sw; x++) {
            const uint8_t *s = sheet + ((q->sy + y) * 128 + q->sx + x) * 4;
            int dx = q->dx + (q->flip ? q->sw - 1 - x : x), dy = q->dy + y;
            if (s[3] && dx < w && dy < h) memcpy(icon + (dy * 64 + dx) * 4, s, 4);
        }
}

/* Crops the icon to what is drawn and hands it out. */
static int emit(const char *name, int w, int h, void (*out)(const char *, const uint8_t *, size_t, void *), void *ctx)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (icon[(y * 64 + x) * 4 + 3]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) return 0;
    int ow = x1 - x0 + 1, oh = y1 - y0 + 1;
    pv[0] = (uint8_t)ow; pv[1] = (uint8_t)oh;
    for (int y = 0; y < oh; y++) memcpy(pv + 2 + (size_t)y * ow * 4, icon + ((y0 + y) * 64 + x0) * 4, (size_t)ow * 4);
    out(name, pv, 2 + (size_t)ow * oh * 4, ctx);
    return 1;
}

static int sm_icons(const uint8_t *sm, size_t sm_len, void (*out)(const char *, const uint8_t *, size_t, void *), void *ctx)
{
    int made = 0;
    for (size_t k = 0; k < sizeof SM_ICONS / sizeof SM_ICONS[0]; k++) {
        uint32_t h = 0xA00000u | SM_ICONS[k].id;
        unsigned size = at16(sm, sm_len, h), bank = at(sm, sm_len, h + 0x0C);
        uint32_t pal = bank << 16 | at16(sm, sm_len, h + 2), tiles = at16(sm, sm_len, h + 0x36) | at(sm, sm_len, h + 0x38) << 16;
        size_t t0 = lorom(tiles), ntiles = size / 32;
        if (!size || ntiles > 16 * 16 || t0 + size > sm_len) continue;
        memset(sheet, 0, sizeof sheet);
        for (size_t t = 0; t < ntiles; t++)
            for (int y = 0; y < 8; y++) {
                const uint8_t *s = sm + t0 + t * 32;
                for (int x = 0; x < 8; x++) {
                    int b = 7 - x, v = (s[y * 2] >> b & 1) | (s[y * 2 + 1] >> b & 1) << 1 | (s[16 + y * 2] >> b & 1) << 2 | (s[17 + y * 2] >> b & 1) << 3;
                    if (v) bgr15(at16(sm, sm_len, pal + (uint32_t)v * 2), sheet + (((t / 16) * 8 + y) * 128 + (t % 16) * 8 + x) * 4);
                }
            }
        memset(icon, 0, sizeof icon);
        for (int i = 0; i < SM_ICONS[k].n; i++) place(&SM_ICONS[k].p[i], SM_ICONS[k].w, SM_ICONS[k].h);
        made += emit(SM_ICONS[k].name, SM_ICONS[k].w, SM_ICONS[k].h, out, ctx);
    }
    return made;
}

/* Decodes sheet n into the sheet buffer with palette half pal. */
static int z3_sheet(const uint8_t *z3, size_t len, int n, int pal)
{
    size_t t = Z3_SHEET_PTRS + (z3[0x7FD9] == 0 ? 0x40 : 0) + (size_t)n;   /* 0x7FD9 (region) is 0 on the Japanese ROM */
    if (t + 0x1BE >= len) return 0;
    int ea = z3[t] << 16 | z3[t + 0xDF] << 8 | z3[t + 0x1BE];
    static uint8_t raw[0x600];
    memset(raw, 0, sizeof raw);
    if (smz_z3_decompress(z3, len, ea, raw, sizeof raw) <= 0) return 0;
    uint8_t col[8][4];
    for (int i = 1; i < 8; i++) {
        size_t o = Z3_SPRITE_PAL + (size_t)(pal / 2) * 30 + (size_t)((pal & 1) * 8 + i - 1) * 2;
        bgr15(z3[o] | z3[o + 1] << 8, col[i]);
    }
    memset(sheet, 0, sizeof sheet);
    for (int k = 0; k < 64; k++)
        for (int y = 0; y < 8; y++) {
            const uint8_t *s = raw + k * 24;
            for (int x = 0; x < 8; x++) {
                int b = 7 - x, v = (s[y * 2] >> b & 1) | (s[y * 2 + 1] >> b & 1) << 1 | (s[16 + y] >> b & 1) << 2;
                if (v) memcpy(sheet + (((k / 16) * 8 + y) * 128 + (k % 16) * 8 + x) * 4, col[v], 4);
            }
        }
    return 1;
}

static int z3_icons(const uint8_t *z3, size_t len, void (*out)(const char *, const uint8_t *, size_t, void *), void *ctx)
{
    int made = 0;
    if (len < 0xE0000) return 0;
    for (size_t k = 0; k < sizeof Z3_ICONS / sizeof Z3_ICONS[0]; k++) {
        memset(icon, 0, sizeof icon);
        int ok = 1;
        for (int i = 0; ok && i < Z3_ICONS[k].n; i++) {
            const Piece *q = &Z3_ICONS[k].p[i];
            ok = z3_sheet(z3, len, q->sheet, Z3_ICONS[k].pal);
            if (ok) place(q, Z3_ICONS[k].w, Z3_ICONS[k].h);
        }
        if (ok) made += emit(Z3_ICONS[k].name, Z3_ICONS[k].w, Z3_ICONS[k].h, out, ctx);
    }
    return made;
}

int smz_goal_icons(const uint8_t *sm, size_t sm_len, const uint8_t *z3, size_t z3_len,
                   void (*out)(const char *name, const uint8_t *pv, size_t n, void *ctx), void *ctx)
{
    int made = z3 ? z3_icons(z3, z3_len, out, ctx) : 0;
    return made + (sm ? sm_icons(sm, sm_len, out, ctx) : 0);   /* the mark icon (Super Metroid) last: a cut stopped halfway is done again */
}
