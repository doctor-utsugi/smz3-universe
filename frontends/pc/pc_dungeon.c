#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_dungeon.h"
#include "pc_gfx.h"
#include "pc_map.h"
#include "pc_store.h"
#include "smz3_dungeonmaps.h"
#include "smz3_state.h"

static SmzDungeonMeta meta;
static SDL_Texture *tex_full, *tex_hint;
static int state;   /* 0 not tried, 1 ready, -1 missing */

/* the boss room of each dungeon, the floor it is on (relative to the ground floor) and where its icon sits (x << 8 | y, -1 for none) */
static const int BOSS_ROOM[14] = {15, 15, 200, 51, 32, 6, 90, 144, 41, 222, 7, 172, 164, 13};
static const int BOSS_FLOOR[14] = {-1, -1, 1, 1, 6, -1, -1, -1, -2, -7, 5, -1, -3, 6};
static const int BOSS_SPOT[14] = {-1, -1, 0x808, 8, 0, 8, 0x808, 8, 0x808, 0x800, 0x404, 0x808, 8, 8};
static const char *const NAMES[14] = {"Sewers", "Hyrule Castle", "Eastern Palace", "Desert Palace", "Castle Tower", "Swamp Palace", "Palace of Darkness",
                                      "Misery Mire", "Skull Woods", "Ice Palace", "Tower of Hera", "Thieves' Town", "Turtle Rock", "Ganon's Tower"};

static SDL_Texture *make_tex(SDL_Renderer *r, const uint8_t *rgba, int ok)
{
    if (!ok) return NULL;
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, SMZ_DM_ATLAS_W, SMZ_DM_ATLAS_H);
    SDL_UpdateTexture(t, NULL, rgba, SMZ_DM_ATLAS_W * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    return t;
}

int dungeon_available(SDL_Renderer *r)
{
    if (state) return state > 0;
    size_t n, atlas = (size_t)SMZ_DM_ATLAS_W * SMZ_DM_ATLAS_H * 4;
    uint8_t *file = store_read(store_path("derived/dungeons.bin"), &n);
    if (file && n == sizeof meta + 2 * atlas) {
        memcpy(&meta, file, sizeof meta);
        tex_full = make_tex(r, file + sizeof meta, 1);
        tex_hint = make_tex(r, file + sizeof meta + atlas, 1);
        state = 1;
    } else state = -1;
    free(file);
    return state > 0;
}

static int floors_of(int d) { return meta.below[d] + meta.above[d]; }
static int u16(const uint8_t *w, int a) { return w[a] | w[a + 1] << 8; }

static int place(const uint8_t *w, DungeonLive *out)
{
    int d = w[0x040C] / 2;
    if (w[0x1B] == 0 || w[0x040C] >= 28 || d >= 14) return 0;
    int idx = (int8_t)w[0xA4] + meta.below[d], fl = floors_of(d);
    if (fl < 1) return 0;
    if (idx < 0) idx = 0;
    if (idx > fl - 1) idx = fl - 1;
    int room = u16(w, 0xA0);
    if (room == 137) room = 169; else if (room == 167) room = 119; else if (room == 79) room = 190;
    int here = -1;
    for (int c = 0; c < 25; c++) if (meta.grid[d][idx][c] == room) { here = c; break; }
    *out = (DungeonLive){d, idx, here, here < 0 ? 0 : (u16(w, 0x22) & 0x1E0) >> 5, here < 0 ? 0 : (u16(w, 0x20) & 0x1E0) >> 5};
    return 1;
}

/* While the game is in the middle of a room change the room number and Link's coordinates do not belong to the same room yet: keep the last settled place. */
int dungeon_live(const uint8_t *wram, size_t len, DungeonLive *out)
{
    static DungeonLive stable;
    static int have;
    if (state <= 0 || len < 0x1000) return 0;
    DungeonLive now;
    int ok = place(wram, &now);
    int moving = wram[0x10] == 0x07 && wram[0x11] != 0;
    if (moving && ok && have && stable.d == now.d) { *out = stable; return 1; }
    have = ok;
    if (ok) stable = now;
    *out = now;
    return ok;
}

void dungeon_view_init(DungeonView *v) { *v = (DungeonView){0, 0, 2, 0}; v->idx = 0; }

int dungeon_showing(const DungeonView *v, const DungeonLive *live) { return state > 0 && !v->world && (v->picked || live); }

static void shown(const DungeonView *v, const DungeonLive *live, int *d, int *idx)
{
    if (v->picked || !live) { *d = v->d; *idx = v->idx; }
    else { *d = live->d; *idx = live->idx; }
}

