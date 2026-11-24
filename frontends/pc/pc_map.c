#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include "pc_assets.h"
#include "pc_gfx.h"
#include "pc_map.h"
#include "pc_emu.h"
#include "smz3_smmap.h"
#include "smz3_z3.h"
#include "smz3_state.h"

#define MAP_W 1623.0f
#define MAP_H 958.0f

static SDL_Texture *map_texs[2], *disc_texs[2], *ring_texs[2];   /* one set per window (gfx_slot) */
#define map_tex map_texs[gfx_slot()]
#define disc_tex disc_texs[gfx_slot()]
#define ring_tex ring_texs[gfx_slot()]
static int map_w, map_h;
static signed short stack_head[512];     /* per location: index of the first check on the same dot, or itself */
static unsigned char stack_size[512];    /* checks on the dot of a head */
static int readys[2];
#define ready readys[gfx_slot()]
static void load(SDL_Renderer *r);

/* The portals of the other game: their pixels as the map has them and painted over (map_portals.py), swapped into each
 * window's texture when a single-game run is tracked. */
static uint8_t *patch_px[2];   /* 0 the map's own, 1 the clean ones; the patches one after another, RGBA rows */
static int patched[2];         /* per window: which set its texture holds */

static void patches_load(const unsigned char *map, int w)
{
    int pw, ph, n;
    unsigned char *strip = stbi_load_from_memory(PC_MAP_PATCH_PNG, (int)PC_MAP_PATCH_PNG_LEN, &pw, &ph, &n, 4);
    size_t total = 0;
    for (int i = 0; i < PC_MAP_PATCH_COUNT; i++) total += (size_t)PC_MAP_PATCHES[i][2] * PC_MAP_PATCHES[i][3] * 4;
    if (strip && (patch_px[0] = malloc(total)) && (patch_px[1] = malloc(total))) {
        size_t o = 0;
        for (int i = 0; i < PC_MAP_PATCH_COUNT; i++) {
            const int *p = PC_MAP_PATCHES[i];
            for (int y = 0; y < p[3]; y++, o += (size_t)p[2] * 4) {
                memcpy(patch_px[0] + o, map + ((size_t)(p[1] + y) * w + p[0]) * 4, (size_t)p[2] * 4);
                memcpy(patch_px[1] + o, strip + ((size_t)y * pw + p[4]) * 4, (size_t)p[2] * 4);
            }
        }
    } else { free(patch_px[0]); patch_px[0] = NULL; }
    stbi_image_free(strip);
}

static void patches_apply(void)
{
    int want = smz3_sm_alone() || smz3_z3_alone();
    if (!map_tex || !patch_px[0] || patched[gfx_slot()] == want) return;
    patched[gfx_slot()] = want;
    size_t o = 0;
    for (int i = 0; i < PC_MAP_PATCH_COUNT; i++) {
        const int *p = PC_MAP_PATCHES[i];
        SDL_Rect r = {p[0], p[1], p[2], p[3]};
        SDL_UpdateTexture(map_tex, &r, patch_px[want] + o, p[2] * 4);
        o += (size_t)p[2] * p[3] * 4;
    }
}

