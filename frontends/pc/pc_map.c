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

#define MAP_W 1623.0f
#define MAP_H 958.0f

static SDL_Texture *map_tex, *disc_tex, *ring_tex;
static int map_w, map_h;
static signed short stack_head[512];     /* per location: index of the first check on the same dot, or itself */
static unsigned char stack_size[512];    /* checks on the dot of a head */
static int ready;
static void load(SDL_Renderer *r);

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

void map_view_init(MapView *v) { v->cx = MAP_W / 2; v->cy = MAP_H / 2; v->zoom = 1.4f; v->follow = 1; v->highlight = 0; }

void map_view_move(MapView *v, float dx, float dy, float dz)
{
    if (dx != 0 || dy != 0) { v->follow = 0; v->cx += dx / v->zoom; v->cy += dy / v->zoom; }
    if (dz != 0) { v->zoom *= dz; if (v->zoom < 0.4f) v->zoom = 0.4f; if (v->zoom > 8) v->zoom = 8; }
}

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
    if (v->follow && d->has_position) { v->cx = d->x; v->cy = d->y; }
    float z = v->zoom, ox = area.x + area.w / 2.0f - v->cx * z, oy = area.y + area.h / 2.0f - v->cy * z;
    SDL_RenderSetClipRect(r, &area);
    gfx_rect((float)area.x, (float)area.y, (float)area.w, (float)area.h, (SDL_Color){0x0B, 0x0E, 0x12, 255});
    if (!map_tex) { SDL_RenderSetClipRect(r, NULL); return; }
    float sx = map_w / MAP_W;
    SDL_FRect whole = {ox, oy, MAP_W * z, MAP_H * z};
    Uint8 veil = discovered_only ? 20 : 145;
    SDL_SetTextureColorMod(map_tex, veil, veil, veil);
    SDL_RenderCopyF(r, map_tex, NULL, &whole);
    SmzRect seen[400];
    int ns = smz_disc_rects(d, seen, 400);
    SDL_SetTextureColorMod(map_tex, 255, 255, 255);
    for (int i = 0; i < ns; i++) {
        SmzRect q = seen[i];
        if (q.x < 0) { q.w += q.x; q.x = 0; }
        if (q.y < 0) { q.h += q.y; q.y = 0; }
        if (q.x + q.w > MAP_W) q.w = MAP_W - q.x;
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
        if (i < 0 || i >= (int)smz3_location_count || i >= 512) continue;
        float px = ox + m->x * z, py = oy + m->y * z;
        if (px < area.x - 20 || py < area.y - 20 || px > area.x + area.w + 20 || py > area.y + area.h + 20) continue;
        if (discovered_only && !seen_at(seen, ns, (float)m->x, (float)m->y)) continue;
        int head = stack_head[i], stacked = stack_size[head] > 1, done;
        if (stacked) {
            if (i != head) continue;
            done = 1;
            for (unsigned j = 0; j < smz3_location_count && j < 512; j++) if (stack_head[j] == head && !t->cleared[j]) done = 0;
        } else done = t->cleared[i];
        int reachable = 1;
        if (!done && in_logic) {
            reachable = 0;
            if (stacked) { for (unsigned j = 0; j < smz3_location_count && j < 512; j++) if (stack_head[j] == head && !t->cleared[j] && in_logic[j]) reachable = 1; }
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
    static SDL_Texture *cache[64];
    static const char *names[64];
    char want[64];
    snprintf(want, sizeof want, "%s", icon);
    for (char *c = want; *c; c++) if (*c == ' ') *c = '_';
    for (int i = 0; i < 64 && names[i]; i++) if (!strcmp(names[i], icon)) return cache[i];
    int slot = 0;
    while (slot < 63 && names[slot]) slot++;
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