static void pick(DungeonView *v, int d, int idx)
{
    v->picked = 1; v->world = 0;
    v->d = (d + 14) % 14;
    int fl = floors_of(v->d);
    v->idx = idx < 0 ? 0 : idx > fl - 1 ? fl - 1 : idx;
}

void dungeon_step(DungeonView *v, const DungeonLive *live, int delta)
{
    int d, idx;
    shown(v, live, &d, &idx);
    int nd = (d + delta + 14) % 14;
    pick(v, nd, meta.below[nd]);
}

void dungeon_floor(DungeonView *v, const DungeonLive *live, int delta)
{
    int d, idx;
    shown(v, live, &d, &idx);
    pick(v, d, idx + delta);
}

void dungeon_here(DungeonView *v) { v->picked = 0; v->world = 0; }

void dungeon_toggle(DungeonView *v, const DungeonLive *live)
{
    if (dungeon_showing(v, live)) v->world = 1;
    else if (live) dungeon_here(v);
    else { v->world = 0; if (!v->picked) pick(v, v->d, meta.below[v->d]); }
}

static void bounds(int d, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = 5; *y0 = 5; *x1 = -1; *y1 = -1;
    for (int f = 0; f < floors_of(d); f++)
        for (int k = 0; k < 25; k++)
            if (meta.grid[d][f][k] != 0xF) {
                int cx = k % 5, cy = k / 5;
                if (cx < *x0) *x0 = cx; if (cx > *x1) *x1 = cx; if (cy < *y0) *y0 = cy; if (cy > *y1) *y1 = cy;
            }
    if (*x1 < 0) { *x0 = 0; *y0 = 0; *x1 = 4; *y1 = 4; }
}

static void dot(SDL_Renderer *r, float x, float y, float rad, int st)
{
    if (st == 2) {
        map_circle(r, x, y, rad * 0.8f, (SDL_Color){0x24, 0x30, 0x38, 230}, 0);
        map_circle(r, x, y, rad * 0.8f, (SDL_Color){0xF0, 0xF4, 0xF8, 240}, 1);
    } else {
        map_circle(r, x, y, rad + 2, (SDL_Color){255, 255, 255, 255}, 0);
        map_circle(r, x, y, rad + 0.5f, (SDL_Color){0, 0, 0, 255}, 0);
        map_circle(r, x, y, rad, (SDL_Color){0xFF, 0xB3, 0, 255}, 0);
    }
}

static int fixed_keys;
void dungeon_set_fixed_keys(int on) { fixed_keys = on; }

/* A key standing up, its bow at the top: gold, a big key larger and paler. */
static void key_mark(SDL_Renderer *r, float x, float y, float s, int big)
{
    SDL_Color gold = big ? (SDL_Color){0xFF, 0xE0, 0x8A, 255} : (SDL_Color){0xF2, 0xC1, 0x4E, 255}, ink = {0x1A, 0x14, 0x08, 255};
    if (big) s *= 1.25f;
    map_circle(r, x, y - s * 0.55f, s * 0.62f, ink, 0);
    gfx_rect(x - s * 0.3f, y - s * 0.3f, s * 0.6f, s * 1.5f, ink);
    map_circle(r, x, y - s * 0.55f, s * 0.45f, gold, 0);
    map_circle(r, x, y - s * 0.55f, s * 0.18f, ink, 0);
    gfx_rect(x - s * 0.13f, y - s * 0.15f, s * 0.26f, s * 1.2f, gold);
    gfx_rect(x + s * 0.13f, y + s * 0.6f, s * 0.32f, s * 0.2f, gold);
    gfx_rect(x + s * 0.13f, y + s * 0.85f, s * 0.24f, s * 0.18f, gold);
}