static SDL_Texture *make_disc(SDL_Renderer *r, int ring)
{
    enum { N = 64 };
    uint32_t px[N * N];
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float d = hypotf(x + 0.5f - N / 2, y + 0.5f - N / 2) / (N / 2.0f), a;
            if (ring) a = d > 1 ? 0 : d > 0.72f ? 1 : 0; else a = d < 1 ? 1 : 0;
            if (ring) a = fminf(1, fmaxf(0, (d - 0.70f) * N / 2)) * fminf(1, fmaxf(0, (1.0f - d) * N / 2));
            else a = fminf(1, fmaxf(0, (1.0f - d) * N / 2));
            px[y * N + x] = 0x00FFFFFFu | ((uint32_t)(a * 255) << 24);
        }
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, N, N);
    SDL_UpdateTexture(t, NULL, px, N * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

void map_circle(SDL_Renderer *r, float x, float y, float rad, SDL_Color c, int ring)
{
    if (!ready) load(r);
    SDL_Texture *t = ring ? ring_tex : disc_tex;
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_FRect d = {x - rad, y - rad, rad * 2, rad * 2};
    SDL_RenderCopyF(r, t, NULL, &d);
}

static const SmzMarker *find_marker(int index)
{
    for (int i = 0; i < PC_MAP_DATA.n_markers; i++) if (PC_MAP_DATA.markers[i].index == index) return &PC_MAP_DATA.markers[i];
    return NULL;
}

static void load(SDL_Renderer *r)
{
    ready = 1;
    int n;
    unsigned char *img = stbi_load_from_memory(PC_MAP_PNG, (int)PC_MAP_PNG_LEN, &map_w, &map_h, &n, 4);
    if (img) {
        map_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, map_w, map_h);
        SDL_UpdateTexture(map_tex, NULL, img, map_w * 4);
        SDL_SetTextureScaleMode(map_tex, SDL_ScaleModeLinear);
        patched[gfx_slot()] = 0;
        if (!patch_px[0]) patches_load(img, map_w);
        stbi_image_free(img);
    }
    disc_tex = make_disc(r, 0);
    ring_tex = make_disc(r, 1);
    /* the checks that share one spot on the map (a dungeon's chests) are one dot */
    for (unsigned i = 0; i < smz3_location_count && i < 512; i++) {
        stack_head[i] = (signed short)i;
        const SmzMarker *m = find_marker((int)i);
        if (!m || smz3_locations[i].game != SMZ3_GAME_ZELDA) continue;
        for (unsigned j = 0; j < i; j++) {
            const SmzMarker *o = find_marker((int)j);
            if (o && smz3_locations[j].game == SMZ3_GAME_ZELDA && o->x == m->x && o->y == m->y) { stack_head[i] = stack_head[j]; break; }
        }
    }
    for (unsigned i = 0; i < smz3_location_count && i < 512; i++) if (stack_size[stack_head[i]] < 255) stack_size[stack_head[i]]++;
}

void map_view_init(MapView *v) { v->cx = MAP_W / 2; v->cy = MAP_H / 2; v->zoom = 1.4f; v->follow = 1; v->highlight = 0; v->gz = v->gz_set = 0; }

static SmzSmMap *gmap;
static uint8_t *grom;
static size_t grom_len;
static SDL_Texture *gtexs[2];
static int gtex_w[2], gtex_h[2];
static uint32_t *gbuf;
static size_t gbuf_len;

void map_game_rom(const uint8_t *rom, size_t len)
{
    free(grom);
    grom = NULL;
    if (!smz_smmap_rom_ok(rom, len) || !(grom = malloc(len))) return;
    memcpy(grom, rom, len);
    grom_len = len;
    if (!gmap) gmap = malloc(sizeof *gmap);
    if (gmap) smz_smmap_init(gmap);
}

int map_game_on(void) { return grom && gmap && smz3_maprando(); }

void map_game_step(int dir)
{
    size_t sl = 0;
    uint8_t *s = emu_sram(&sl);
    if (map_game_on()) smz_smmap_step(gmap, s, sl, dir);
}

const char *map_game_area(void) { return map_game_on() ? smz_smmap_area_name(gmap->shown) : ""; }

void map_view_move(MapView *v, float dx, float dy, float dz)
{
    if (map_game_on()) {
        float z = v->gz > 0 ? v->gz : 1;
        if (dx != 0 || dy != 0) { v->follow = 0; v->gx += dx / z; v->gy += dy / z; }
        if (dz != 0 && dz != 1) { z *= dz; v->gz_set = v->gz = z < 0.5f ? 0.5f : z > 8 ? 8 : z; }
        return;
    }
    if (dx != 0 || dy != 0) { v->follow = 0; v->cx += dx / v->zoom; v->cy += dy / v->zoom; }
    if (dz != 0) { v->zoom *= dz; if (v->zoom < 0.4f) v->zoom = 0.4f; if (v->zoom > 8) v->zoom = 8; }
}

/* The game's pause map (core/ui/smz3_smmap.c) in the area, with the area's name in a corner. */
static void game_map_draw(SDL_Renderer *r, MapView *v, SDL_Rect area, const uint8_t *wram, size_t wram_len)
{
    size_t sl = 0;
    uint8_t *sram = emu_sram(&sl);
    smz_smmap_update(gmap, grom, grom_len, wram, wram_len, sram, sl);
    float cx, cy, z;
    smz_smmap_home(gmap, area.w, area.h, &cx, &cy, &z);
    int here = gmap->samus_area == gmap->shown && gmap->samus_x >= 0;
    if (v->gz_set > 0 && v->gz_set != z && here) { cx = gmap->samus_x * 8 + 4.0f; cy = gmap->samus_y * 8 + 4.0f; }
    if (v->gz_set > 0) z = v->gz_set;
    if (!v->follow && v->gz > 0) { cx = v->gx; cy = v->gy; }
    v->gx = cx; v->gy = cy; v->gz = z;
    z = z >= 1 ? floorf(z + 0.5f) : 0.5f;   /* whole pixels keep the walls one line thick */
    size_t need = (size_t)area.w * area.h;
    if (need > gbuf_len) { free(gbuf); gbuf = malloc(need * 4); gbuf_len = gbuf ? need : 0; }
    if (!gbuf) return;
    smz_smmap_draw(gmap, gbuf, area.w, area.h, area.w, cx, cy, z);
    int s = gfx_slot();
    if (!gtexs[s] || gtex_w[s] < area.w || gtex_h[s] < area.h) {
        if (gtexs[s]) SDL_DestroyTexture(gtexs[s]);
        gtexs[s] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, area.w, area.h);
        gtex_w[s] = area.w; gtex_h[s] = area.h;
        if (gtexs[s]) SDL_SetTextureScaleMode(gtexs[s], SDL_ScaleModeNearest);
    }
    if (!gtexs[s]) return;
    SDL_Rect src = {0, 0, area.w, area.h};
    SDL_UpdateTexture(gtexs[s], &src, gbuf, area.w * 4);
    SDL_RenderCopy(r, gtexs[s], &src, &area);
    float h = fminf(20.0f, fmaxf(12.0f, area.h / 22.0f));
    const char *name = smz_smmap_area_name(gmap->shown);
    if (*name) {
        float w = gfx_text_width(h, name) + h;
        gfx_rect(area.x, area.y, w, h * 1.5f, (SDL_Color){0, 0, 0, 170});
        gfx_text(area.x + h / 2, area.y + h * 0.25f, h, (SDL_Color){0xE8, 0xEC, 0xF2, 255}, 0, name);
    }
}

/* A single game shows only its half of the combined map: Super Metroid right of x 480 (smz_disc_area_box), A Link to the
 * Past left of it. */
#define HALF_X (smz3_sm_alone() ? 480.0f : 0.0f)
#define HALF_END (smz3_z3_alone() ? 480.0f : MAP_W)

void map_view_fit(MapView *v, SDL_Rect area)
{
    v->zoom = fminf(area.w / (HALF_END - HALF_X), area.h / MAP_H);
    v->cx = (HALF_END + HALF_X) / 2; v->cy = MAP_H / 2;
}

static const uint8_t *visible;
void map_set_visible(const uint8_t *vis) { visible = vis; }
const uint8_t *map_visible(void) { return visible; }

static int seen_at(const SmzRect *rects, int n, float x, float y)
{
    for (int i = 0; i < n; i++) if (x >= rects[i].x && x < rects[i].x + rects[i].w && y >= rects[i].y && y < rects[i].y + rects[i].h) return 1;
    return 0;
}