void dungeon_draw(SDL_Renderer *r, DungeonView *v, SDL_Rect area, const SmzTracker *t, const uint8_t *in_logic, const DungeonLive *live,
                  const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int show_all, Uint32 ticks)
{
    int d, idx;
    shown(v, live, &d, &idx);
    gfx_rect((float)area.x, (float)area.y, (float)area.w, (float)area.h, (SDL_Color){0x0B, 0x0E, 0x12, 255});
    float u = area.h / 560.0f;
    int fl = floors_of(d), below = meta.below[d];
    int x0, y0, x1, y1;
    bounds(d, &x0, &y0, &x1, &y1);
    int cols = x1 - x0 + 1, rows = y1 - y0 + 1;

    /* what the game remembers: room quadrants visited, the dungeon's map and compass */
    int zelda = t->game == SMZ3_GAME_ZELDA;
    const uint8_t *src = zelda ? wram : sram;
    size_t srclen = zelda ? wram_len : sram_len;
    int base = zelda ? 0xF000 : 0, have_data = src && srclen >= (size_t)base + 0x370;
    const uint8_t *flags = have_data ? src + base : NULL;
    int map_mask = have_data ? u16(src, base + 0x368) : 0, compass_mask = have_data ? u16(src, base + 0x364) : 0;
    int have_map = have_data && (map_mask & (0x8000 >> d));

    float title_h = 34 * u, side = 76 * u, foot = 44 * u;
    float areaL = area.x + side, areaT = area.y + title_h, areaR = area.x + area.w - 10 * u, areaB = area.y + area.h - foot;
    float scale = fminf((areaR - areaL) / (cols * SMZ_DM_ROOM), (areaB - areaT) / (rows * SMZ_DM_ROOM));
    float picW = cols * SMZ_DM_ROOM * scale, picH = rows * SMZ_DM_ROOM * scale;
    float left = areaL + ((areaR - areaL) - picW) / 2, top = areaT + ((areaB - areaT) - picH) / 2;

    SDL_RenderSetClipRect(r, &area);
    for (int c = 0; c < 25; c++) {
        int room = meta.grid[d][idx][c];
        if (room == 0xF) continue;
        int seen = flags ? flags[room * 2] & 0xF : 0;
        for (int q = 0; q < 4; q++) {
            int visited = seen & (8 >> q);
            SDL_Texture *atlas = visited || show_all ? tex_full : have_map ? tex_hint : NULL;
            if (!atlas) continue;
            int ox = (c % 5) * SMZ_DM_ROOM + (q & 1) * 8, oy = (c / 5) * SMZ_DM_ROOM + (q >> 1) * 8;
            SDL_Rect s = {idx * SMZ_DM_CELL + ox, d * SMZ_DM_CELL + oy, 8, 8};
            SDL_FRect dst = {left + (ox - x0 * SMZ_DM_ROOM) * scale, top + (oy - y0 * SMZ_DM_ROOM) * scale, 8 * scale + 0.5f, 8 * scale + 0.5f};
            SDL_SetTextureAlphaMod(atlas, visited || !show_all ? 255 : 100);
            SDL_RenderCopyF(r, atlas, &s, &dst);
        }
    }

    /* unopened checks: at their chest when the room data places it, else together at the room's centre */
    for (int c = 0; c < 25; c++) {
        int room = meta.grid[d][idx][c];
        if (room == 0xF) continue;
        float ox = left + (c % 5 - x0) * SMZ_DM_ROOM * scale, oy = top + (c / 5 - y0) * SMZ_DM_ROOM * scale;
        int loose = 0;
        for (unsigned i = 0; i < smz3_location_count && i < 512; i++) {
            const smz3_location *l = &smz3_locations[i];
            if (l->game != SMZ3_GAME_ZELDA || l->zelda_misc || (int)l->addr != room || t->cleared[i]) continue;
            int st = !in_logic || in_logic[i] ? 1 : 2, k = (int)l->flag - 4;
            if (k >= 0 && k < SMZ_DM_CHESTS && k < meta.chest_n[room]) dot(r, ox + (meta.chest_xy[room][k][0] + 0.5f) / 4 * scale, oy + (meta.chest_xy[room][k][1] + 0.5f) / 4 * scale, scale * 2, st);
            else loose |= st;
        }
        if (loose) {
            float rad = scale * 2.6f, off = loose == 3 ? rad * 0.9f : 0;
            if (loose & 2) dot(r, ox + 8 * scale + off, oy + 8 * scale, rad, 2);
            if (loose & 1) dot(r, ox + 8 * scale - off, oy + 8 * scale, rad, 1);
        }
    }

    /* keys under pots and on enemies, until taken, on runs that ask for them */
    for (int i = 0; fixed_keys && i < smz_fixed_key_count; i++) {
        const SmzFixedKey *fk = &smz_fixed_keys[i];
        if (smz_fixed_key_taken(i, flags, have_data ? srclen - base : 0, zelda ? wram : NULL, zelda ? wram_len : 0)) continue;
        for (int c = 0; c < 25; c++) {
            if (meta.grid[d][idx][c] != fk->room) continue;
            float ox = left + (c % 5 - x0) * SMZ_DM_ROOM * scale, oy = top + (c / 5 - y0) * SMZ_DM_ROOM * scale;
            key_mark(r, ox + (fk->x + 0.5f) / 4 * scale, oy + (fk->y + 0.5f) / 4 * scale, scale * 1.6f, fk->big);
        }
    }

    /* the boss, once the compass says where (or on all-map runs), until it is beaten */
    int spot = BOSS_SPOT[d];
    if (spot >= 0 && idx == BOSS_FLOOR[d] + below && flags) {
        int known = show_all || (compass_mask & (0x8000 >> d)), beaten = flags[BOSS_ROOM[d] * 2 + 1] & 0x08, c = -1;
        for (int k = 0; k < 25; k++) if (meta.grid[d][idx][k] == BOSS_ROOM[d]) c = k;
        if (known && !beaten && c >= 0) {
            float x = left + ((c % 5 - x0) * SMZ_DM_ROOM + (spot >> 8) + 4) * scale, y = top + ((c / 5 - y0) * SMZ_DM_ROOM + (spot & 0xFF) + 4) * scale, rad = scale * 2.8f;
            map_circle(r, x, y, rad + 2, (SDL_Color){255, 255, 255, 255}, 0);
            map_circle(r, x, y, rad, (SDL_Color){0xD3, 0x2F, 0x2F, 255}, 0);
            gfx_text(x, y - rad * 0.8f, rad * 1.6f, (SDL_Color){255, 255, 255, 255}, 1, "B");
        }
    }

    /* Link, blinking, only on the floor he is on */
    if (live && live->d == d && live->idx == idx && live->cell >= 0 && (ticks / 350) % 2 == 0) {
        float x = left + ((live->cell % 5 - x0) * SMZ_DM_ROOM + live->lx + 0.5f) * scale, y = top + ((live->cell / 5 - y0) * SMZ_DM_ROOM + live->ly + 0.5f) * scale;
        map_circle(r, x, y, scale * 1.9f, (SDL_Color){0, 0, 0, 255}, 0);
        map_circle(r, x, y, scale * 1.3f, (SDL_Color){255, 255, 255, 255}, 0);
    }

    /* header, the floor list (top floor first) and the hint */
    SDL_Color white = {0xE8, 0xEC, 0xF2, 255}, dim = {0x87, 0x93, 0xA6, 255}, amber = {0xE9, 0xA2, 0x3B, 255};
    gfx_text(area.x + 10 * u, area.y + 6 * u, 22 * u, white, 0, NAMES[d]);
    for (int i = fl - 1; i >= 0; i--) {
        char label[8];
        int fn = i - below;
        if (fn >= 0) snprintf(label, sizeof label, "%dF", fn + 1); else snprintf(label, sizeof label, "B%d", -fn);
        float y = area.y + title_h + (fl - 1 - i) * 34 * u;
        gfx_rect(area.x + 8 * u, y, 56 * u, 28 * u, i == idx ? (SDL_Color){0x3A, 0x4A, 0x66, 255} : (SDL_Color){0x1D, 0x24, 0x30, 255});
        gfx_text(area.x + 36 * u, y + 3 * u, 20 * u, i == idx ? amber : white, 1, label);
    }
    /* the strip to jump to any dungeon, and what the dots mean */
    static const char *const SHORT[14] = {"Sewers", "Castle", "Eastern", "Desert", "Tower", "Swamp", "Dark", "Mire", "Skull", "Ice", "Hera", "Thieves", "Turtle", "Ganon"};
    float cw = (area.w - 12 * u) / 14, cy = area.y + area.h - foot + 2 * u;
    for (int i = 0; i < 14; i++) {
        float cx = area.x + 6 * u + i * cw;
        /* checks still to get in the dungeon (the sewers belong to Hyrule Castle), green when it is done */
        const char *region = i == 0 ? "Hyrule Castle" : NAMES[i];
        int left = 0, all = 0;
        for (unsigned j = 0; j < smz3_location_count && j < SMZ_TRK_MAX_LOCS; j++)
            if (!strcmp(smz3_locations[j].region, region)) { all++; if (!t->cleared[j]) left++; }
        char n[16];
        if (left) snprintf(n, sizeof n, "%d left", left); else snprintf(n, sizeof n, "done");
        gfx_rect(cx, cy, cw - 3 * u, 38 * u, i == d ? (SDL_Color){0x3A, 0x4A, 0x66, 255} : (SDL_Color){0x1D, 0x24, 0x30, 255});
        gfx_text(cx + (cw - 3 * u) / 2, cy + 2 * u, 16 * u, i == d ? amber : dim, 1, SHORT[i]);
        if (all) gfx_text(cx + (cw - 3 * u) / 2, cy + 19 * u, 14 * u, left ? white : (SDL_Color){0x4C, 0xC3, 0x8A, 255}, 1, n);
    }
    map_legend(r, area.x + area.w - 10 * u, area.y + 8 * u, 16 * u, 1, in_logic != NULL);
    SDL_RenderSetClipRect(r, NULL);
}