/* One line of "dot meaning" pairs ending at x (right aligned); the world map adds the collected dot and the player. */
void map_legend(SDL_Renderer *r, float x, float y, float h, int dungeon, int logic)
{
    struct { int kind; const char *label; } L[5];
    int n = 0;
    if (logic) L[n++] = (typeof(L[0])){0, "reachable"};
    L[n++] = (typeof(L[0])){1, logic ? "needs more items" : "to collect"};
    if (!dungeon) L[n++] = (typeof(L[0])){2, "collected"};
    L[n++] = (typeof(L[0])){3, "boss"};
    if (!dungeon) L[n++] = (typeof(L[0])){4, "you"};
    float total = 0, gap = h * 1.2f, dot = h * 0.9f;
    for (int i = 0; i < n; i++) total += dot + gfx_text_width(h, L[i].label) + gap;
    float cx = x - total + gap;
    for (int i = 0; i < n; i++) {
        float py = y + h * 0.55f, rad = h * 0.38f;
        SDL_Color white = {255, 255, 255, 255}, black = {0, 0, 0, 255};
        if (L[i].kind == 0) { map_circle(r, cx + rad, py, rad + 1.5f, white, 0); map_circle(r, cx + rad, py, rad, (SDL_Color){0xFF, 0xB3, 0, 255}, 0); }
        else if (L[i].kind == 1) { map_circle(r, cx + rad, py, rad, (SDL_Color){0x24, 0x30, 0x38, 230}, 0); map_circle(r, cx + rad, py, rad, (SDL_Color){0xF0, 0xF4, 0xF8, 240}, 1); }
        else if (L[i].kind == 2) { map_circle(r, cx + rad, py, rad + 1, black, 0); map_circle(r, cx + rad, py, rad, white, 0); }
        else if (L[i].kind == 3) { gfx_rect(cx, py - rad, 2 * rad, 2 * rad, (SDL_Color){0xE0, 0x5A, 0x4F, 255}); }
        else { map_circle(r, cx + rad, py, rad, (SDL_Color){0xE9, 0xA2, 0x3B, 255}, 0); }
        cx += dot;
        gfx_text(cx, y, h, (SDL_Color){0x87, 0x93, 0xA6, 255}, 0, L[i].label);
        cx += gfx_text_width(h, L[i].label) + gap;
    }
}

void map_draw(SDL_Renderer *r, MapView *v, SDL_Rect area, const SmzTracker *t, const SmzDiscovery *d, int discovered_only,
              const uint8_t *in_logic, const uint8_t *wram, size_t wram_len, Uint32 ticks)
{
    if (!ready) load(r);
    patches_apply();
    if (v->follow && d->has_position) { v->cx = d->x; v->cy = d->y; }
    float z = v->zoom, ox = area.x + area.w / 2.0f - v->cx * z, oy = area.y + area.h / 2.0f - v->cy * z;
    SDL_RenderSetClipRect(r, &area);
    gfx_rect((float)area.x, (float)area.y, (float)area.w, (float)area.h, (SDL_Color){0x0B, 0x0E, 0x12, 255});
    if (map_game_on()) { game_map_draw(r, v, area, wram, wram_len); SDL_RenderSetClipRect(r, NULL); return; }
    if (smz3_maprando()) {   /* its rooms are moved: the vanilla map would point the wrong way */
        float h = fminf(22.0f, area.w / 24.0f);
        gfx_text(area.x + area.w / 2.0f, area.y + area.h / 2.0f - h, h, (SDL_Color){0x87, 0x93, 0xA6, 255}, 1, "Map Rando rebuilds the map:");
        gfx_text(area.x + area.w / 2.0f, area.y + area.h / 2.0f + 4, h, (SDL_Color){0x87, 0x93, 0xA6, 255}, 1, "see the game's own map");
        SDL_RenderSetClipRect(r, NULL);
        return;
    }
    if (!map_tex) { SDL_RenderSetClipRect(r, NULL); return; }
    float sx = map_w / MAP_W, hx = HALF_X, he = HALF_END;
    SDL_FRect whole = {ox + hx * z, oy, (he - hx) * z, MAP_H * z};
    SDL_Rect part = {(int)(hx * sx), 0, (int)(he * sx) - (int)(hx * sx), map_h};
    Uint8 veil = discovered_only ? 20 : 145;
    SDL_SetTextureColorMod(map_tex, veil, veil, veil);
    SDL_RenderCopyF(r, map_tex, &part, &whole);
    SmzRect seen[400];
    int ns = smz_disc_rects(d, seen, 400);
    SDL_SetTextureColorMod(map_tex, 255, 255, 255);
    for (int i = 0; i < ns; i++) {
        SmzRect q = seen[i];
        if (q.x < hx) { q.w += q.x - hx; q.x = hx; }
        if (q.y < 0) { q.h += q.y; q.y = 0; }
        if (q.x + q.w > he) q.w = he - q.x;
        if (q.y + q.h > MAP_H) q.h = MAP_H - q.y;
        if (q.w <= 0 || q.h <= 0) continue;
        SDL_Rect src = {(int)(q.x * sx), (int)(q.y * sx), (int)ceilf(q.w * sx), (int)ceilf(q.h * sx)};
        SDL_FRect dst = {ox + src.x / sx * z, oy + src.y / sx * z, src.w / sx * z, src.h / sx * z};
        SDL_RenderCopyF(r, map_tex, &src, &dst);
    }

    float pulse = 0.5f + 0.5f * sinf(ticks / 1500.0f * 6.2832f), rad = fminf(16, fmaxf(5, 5 + 2 * z));
    SDL_Color amber = {0xFF, 0xB3, 0x00, 255}, white = {255, 255, 255, 255}, black = {0, 0, 0, 255}, dim = {0x24, 0x30, 0x38, 230};
    for (int k = 0; k < d->data->n_markers; k++) {
        const SmzMarker *m = &d->data->markers[k];
        int i = m->index;
        if (i < 0 || i >= (int)smz3_location_count || i >= 512 || !smz3_mode_has(smz3_locations[i].game)) continue;
        float my = (float)(m->y + smz3_marker_dy(i)), px = ox + m->x * z, py = oy + my * z;
        if (px < area.x - 20 || py < area.y - 20 || px > area.x + area.w + 20 || py > area.y + area.h + 20) continue;
        if (discovered_only && !seen_at(seen, ns, (float)m->x, my)) continue;
        int head = stack_head[i], stacked = stack_size[head] > 1, done;
        if (stacked) {
            if (i != head) continue;
            int any = 0;
            done = 1;
            for (unsigned j = 0; j < smz3_location_count && j < 512; j++) {
                if (stack_head[j] != head || (visible && !visible[j] && !t->cleared[j])) continue;
                any = 1;
                if (!t->cleared[j]) done = 0;
            }
            if (!any) continue;
        } else {
            done = t->cleared[i];
            if (!done && visible && !visible[i]) continue;
        }
        int reachable = 1;
        if (!done && in_logic) {
            reachable = 0;
            if (stacked) { for (unsigned j = 0; j < smz3_location_count && j < 512; j++) if (stack_head[j] == head && !t->cleared[j] && in_logic[j] && (!visible || visible[j])) reachable = 1; }
            else reachable = in_logic[i];
        }
        if (done) { map_circle(r, px, py, rad * 0.65f * 1.15f, black, 0); map_circle(r, px, py, rad * 0.65f, white, 0); }
        else if (!reachable) { map_circle(r, px, py, rad * 0.7f, dim, 0); map_circle(r, px, py, rad * 0.7f, (SDL_Color){0xF0, 0xF4, 0xF8, 240}, 1); }
        else {
            float rr = rad * (1 + 0.22f * pulse);
            map_circle(r, px, py, rr + 2, white, 0);
            map_circle(r, px, py, rr + 0.5f, black, 0);
            map_circle(r, px, py, rr - 0.8f, amber, 0);
            if (v->highlight) {
                float hr = rr * (1.9f + 0.5f * pulse);
                map_circle(r, px, py, hr, (SDL_Color){0xFF, 0xD5, 0x4F, 255}, 1);
                map_circle(r, px, py, hr + 1.5f, (SDL_Color){0xFF, 0xB3, 0, 160}, 1);
            }
        }
    }
    SmzBossMark boss[16];
    int nb = wram ? smz_disc_boss_marks(d, wram, wram_len, boss, 16) : 0;
    for (int i = 0; i < nb; i++) {
        float px = ox + boss[i].x * z, py = oy + boss[i].y * z, h = fminf(20, fmaxf(8, 7 + 3 * z));
        if (boss[i].x < hx || boss[i].x > he) continue;
        if (discovered_only && !seen_at(seen, ns, boss[i].x, boss[i].y)) continue;
        SDL_Color c = boss[i].done ? (SDL_Color){0x60, 0x68, 0x78, 255} : (SDL_Color){0xE0, 0x5A, 0x4F, 255};
        gfx_rect(px - h, py - h, 2 * h, 2 * h, (SDL_Color){0, 0, 0, 255});
        gfx_rect(px - h + 2, py - h + 2, 2 * h - 4, 2 * h - 4, c);
        char s[2] = {boss[i].letter, 0};
        gfx_text(px, py - h * 0.8f, h * 1.6f, (SDL_Color){255, 255, 255, 255}, 1, s);
    }
    if (d->has_position) {
        float px = ox + d->x * z, py = oy + d->y * z, pr = fminf(14, fmaxf(7, 6 + 2 * z));
        if ((ticks / 350) % 2 == 0) { map_circle(r, px, py, pr * 1.5f, black, 0); map_circle(r, px, py, pr * 1.15f, white, 0); map_circle(r, px, py, pr, (SDL_Color){0xE9, 0xA2, 0x3B, 255}, 0); }
        else map_circle(r, px, py, pr * 0.45f, white, 0);
    }
    SDL_RenderSetClipRect(r, NULL);
}

SDL_Texture *map_item_icon(SDL_Renderer *r, const char *icon)
{
    static SDL_Texture *caches[2][128];
    static const char *names_s[2][128];
    SDL_Texture **cache = caches[gfx_slot()];
    const char **names = names_s[gfx_slot()];
    char want[64];
    snprintf(want, sizeof want, "%s", icon);
    for (char *c = want; *c; c++) if (*c == ' ') *c = '_';
    for (int i = 0; i < 128 && names[i]; i++) if (!strcmp(names[i], icon)) return cache[i];
    int slot = 0;
    while (slot < 127 && names[slot]) slot++;
    names[slot] = icon;
    for (const PcIcon *p = PC_ITEM_ICONS; p->name; p++) {
        if (strcmp(p->name, want)) continue;
        int w, h, n;
        unsigned char *img = stbi_load_from_memory(p->data, (int)p->len, &w, &h, &n, 4);
        if (!img) break;
        SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_UpdateTexture(t, NULL, img, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
        stbi_image_free(img);
        cache[slot] = t;
        break;
    }
    return cache[slot];
}

SDL_Rect map_icon_art(const char *icon)
{
    static struct { const char *name; SDL_Rect art; } known[8];
    int k = 0;
    for (; k < 8 && known[k].name; k++) if (!strcmp(known[k].name, icon)) return known[k].art;
    SDL_Rect art = {0, 0, 32, 32};
    for (const PcIcon *p = PC_ITEM_ICONS; p->name; p++) {
        if (strcmp(p->name, icon)) continue;
        int w, h, n, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
        unsigned char *img = stbi_load_from_memory(p->data, (int)p->len, &w, &h, &n, 4);
        if (!img) break;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (img[(y * w + x) * 4 + 3]) { if (x < x0) x0 = x; if (y < y0) y0 = y; if (x > x1) x1 = x; if (y > y1) y1 = y; }
        stbi_image_free(img);
        if (x1 >= 0) art = (SDL_Rect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
        break;
    }
    if (k < 8) { known[k].name = icon; known[k].art = art; }
    return art;
}

float map_hash_icons(SDL_Renderer *r, float x, float y, float size, const char *hash)
{
    int ic[5];
    if (!smz_z3_hash_icons(hash, ic)) return 0;
    static char names[32][16];   /* map_item_icon keeps the name pointer as its cache key */
    for (int i = 0; i < 5; i++) {
        char *name = names[ic[i] & 31];
        snprintf(name, sizeof names[0], "z3hash_%02d.png", ic[i] & 31);
        SDL_Texture *t = map_item_icon(r, name);
        SDL_FRect d = {x + i * size * 1.15f, y, size, size};
        if (t) SDL_RenderCopyF(r, t, NULL, &d);
    }
    return size * (5 * 1.15f - 0.15f);
}
