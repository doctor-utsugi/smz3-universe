#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_dual.h"
#include "pc_gfx.h"
#include "pc_hints.h"
#include "pc_hotkeys.h"
#include "pc_map.h"
#include "pc_online.h"
#include "pc_settings.h"
#include "pc_shortcuts.h"
#include "smz3_shortcuts.h"
#include "smz3_text.h"

#define RUN_MAP_X 0.46f   /* where the run tab's map starts, as a part of the screen's width */
#define MAX_HITS 256
#define MAX_REGIONS 64
#define HOLD_MS 500
#define DOUBLE_TAP_MS 300
#define LEAVE_MS 3000

enum { TAB_RUN, TAB_ITEMS, TAB_CHECKS, TAB_MAP, TAB_HELP, TAB_CONFIG, TAB_COUNT };
static const char *const TAB_NAMES[TAB_COUNT] = {"RUN", "ITEMS", "CHECKS", "MAP", "HELP", "CONFIG"};

enum { HIT_TAB, HIT_ZOOM, HIT_SLOT, HIT_SHORTCUT, HIT_PICK, HIT_PICK_CLEAR, HIT_PICK_CLOSE, HIT_REGION, HIT_HINT, HIT_REVEAL, HIT_REVEAL_REGION, HIT_CENTER, HIT_FOLLOW, HIT_DUNGEONS,
       HIT_CHIP, HIT_MINI_CHIP, HIT_CONFIG, HIT_HOTKEY, HIT_RESET, HIT_LEAVE, HIT_FIN_MENU, HIT_FIN_CREDITS, HIT_FIN_BACK };
enum { CF_SLOT, CF_SPEED, CF_SHAPE, CF_SCREENS, CF_FIXED, CF_ROW, CF_WEAPONS, CF_DAILY };
enum { VIDEO_ASPECT = 2, VIDEO_DAILY = 6, VIDEO_SCREENS = 10 };   /* settings_video_row's rows */
enum { DRAG_NONE, DRAG_SCROLL, DRAG_PAN };

typedef struct { float x, y, w, h; int id, arg; } Hit;
typedef struct { int game, first, n; const char *name; } Region;

static SDL_Renderer *R;
static DualFrame F;
static int W, H, tab, action, card_up;
static float u;   /* one Android dp on this screen: the Thor's layout in its own units */
static float tab_h;
static Hit hits[MAX_HITS];
static int n_hits;
static float clip_y0, clip_y1;

static float scroll[TAB_COUNT], content[TAB_COUNT];
static int map_fitted, map_dungeon, was_live;
static float mini_zoom = 1;   /* kept while the program runs, as on the Thor */
static SDL_Rect map_area;
static MapView mv, runmv;
static DungeonView dv, minidv;

static Region regions[MAX_REGIONS];
static int n_regions, region_locs[SMZ_TRK_MAX_LOCS];
static uint8_t region_open[MAX_REGIONS];

static uint64_t owned, active;
static int picker = -1;       /* the slot whose item picker is open */
static int region_list;       /* HELP: the regions to reveal are listed */
static char note[SMZ_HELP_TEXT + 16];

static struct { int on, moved, drag, has_hit; float x0, y0, x, y; Uint32 at; Hit hit; } P;
static struct { SDL_FingerID id; float x, y; } finger[2];
static int n_fingers, pinching;
static float pinch_dist;
static Uint32 last_tap;
static float last_tap_x, last_tap_y;
static Uint32 leave_at;   /* the first tap on the way out: a second one soon after leaves */

static SDL_Color C(int r, int g, int b) { SDL_Color c = {r, g, b, 255}; return c; }
#define TEXT C(0xE8, 0xEC, 0xF2)
#define DIM C(0x87, 0x93, 0xA6)
#define AMBER C(0xE9, 0xA2, 0x3B)
#define GREEN C(0x4C, 0xC3, 0x8A)
#define RED C(0xE0, 0x5A, 0x4F)
#define TRACK C(0x1D, 0x24, 0x30)
#define GOLD C(0xFF, 0xD5, 0x4A)

static void build_regions(void)
{
    if (n_regions) return;
    int count[MAX_REGIONS] = {0}, of[SMZ_TRK_MAX_LOCS];
    for (int g = SMZ3_GAME_ZELDA; g <= SMZ3_GAME_METROID; g++)
        for (unsigned i = 0; i < smz3_location_count && i < SMZ_TRK_MAX_LOCS; i++) {
            const smz3_location *l = &smz3_locations[i];
            if ((int)l->game != g) continue;
            int r = 0;
            while (r < n_regions && (regions[r].game != g || strcmp(regions[r].name, l->region))) r++;
            if (r == n_regions) { if (r == MAX_REGIONS) { of[i] = -1; continue; } regions[n_regions++] = (Region){g, 0, 0, l->region}; }
            of[i] = r;
            count[r]++;
        }
    for (int r = 1; r < n_regions; r++) regions[r].first = regions[r - 1].first + count[r - 1];
    for (int g = SMZ3_GAME_ZELDA; g <= SMZ3_GAME_METROID; g++)
        for (unsigned i = 0; i < smz3_location_count && i < SMZ_TRK_MAX_LOCS; i++)
            if ((int)smz3_locations[i].game == g && of[i] >= 0) { Region *r = &regions[of[i]]; region_locs[r->first + r->n++] = (int)i; }
}

static int region_done(const Region *r)
{
    int n = 0;
    for (int i = 0; i < r->n; i++) if (F.t->cleared[region_locs[r->first + i]]) n++;
    return n;
}

static void fmt_time(int64_t ms, char *out, size_t cap)
{
    long long s = ms / 1000;
    snprintf(out, cap, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
}

static float fit_px(const char *s, float w, float px, float min)
{
    float tw = gfx_text_width(px, s);
    if (tw > w) px *= w / tw;
    return px < min ? min : px;
}

/* The text cut to the width, as a TextView ellipsizes it (the font has no ellipsis). */
static const char *cut_text(const char *s, float w, float px)
{
    static char b[96];
    if (gfx_text_width(px, s) <= w) return s;
    int n = snprintf(b, sizeof b - 2, "%s", s);
    if (n > (int)sizeof b - 3) n = (int)sizeof b - 3;
    while (n > 0) {
        strcpy(b + n, "..");
        if (gfx_text_width(px, b) <= w) break;
        n--;
    }
    return b;
}

/* Word wrap to the width; returns the y under the text. */
static float wrapped(float x, float y, float w, float px, SDL_Color c, const char *s)
{
    char line[SMZ_HELP_TEXT + 16];
    while (*s) {
        int n = 0, fit = 0;
        for (;;) {
            int j = n;
            while (s[j] && s[j] != ' ' && s[j] != '\n') j++;
            snprintf(line, sizeof line, "%.*s", j, s);
            if (fit && gfx_text_width(px, line) > w) break;
            fit = j;
            if (s[j] != ' ') break;
            n = j + 1;
        }
        snprintf(line, sizeof line, "%.*s", fit, s);
        if (y + px > clip_y0 && y < clip_y1) gfx_text(x, y, px, c, 0, line);
        y += px * 1.3f;
        s += fit;
        if (*s == ' ' || *s == '\n') s++;
    }
    return y;
}

static void add_hit(float x, float y, float w, float h, int id, int arg)
{
    if (y < clip_y0) { h -= clip_y0 - y; y = clip_y0; }
    if (y + h > clip_y1) h = clip_y1 - y;
    if (h > 0 && n_hits < MAX_HITS) hits[n_hits++] = (Hit){x, y, w, h, id, arg};
}

static const Hit *hit_at(float x, float y)
{
    for (int i = n_hits - 1; i >= 0; i--) {
        const Hit *h = &hits[i];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) return h;
    }
    return NULL;
}

static void bar(float x, float y, float w, float h, float frac, SDL_Color fill)
{
    gfx_rect(x, y, w, h, TRACK);
    if (frac > 0) gfx_rect(x, y, w * (frac > 1 ? 1 : frac), h, fill);
}

static void button(float x, float y, float w, float h, const char *label, int enabled, int id, int arg)
{
    int down = P.on && P.has_hit && P.hit.id == id && P.hit.arg == arg;
    gfx_chamfer(x, y, w, h, 3, C(0x25, 0x32, 0x4A));
    gfx_chamfer(x + 1, y + 1, w - 2, h - 2, 2, down ? C(0x2E, 0x4A, 0x6B) : C(0x14, 0x1B, 0x27));
    float px = fit_px(label, w - 8 * u, 14 * u, 9 * u);
    gfx_text(x + w / 2, y + (h - px) / 2, px, enabled ? TEXT : C(0x4A, 0x55, 0x66), 1, label);
    if (enabled) add_hit(x, y, w, h, id, arg);
}

static void icon_at(const char *icon, const SDL_Rect *src, float x, float y, float w, float h, int alpha)
{
    SDL_Texture *t = map_item_icon(R, icon);
    if (!t) return;
    SDL_SetTextureAlphaMod(t, (Uint8)alpha);
    SDL_FRect d = {x, y, w, h};
    SDL_RenderCopyF(R, t, src, &d);
}

/* A scrolled page: drawing is clipped to it and it returns the y of its first line. */
static float page_begin(int p, float x, float top, float w, float h)
{
    float most = content[p] - h;
    if (scroll[p] > most) scroll[p] = most;
    if (scroll[p] < 0) scroll[p] = 0;
    SDL_Rect clip = {(int)x, (int)top, (int)w, (int)h};
    SDL_RenderSetClipRect(R, &clip);
    clip_y0 = top; clip_y1 = top + h;
    return top - scroll[p];
}

static void page_end(int p, float top, float y)
{
    content[p] = y + scroll[p] - top;
    SDL_RenderSetClipRect(R, NULL);
    clip_y0 = 0; clip_y1 = (float)H;
}

/* ---------- RUN ---------- */

static void gem_shape(float l, float t, float w, float h, SDL_Color c)
{
    float a[8] = {l, t + h * 0.32f, l + w, t + h * 0.32f, l + w * 0.8f, t + h, l + w * 0.2f, t + h};
    float b[8] = {l + w * 0.5f, t, l + w, t + h * 0.32f, l, t + h * 0.32f, l, t + h * 0.32f};
    SDL_Color cs[4] = {c, c, c, c};
    gfx_quad(a, cs);
    gfx_quad(b, cs);
}

/* One line with the four boss tokens and the seven numbered crystals; what is still missing is dimmed. */
static void rewards(float x, float y, float w, float h)
{
    static const char *const TOKEN[4] = {"rewards/kraid_boss_token.png", "rewards/phantoon_boss_token.png", "rewards/draygon_boss_token.png", "rewards/ridley_boss_token.png"};
    float unit = fminf(h, w / (4 + 0.25f + 7 * 0.7f + 0.2f));
    for (int i = 0; i < 4; i++) {
        SDL_Rect art = map_icon_art(TOKEN[i]);
        float th = unit * art.h / art.w;
        icon_at(TOKEN[i], &art, x, y + (h - th) / 2, unit, th, (F.t->boss_mask >> i & 1) ? 255 : 90);
        x += unit;
    }
    x += unit * 0.25f;
    float gw = unit * 0.7f, gh = unit * 0.9f, gt = y + (h - gh) / 2, in = 1.5f * u * 0.7f;
    for (int n = 1; n <= 7; n++) {
        int done = F.t->crystal_mask >> (n - 1) & 1, red = n == 5 || n == 6;
        gem_shape(x, gt, gw, gh, done ? C(0xE8, 0xF6, 0xFF) : C(0x66, 0x70, 0x80));
        gem_shape(x + in, gt + in * 1.3f, gw - 2 * in, gh - 2.2f * in, !done ? C(0x2A, 0x33, 0x40) : red ? C(0xFF, 0x6B, 0x6B) : C(0x4F, 0xC3, 0xF7));
        char d[2] = {(char)('0' + n), 0};
        gfx_text(x + gw / 2, gt + gh * 0.3f, gh * 0.5f, done ? C(0x0A, 0x22, 0x30) : C(0x8C, 0x97, 0xA3), 1, d);
        x += gw;
    }
}

static void run_left(float x, float top, float cw, float h)
{
    const SmzTracker *t = F.t;
    unsigned done = t->zelda_cleared + t->metroid_cleared, total = t->zelda_total + t->metroid_total;
    char s[96];
    float y = page_begin(TAB_RUN, 0, top, x + cw + 4 * u, h);
    y += 4 * u;
    snprintf(s, sizeof s, "%u%%", total ? done * 100 / total : 0);
    gfx_text(x, y, 46 * u, AMBER, 0, s);
    float hx = x + gfx_text_width(46 * u, s) + 8 * u, hw = x + cw - hx;
    const char *g = !t->started ? "Waiting for a file" : t->game == SMZ3_GAME_ZELDA ? "A Link to the Past" : t->game == SMZ3_GAME_METROID ? "Super Metroid" : t->game == SMZ3_GAME_CREDITS ? "Credits" : "Unknown";
    gfx_text(hx, y + 6 * u, fit_px(g, hw, 14 * u, 8 * u), AMBER, 0, g);
    int64_t played = t->started ? t->elapsed_ms : 0;
    fmt_time(F.clock_ms >= 0 ? F.clock_ms : played, s, sizeof s);
    gfx_text(hx, y + 24 * u, fit_px(s, hw, 20 * u, 12 * u), TEXT, 0, s);
    y += 54 * u;
    snprintf(s, sizeof s, "Zelda  %u / %u", t->zelda_cleared, t->zelda_total);
    gfx_text(x, y, 13 * u, TEXT, 0, s); y += 17 * u;
    bar(x, y, cw, 8 * u, t->zelda_total ? (float)t->zelda_cleared / t->zelda_total : 0, GREEN); y += 12 * u;
    snprintf(s, sizeof s, "Metroid  %u / %u", t->metroid_cleared, t->metroid_total);
    gfx_text(x, y, 13 * u, TEXT, 0, s); y += 17 * u;
    bar(x, y, cw, 8 * u, t->metroid_total ? (float)t->metroid_cleared / t->metroid_total : 0, RED); y += 12 * u;
    snprintf(s, sizeof s, "Checks  %u / %u", done, total);
    gfx_text(x, y, 13 * u, TEXT, 0, s); y += 16 * u;
    int items = 0;
    for (unsigned i = 0; i < smz3_inventory_count && i < SMZ_TRK_MAX_INV; i++) if (t->inventory[i] > 0) items++;
    char rate[16] = "-";
    if (played >= 300000) snprintf(rate, sizeof rate, "%.1f", done * 3600000.0 / played);
    snprintf(s, sizeof s, "Items  %d / %u   -   %s/h", items, smz3_inventory_count, rate);
    gfx_text(x, y, 13 * u, TEXT, 0, s); y += 20 * u;
    rewards(x, y, cw, 28 * u); y += 32 * u;
    if (t->n_recent_items) {
        gfx_text(x, y, 12 * u, C(0xC8, 0xD0, 0xD8), 0, "RECENT ITEMS"); y += 16 * u;
        for (int i = 0; i < t->n_recent_items; i++) {
            const smz3_inv_item *it = &smz3_inventory_items[t->recent_items[i]];
            float ix = x + (i % 2) * cw / 2, iy = y + (i / 2) * 22 * u;
            icon_at(it->icon, NULL, ix, iy, 22 * u, 22 * u, 255);
            gfx_text(ix + 26 * u, iy + 4 * u, 12 * u, C(255, 255, 255), 0, cut_text(it->name, cw / 2 - 28 * u, 12 * u));
        }
        y += (t->n_recent_items + 1) / 2 * 22 * u + 4 * u;
    }
    int best[2] = {-1, -1};   /* the regions nearest to being finished */
    for (int r = 0; r < n_regions; r++) {
        int d = region_done(&regions[r]), left = regions[r].n - d;
        if (d < 1 || !left) continue;
        for (int k = 0; k < 2; k++)
            if (best[k] < 0 || left < regions[best[k]].n - region_done(&regions[best[k]])) {
                if (k == 0) best[1] = best[0];
                best[k] = r;
                break;
            }
    }
    if (best[0] >= 0) {
        gfx_text(x, y, 12 * u, DIM, 0, "NEARLY DONE"); y += 15 * u;
        for (int k = 0; k < 2 && best[k] >= 0; k++, y += 15 * u) {
            const Region *r = &regions[best[k]];
            snprintf(s, sizeof s, "%s: %d left", r->name, r->n - region_done(r));
            gfx_text(x, y, 12 * u, DIM, 0, cut_text(s, cw, 12 * u));
        }
    }
    page_end(TAB_RUN, top, y + 4 * u);
}

static void shortcut_icon(int id, float x, float y, float s, int alpha)
{
    const SmzShortcut *sc = &smz_shortcuts[id];
    size_t n = strlen(sc->name);
    if (sc->icon[0]) icon_at(sc->icon, NULL, x, y, s, s, alpha);
    else gfx_text(x + s / 2, y + s / 2 - 6 * u, fit_px(sc->name, s, 11 * u, 7 * u), C(255, 255, 255), 1, sc->name);
    if (n && sc->name[n - 1] >= '0' && sc->name[n - 1] <= '9') {   /* the four bottles share one icon */
        char d[2] = {sc->name[n - 1], 0};
        gfx_text(x + s + 1, y + s - 13 * u, 12 * u, C(0, 0, 0), 2, d);
        gfx_text(x + s, y + s - 14 * u, 12 * u, C(255, 255, 255), 2, d);
    }
}

static void shortcut_cell(float x, float y, float w, float h, int id, int hit, int arg)
{
    int on = id >= 0 && (active >> id & 1), have = id >= 0 && (owned >> id & 1);
    SDL_Color back = on ? (SDL_Color){0xE9, 0xA2, 0x3B, 110} : (SDL_Color){0, 0, 0, 150};
    gfx_rect(x + 2 * u, y, w - 4 * u, h, back);
    if (id >= 0) {
        float s = fminf(w - 12 * u, h - 6 * u);
        shortcut_icon(id, x + (w - s) / 2, y + (h - s) / 2, s, have ? 255 : 64);
    } else gfx_text(x + w / 2, y + (h - 18 * u) / 2, 18 * u, DIM, 1, "+");
    add_hit(x, y, w, h, hit, arg);
}

/* The rows over the mini map: five ALttP slots, or the beams owned and under them the HUD weapons owned. Returns the y under them. */
static float shortcut_rows(float x, float y, float w)
{
    int game = F.t->started ? (int)F.t->game : 0, on = (game == SMZ3_GAME_ZELDA || game == SMZ3_GAME_METROID) && sc_row_shown();
    if (on && game == SMZ3_GAME_ZELDA) {
        for (int i = 0; i < SC_SLOTS; i++) shortcut_cell(x + i * w / SC_SLOTS, y + 2 * u, w / SC_SLOTS, 44 * u, sc_slot(i), HIT_SLOT, i);
        y += 48 * u;
    } else if (on) {
        int k = 0;
        for (int id = SC_WAVE; id < SC_WEAPON_FIRST; id++) if (owned >> id & 1) { shortcut_cell(x + k * w / 4, y + 2 * u, w / 4, 44 * u, id, HIT_SHORTCUT, id); k++; }
        y += 48 * u;
    }
    if (game == SMZ3_GAME_METROID && sc_weapon_row_shown()) {
        int k = 0;
        for (int id = SC_WEAPON_FIRST; id < SC_COUNT; id++) if (owned >> id & 1) { shortcut_cell(x + k * w / 4, y, w / 4, 44 * u, id, HIT_SHORTCUT, id); k++; }
        if (k) y += 46 * u;
    }
    return y;
}

static void zoom_button(float x, float y, float w, float h, const char *label, int dir)
{
    dungeon_chip_draw(x, y, w, h, label, 0);
    add_hit(x, y, w, h, HIT_ZOOM, dir);
}

/* The map that always follows the player; the floor Link is on while he is in a dungeon. */
static void mini_map(SDL_Rect a)
{
    const DungeonLive *live = F.live;
    if (minidv.picked && (!live || live->d != minidv.d)) minidv.picked = 0;   /* a floor picked here belongs to one dungeon visit */
    if (live && dungeon_available(R)) {
        DungeonChip chips[24];
        int n = dungeon_panel(R, &minidv, a, u, 1, mini_zoom, F.t, F.in_logic, live, F.wram, F.wram_len, F.sram, F.sram_len, !F.discovered_only, SDL_GetTicks(), chips, 24);
        for (int i = 0; i < n; i++) add_hit(chips[i].x, chips[i].y, chips[i].w, chips[i].h, HIT_MINI_CHIP, chips[i].act | chips[i].arg << 4);
    } else {
        if (F.disc->has_position) { runmv.follow = 1; runmv.zoom = a.w / 240.0f * mini_zoom; }
        else { runmv.follow = 0; map_view_fit(&runmv, a); }
        map_draw(R, &runmv, a, F.t, F.disc, F.discovered_only, F.in_logic, F.wram, F.wram_len, SDL_GetTicks());
    }
    float bx = a.x + a.w - 46 * u, by = a.y + a.h - 44 * u;
    zoom_button(bx, by, 40 * u, 38 * u, "+", 1);
    zoom_button(bx - 46 * u, by, 40 * u, 38 * u, "-", -1);
}

/* The item picker of a slot, over the whole page. */
static void slot_picker(float top)
{
    int ids[SC_ZELDA_COUNT], n = 0, cur = sc_slot(picker);
    float x = 10 * u, w = W - 20 * u, y = top + 8 * u;
    char s[64];
    gfx_rect(0, top, (float)W, H - top, C(8, 12, 18));
    if (cur >= 0) snprintf(s, sizeof s, "Slot %d: %s", picker + 1, smz_shortcuts[cur].name);
    else snprintf(s, sizeof s, "Slot %d: tap an item to put in it", picker + 1);
    gfx_text(x, y, 15 * u, GOLD, 0, s);
    y += 22 * u;
    for (int id = 0; id < SC_ZELDA_COUNT; id++) if (owned >> id & 1) ids[n++] = id;
    float gh = H - y - 52 * u;
    if (!n) gfx_text(x, y, 14 * u, C(0xC8, 0xD0, 0xD8), 0, "No items yet");
    else {
        float cell = 0;
        int cols = 1;
        for (int c = 1; c <= n; c++) {
            float size = fminf(w / c, gh / ((n + c - 1) / c));
            if (size > cell) { cell = size; cols = c; }
        }
        if (cell > 120 * u) cell = 120 * u;
        float left = x + (w - cell * cols) / 2;
        for (int i = 0; i < n; i++) {
            float cx = left + (i % cols) * cell, cy = y + (i / cols) * cell;
            if (ids[i] == cur) gfx_rect(cx, cy, cell, cell, (SDL_Color){0xFF, 0xD5, 0x4A, 160});
            shortcut_icon(ids[i], cx + 6 * u, cy + 6 * u, cell - 12 * u, 255);
            add_hit(cx, cy, cell, cell, HIT_PICK, ids[i]);
        }
    }
    float by = H - 46 * u, bw = (w - 8 * u) / 2;
    button(x, by, bw, 40 * u, "CLEAR", 1, HIT_PICK_CLEAR, 0);
    button(x + bw + 8 * u, by, bw, 40 * u, "CLOSE", 1, HIT_PICK_CLOSE, 0);
}

static void page_run(float top)
{
    if (picker >= 0) { slot_picker(top); return; }
    float rx = W * RUN_MAP_X, rw = W - rx;
    run_left(12 * u, top, rx - 16 * u, H - top);
    float my = shortcut_rows(rx, top, rw);
    mini_map((SDL_Rect){(int)rx, (int)my, (int)rw, (int)(H - my)});
}

/* ---------- ITEMS ---------- */

static int shows_count(const char *name, int level)
{
    static const char *const COUNTED[] = {"Bottles", "Hearts", "Heart Pieces", "Energy Tanks", "Reserve", "Missiles", "Super Missiles", "Power Bombs"};
    static const char *const LEVELS[] = {"Sword", "Shield", "Mail", "Gloves"};
    for (int i = 0; i < 8; i++) if (!strcmp(name, COUNTED[i])) return level > 0;
    for (int i = 0; i < 4; i++) if (!strcmp(name, LEVELS[i])) return level > 1;
    return 0;
}

/* One grid for both games, A Link to the Past first; the square cell is what the page allows. */
static void page_items(float top)
{
    int columns = 9, n = smz3_inventory_count < SMZ_TRK_MAX_INV ? (int)smz3_inventory_count : SMZ_TRK_MAX_INV, rows = (n + columns - 1) / columns, k = 0;
    float iw = W - 24 * u, ih = H - top - 8 * u, cell = fminf(iw / columns, ih / rows), left = 12 * u + (iw - cell * columns) / 2, y = top + 4 * u;
    for (int g = SMZ3_GAME_ZELDA; g <= SMZ3_GAME_METROID; g++)
        for (int i = 0; i < n; i++) {
            const smz3_inv_item *it = &smz3_inventory_items[i];
            if ((int)it->game != g) continue;
            float cx = left + (k % columns) * cell, cy = y + (k / columns) * cell;
            int level = F.t->inventory[i];
            k++;
            icon_at(it->icon, NULL, cx + 3 * u, cy + 3 * u, cell - 6 * u, cell - 6 * u, level > 0 ? 255 : 51);
            if (!shows_count(it->name, level)) continue;
            char s[12];
            snprintf(s, sizeof s, "%d", level);
            gfx_text(cx + cell - 2 * u, cy + cell - 16 * u, 13 * u, C(0, 0, 0), 2, s);
            gfx_text(cx + cell - 3 * u, cy + cell - 17 * u, 13 * u, TEXT, 2, s);
        }
}

/* ---------- CHECKS ---------- */

static void tick_mark(float x, float y, float s, SDL_Color c)
{
    float t = s * 0.1f, a[8] = {x, y + s * 0.5f, x + s * 0.36f, y + s * 0.86f, x + s * 0.36f, y + s * 0.86f + 2 * t, x, y + s * 0.5f + 2 * t};
    float b[8] = {x + s * 0.36f, y + s * 0.86f, x + s, y + s * 0.14f, x + s, y + s * 0.14f + 2 * t, x + s * 0.36f, y + s * 0.86f + 2 * t};
    SDL_Color cs[4] = {c, c, c, c};
    gfx_quad(a, cs);
    gfx_quad(b, cs);
}

static void check_row(int i, float x, float y, float w)
{
    const uint8_t *vis = hints_visible();
    const char *name = smz3_locations[i].name;
    float px = 13 * u, tx = x + 16 * u;
    if (F.t->cleared[i]) {
        tick_mark(x, y + 2 * u, 11 * u, GREEN);
        gfx_text(tx, y, px, GREEN, 0, cut_text(name, w - 16 * u, px));
    } else if ((F.in_logic && F.in_logic[i]) || (vis && vis[i])) {
        map_circle(R, x + 5 * u, y + px / 2, 3 * u, TEXT, 0);
        gfx_text(tx, y, px, TEXT, 0, cut_text(name, w - 16 * u, px));
    } else if (vis) gfx_text(tx, y, px, DIM, 0, "???");
    else gfx_text(tx, y, px, DIM, 0, cut_text(name, w - 16 * u, px));
}

static void page_checks(float top)
{
    float x = 16 * u, w = W - 32 * u, h = H - top, y = page_begin(TAB_CHECKS, 0, top, (float)W, h);
    int game = 0;
    char s[96];
    for (int r = 0; r < n_regions; r++) {
        const Region *g = &regions[r];
        if (g->game != game) {
            game = g->game;
            y += 8 * u;
            gfx_text(x, y, 12 * u, DIM, 0, game == SMZ3_GAME_ZELDA ? "A LINK TO THE PAST" : "SUPER METROID");
            y += 18 * u;
        }
        int done = region_done(g), open = 0;
        if (F.in_logic) for (int i = 0; i < g->n; i++) { int l = region_locs[g->first + i]; if (!F.t->cleared[l] && F.in_logic[l]) open++; }
        int n = snprintf(s, sizeof s, "%s  %d / %d", g->name, done, g->n);
        if (open) snprintf(s + n, sizeof s - (size_t)n, "   +%d in logic", open);
        if (y + 40 * u > top && y < top + h) {
            gfx_text(x, y + 6 * u, 14 * u, done == g->n ? GREEN : TEXT, 0, s);
            bar(x, y + 26 * u, w, 8 * u, (float)done / g->n, AMBER);
            add_hit(0, y, (float)W, 36 * u, HIT_REGION, r);
        }
        y += 38 * u;
        if (!region_open[r]) continue;
        for (int i = 0; i < g->n; i++, y += 21 * u)
            if (y + 21 * u > top && y < top + h) check_row(region_locs[g->first + i], x + 18 * u, y + 2 * u, w - 18 * u);
    }
    page_end(TAB_CHECKS, top, y + 12 * u);
}

/* ---------- MAP ---------- */

static void map_button(float *right, float y, const char *label, int id)
{
    float px = 14 * u, w = gfx_text_width(px, label) + 28 * u, h = 34 * u, x = *right - w;
    gfx_rect(x, y, w, h, (SDL_Color){0, 0, 0, 170});
    gfx_text(x + 14 * u, y + 10 * u, px, AMBER, 0, label);
    add_hit(x, y, w, h, id, 0);
    *right = x - 8 * u;
}

static void page_map(float top)
{
    map_area = (SDL_Rect){0, (int)top, W, (int)(H - top)};
    if (!map_fitted) { map_view_fit(&mv, map_area); map_fitted = 1; }
    map_dungeon = dungeon_showing(&dv, F.live);
    if (map_dungeon) {
        DungeonChip chips[24];
        int n = dungeon_panel(R, &dv, map_area, u, 0, 1, F.t, F.in_logic, F.live, F.wram, F.wram_len, F.sram, F.sram_len, !F.discovered_only, SDL_GetTicks(), chips, 24);
        for (int i = 0; i < n; i++) add_hit(chips[i].x, chips[i].y, chips[i].w, chips[i].h, HIT_CHIP, chips[i].act | chips[i].arg << 4);
        return;
    }
    map_draw(R, &mv, map_area, F.t, F.disc, F.discovered_only, F.in_logic, F.wram, F.wram_len, SDL_GetTicks());
    if (dungeon_available(R)) {
        dungeon_chip_draw(6 * u, H - 38 * u, 92 * u, 32 * u, "DUNGEONS", 0);
        add_hit(6 * u, H - 38 * u, 92 * u, 32 * u, HIT_DUNGEONS, 0);
    }
    float right = W - 10 * u, by = H - 44 * u;
    map_button(&right, by, "[CENTER]", HIT_CENTER);
    map_button(&right, by, mv.follow ? "[FOLLOW: ON]" : "[FOLLOW]", HIT_FOLLOW);
}

static void map_center(void)
{
    if (F.disc && F.disc->has_position) { mv.zoom = map_area.w / 240.0f * mini_zoom; mv.cx = F.disc->x; mv.cy = F.disc->y; }
    else map_view_fit(&mv, map_area);
}

static void map_pinch(float fx, float fy, float k)
{
    float fit = fminf(map_area.w / 1623.0f, map_area.h / 958.0f), z0 = mv.zoom, z1 = z0 * k;
    z1 = z1 < fit * 0.5f ? fit * 0.5f : z1 > 16 ? 16 : z1;
    float ax = fx - map_area.x - map_area.w / 2.0f, ay = fy - map_area.y - map_area.h / 2.0f;   /* the point under the fingers stays there */
    mv.cx += ax * (1 / z0 - 1 / z1);
    mv.cy += ay * (1 / z0 - 1 / z1);
    mv.zoom = z1;
}

/* ---------- HELP ---------- */

static void page_daily(float x, float w, float y, float top)
{
    const SmzDailyRun *dr = F.daily;
    int64_t now = online_now_ms(), left = dr->closes_at - now;
    char a[32], b[32], s[SMZ_DAILY_HINT_LEN + 16];
    y = wrapped(x, y, w, 16 * u, AMBER, dr->goal);
    smz_daily_span(left, a, sizeof a);
    fmt_time(now - smz_daily_clock_from(dr), b, sizeof b);
    if (left > 0) snprintf(s, sizeof s, "Window closes in %s    Own time %s", a, b);
    else snprintf(s, sizeof s, "The window has closed: no prize    Own time %s", b);
    y = wrapped(x, y + 4 * u, w, 13 * u, left < 15 * 60000 ? RED : DIM, s);
    if (dr->next_hint_at) {
        smz_daily_span(dr->next_hint_at - now, a, sizeof a);
        snprintf(s, sizeof s, "Hints come on a timer. Next one in %s.", a);
    } else snprintf(s, sizeof s, "Every hint is out.");
    y = wrapped(x, y + 8 * u, w, 16 * u, TEXT, s);
    if (dr->n_hints) y = wrapped(x, y + 12 * u, w, 14 * u, DIM, "Hints so far") + 6 * u;
    for (int i = 0; i < dr->n_hints; i++) {
        snprintf(s, sizeof s, "%lld min   %s", (long long)((dr->hint_at[i] - dr->started_at) / 60000), dr->hint[i]);
        y = wrapped(x, y, w, 14 * u, TEXT, s) + 8 * u;
    }
    page_end(TAB_HELP, top, y + 12 * u);
}

static void page_help(float top)
{
    float x = 16 * u, w = W - 32 * u, y = page_begin(TAB_HELP, 0, top, (float)W, H - top) + 12 * u;
    int mode = hints_mode(), can = hints_can_ask();
    char s[64];
    if (F.daily) { page_daily(x, w, y, top); return; }
    if (mode == HELP_ALL) y = wrapped(x, y, w, 16 * u, TEXT, "All helps are on: hints are unlimited and the map shows what is left.");
    else { snprintf(s, sizeof s, "Helps left: %d of %d", hints_left(), hints_total()); y = wrapped(x, y, w, 16 * u, TEXT, s); }
    if (note[0]) y = wrapped(x, y + 8 * u, w, 16 * u, TEXT, note);
    y += 10 * u;
    if (hints_count() < SMZ_HELP_COUNT) {
        button(x, y, w, 40 * u, hints_busy() ? "Thinking..." : "Give me a hint", can && !hints_busy(), HIT_HINT, 0);
        y += 46 * u;
    }
    if (mode == HELP_LIMITED) {
        button(x, y, w, 40 * u, "Show the checks left in a region", can, HIT_REVEAL, 0);
        y += 46 * u;
        for (int r = 0; region_list && r < n_regions; r++, y += 40 * u) {
            char label[96];
            snprintf(label, sizeof label, "%s  (%s)", regions[r].name, regions[r].game == SMZ3_GAME_ZELDA ? "Zelda" : "Metroid");
            button(x, y, w, 36 * u, label, 1, HIT_REVEAL_REGION, r);
        }
    }
    if (hints_count()) y = wrapped(x, y + 8 * u, w, 14 * u, DIM, "Hints so far") + 6 * u;
    for (int i = 0; i < hints_count(); i++) y = wrapped(x, y, w, 14 * u, TEXT, hints_text(i)) + 8 * u;
    page_end(TAB_HELP, top, y + 12 * u);
}

/* ---------- CONFIG ---------- */

static float cfg_head(float x, float y, const char *s)
{
    gfx_text(x, y + 14 * u, 13 * u, DIM, 0, s);
    return y + 34 * u;
}

static float cfg_row(float x, float y, float w, const char *title, const char *value, float bw, int arg)
{
    gfx_text(x, y + 10 * u, fit_px(title, w - bw - 8 * u, 16 * u, 10 * u), TEXT, 0, title);
    button(x + w - bw, y, bw, 36 * u, value, 1, HIT_CONFIG, arg);
    return y + 40 * u;
}

static void page_config(float top)
{
    float x = 16 * u, w = W - 32 * u, y = page_begin(TAB_CONFIG, 0, top, (float)W, H - top), b = 92 * u, wide = 150 * u;
    char s[48];
    int armed = leave_at && SDL_GetTicks() - leave_at < LEAVE_MS;
    button(x, y + 8 * u, w, 36 * u, armed ? "Tap again to leave" : "Save and leave the run", 1, HIT_LEAVE, 0);
    y += 44 * u;
    y = cfg_head(x, y, "HOTKEYS  (tap a button to change it; hold the first, then press the second)");
    for (int a = 0; a < HK_COUNT; a++) {
        gfx_text(x, y + 10 * u, fit_px(hk_title(a), w - 2 * b - 12 * u, 16 * u, 10 * u), TEXT, 0, hk_title(a));
        button(x + w - 2 * b - 4 * u, y, b, 36 * u, hk_name(a, 0), 1, HIT_HOTKEY, a * 2);
        button(x + w - b, y, b, 36 * u, hk_name(a, 1), 1, HIT_HOTKEY, a * 2 + 1);
        y += 40 * u;
    }
    y = cfg_head(x, y, "SAVE STATES AND SPEED");
    if (F.qslot) { snprintf(s, sizeof s, "Slot %d", *F.qslot + 1); y = cfg_row(x, y, w, "Active save slot", s, b, CF_SLOT); }
    snprintf(s, sizeof s, "x%d", settings_ff_speed());
    y = cfg_row(x, y, w, "Fast-forward speed", s, b, CF_SPEED);
    y = cfg_row(x, y, w, "Screen shape", settings_aspect_name(), wide, CF_SHAPE);
    button(x, y + 8 * u, wide, 36 * u, "Reset to defaults", 1, HIT_RESET, 0);
    y = wrapped(x, y + 52 * u, w, 12 * u, DIM, "The hotkeys work in game on the main screen. The state is saved in the active slot.");
    y = cfg_head(x, y, "SCREENS");
    settings_video_row(VIDEO_SCREENS, s, sizeof s);
    y = cfg_row(x, y, w, "Screen mode", s, wide, CF_SCREENS);
    y = wrapped(x, y + 2 * u, w, 12 * u, DIM, "Dual puts the tracker on a second screen, single puts it over the game. It changes the next time SMZ3 opens.");
    y = cfg_head(x, y, "DUNGEON MAPS");
    y = cfg_row(x, y, w, "Fixed keys", hints_fixed_keys() ? "Shown" : "Hidden", b, CF_FIXED);
    y = cfg_head(x, y, "ITEM SHORTCUTS  (the row over the RUN tab's map)");
    y = cfg_row(x, y, w, "Shortcut row", sc_row_shown() ? "Shown" : "Hidden", b, CF_ROW);
    y = cfg_row(x, y, w, "Weapon row", sc_weapon_row_shown() ? "Shown" : "Hidden", b, CF_WEAPONS);
    y = wrapped(x, y + 2 * u, w, 12 * u, DIM, "Tap a slot to equip its item on Y, a beam to turn it on or off, or a weapon to pick it (again: back to the beam). Hold a slot to change it.");
    y = cfg_head(x, y, "DAILY CHALLENGE  (over the game)");
    for (int i = 0; i < 4; i++) {
        const char *title = settings_video_row(VIDEO_DAILY + i, s, sizeof s);
        y = cfg_row(x, y, w, title, s, b, CF_DAILY + i);
    }
    page_end(TAB_CONFIG, top, y + 12 * u);
}

static void config_tap(int arg)
{
    if (arg == CF_SLOT) { if (F.qslot && F.qslots) *F.qslot = (*F.qslot + 1) % F.qslots; }
    else if (arg == CF_SPEED) settings_ff_next();
    else if (arg == CF_SHAPE) settings_video_change(VIDEO_ASPECT, 1);
    else if (arg == CF_SCREENS) settings_video_change(VIDEO_SCREENS, 1);
    else if (arg == CF_FIXED) hints_set_fixed_keys(!hints_fixed_keys());
    else if (arg == CF_ROW) sc_toggle_row();
    else if (arg == CF_WEAPONS) sc_toggle_weapon_row();
    else settings_video_change(VIDEO_DAILY + arg - CF_DAILY, 1);
}

static void config_reset(void)
{
    hk_reset();
    if (F.qslot) *F.qslot = 0;
    while (settings_ff_speed() != 4) settings_ff_next();
    for (int i = 0; i < 8 && strcmp(settings_aspect_name(), "Run default"); i++) settings_video_change(VIDEO_ASPECT, 1);
}

/* ---------- the portal ---------- */

static int last_game;
static Uint32 portal_at;
static float portal_hold;   /* test hook: the portal stands still at this point */
static SDL_Color portal_from;

static void portal_arc(float cx, float cy, float r, float from, float sweep, float w, SDL_Color c)
{
    if (r < 1) return;
    float step = w * 0.6f / r;
    for (float a = from; a < from + sweep; a += step) gfx_rect(cx + r * cosf(a) - w / 2, cy + r * sinf(a) - w / 2, w, w, c);
}

/* The old game's colour over the tracker, a violet ring opening in the middle and spinning as it grows, and the new game showing through the hole while it fades in. */
static void portal_draw(void)
{
    float t = portal_hold ? portal_hold : (SDL_GetTicks() - portal_at) / 1000.0f;
    if (t >= 1) { portal_at = 0; return; }
    t = powf(t, 2.8f);   /* Android's AccelerateInterpolator(1.4) */
    float cx = W / 2.0f, cy = H / 2.0f, r = hypotf(cx, cy) * 1.05f * t, tau = 6.2831853f;
    SDL_Color cover = portal_from;
    cover.a = (Uint8)(255 * (1 - t * 0.35f));
    gfx_rect(0, 0, (float)W, (float)H, (SDL_Color){0x08, 0x0A, 0x10, (Uint8)(255 * (1 - t))});
    for (int y = 0; y < H; y++) {
        float dy = y + 0.5f - cy, half = r * r > dy * dy ? sqrtf(r * r - dy * dy) : -1;
        if (half < 0) gfx_rect(0, (float)y, (float)W, 1, cover);
        else {
            if (cx - half > 0) gfx_rect(0, (float)y, cx - half, 1, cover);
            if (cx + half < W) gfx_rect(cx + half, (float)y, W - cx - half, 1, cover);
        }
    }
    int fade = (int)(255 * (1 - t));
    float w = u * (5 - 3 * t), spin = tau * t;
    SDL_Color ring = {0xB4, 0x8C, 0xF0, (Uint8)fade}, glow = ring, soft = ring, half = ring;
    glow.a = (Uint8)(fade / 6); soft.a = (Uint8)(fade / 12); half.a = (Uint8)(fade / 2);
    portal_arc(cx, cy, r, 0, tau, w * 5, soft);
    portal_arc(cx, cy, r, 0, tau, w * 3, glow);
    portal_arc(cx, cy, r, spin, tau * 250 / 360, w, ring);
    portal_arc(cx, cy, r, spin + tau * 270 / 360, tau * 60 / 360, w, half);
}

static void portal_check(void)
{
    int game = F.t->started ? (int)F.t->game : 0;
    if (game != SMZ3_GAME_ZELDA && game != SMZ3_GAME_METROID) return;
    if (last_game && game != last_game) {
        portal_at = SDL_GetTicks();
        portal_from = last_game == SMZ3_GAME_ZELDA ? C(0x14, 0x23, 0x1A) : C(0x05, 0x08, 0x0C);
    }
    last_game = game;
}

/* ---------- the frame ---------- */

static void draw_tabs(void)
{
    int shown[TAB_COUNT], n = 0, help = hints_mode() != HELP_NONE || F.daily;
    if (tab == TAB_HELP && !help) tab = TAB_RUN;
    for (int i = 0; i < TAB_COUNT; i++) if (i != TAB_HELP || help) shown[n++] = i;
    float tw = (float)W / n;
    tab_h = 30 * u;
    for (int i = 0; i < n; i++) {
        int on = shown[i] == tab;
        const char *s = TAB_NAMES[shown[i]];
        float px = fit_px(s, tw - 6 * u, 15 * u, 9 * u);
        gfx_rect(i * tw, 0, tw, tab_h, on ? C(0x2E, 0x4A, 0x6B) : C(0x08, 0x0B, 0x11));
        gfx_text(i * tw + tw / 2, (tab_h - px) / 2, px, on ? AMBER : DIM, 1, s);
        add_hit(i * tw, 0, tw, tab_h, HIT_TAB, shown[i]);
    }
}

static void read_shortcuts(void)
{
    if (!F.wram || !F.sram || !smz_shortcut_ready(F.wram, F.wram_len, F.sram, F.sram_len)) return;   /* out of play the last reading stands, as on the Thor */
    owned = active = 0;
    for (int id = 0; id < SC_COUNT; id++) {
        if (smz_shortcut_owned(id, F.wram, F.wram_len, F.sram, F.sram_len)) owned |= 1ull << id;
        if (smz_shortcut_active(id, F.wram, F.wram_len, F.sram, F.sram_len)) active |= 1ull << id;
    }
}

void dual_draw(SDL_Renderer *r, const DualFrame *f, int w, int h)
{
    R = r; F = *f; W = w; H = h;
    u = fminf(h / 370.0f, w / 470.0f);
    n_hits = 0;
    clip_y0 = 0; clip_y1 = (float)h;
    build_regions();
    read_shortcuts();
    hints_poll(note, sizeof note);
    if (was_live && !F.live && dv.world) { dv.world = 0; dv.picked = 0; }   /* leaving a dungeon brings the map back to its usual choice */
    was_live = F.live != NULL;
    if (P.on && P.has_hit && P.hit.id == HIT_SLOT && SDL_GetTicks() - P.at >= HOLD_MS) { picker = P.hit.arg; P.has_hit = 0; }
    draw_tabs();
    float top = tab_h;
    if (tab == TAB_RUN) page_run(top);
    else if (tab == TAB_ITEMS) page_items(top);
    else if (tab == TAB_CHECKS) page_checks(top);
    else if (tab == TAB_MAP) page_map(top);
    else if (tab == TAB_HELP) page_help(top);
    else page_config(top);
    card_up = f->card != NULL;
    if (card_up) {
        FinishButtons b;
        n_hits = 0;
        finish_screen(f->card, w, h, f->card_open, &b);
        if (b.n == 2) {
            add_hit(b.menu.x, b.menu.y, b.menu.w, b.menu.h, HIT_FIN_MENU, 0);
            add_hit(b.credits.x, b.credits.y, b.credits.w, b.credits.h, HIT_FIN_CREDITS, 0);
        } else if (b.n == 1) add_hit(b.back.x, b.back.y, b.back.w, b.back.h, HIT_FIN_BACK, 0);
    }
    portal_check();
    if (portal_at || portal_hold) portal_draw();
}

void dual_open(int slot, const SmzRun *run)
{
    hints_open(slot, run);
    hk_load();
    map_set_visible(hints_visible());
    last_game = 0; portal_at = 0; leave_at = 0;
    portal_from = C(0x14, 0x23, 0x1A);
    portal_hold = getenv("SMZ3_PORTAL") ? (float)atof(getenv("SMZ3_PORTAL")) : 0;   /* test hook */
    tab = getenv("SMZ3_TAB") ? atoi(getenv("SMZ3_TAB")) % TAB_COUNT : TAB_RUN;   /* test hook */
    map_view_init(&mv);
    mv.follow = 0;
    map_view_init(&runmv);
    dungeon_view_init(&dv);
    dungeon_view_init(&minidv);
    memset(scroll, 0, sizeof scroll);
    memset(content, 0, sizeof content);
    memset(region_open, 0, sizeof region_open);
    memset(&P, 0, sizeof P);
    map_fitted = map_dungeon = was_live = action = card_up = region_list = n_hits = n_fingers = pinching = 0;
    owned = active = 0;
    picker = -1;
    note[0] = 0;
}

void dual_close(void) { hints_close(); }

int dual_take(void) { int a = action; action = DUAL_NONE; return a; }

/* ---------- touch ---------- */

static void do_hit(const Hit *h)
{
    int id;
    const DungeonLive *live = F.live;
    switch (h->id) {
    case HIT_TAB: tab = h->arg; break;
    case HIT_HOTKEY: hk_cycle(h->arg / 2, h->arg % 2); break;
    case HIT_RESET: config_reset(); break;
    case HIT_LEAVE:
        if (leave_at && SDL_GetTicks() - leave_at < LEAVE_MS) { action = DUAL_MENU; leave_at = 0; }
        else leave_at = SDL_GetTicks();
        break;
    case HIT_ZOOM: mini_zoom *= h->arg > 0 ? 1.4f : 1 / 1.4f; mini_zoom = mini_zoom < 0.5f ? 0.5f : mini_zoom > 4 ? 4 : mini_zoom; break;
    case HIT_SLOT: id = sc_slot(h->arg); if (id < 0) picker = h->arg; else sc_act(id); break;
    case HIT_SHORTCUT: sc_act(h->arg); break;
    case HIT_PICK: sc_set_slot(picker, h->arg); break;
    case HIT_PICK_CLEAR: sc_set_slot(picker, -1); break;
    case HIT_PICK_CLOSE: picker = -1; break;
    case HIT_REGION: region_open[h->arg] ^= 1; break;
    case HIT_HINT: hints_ask(); break;
    case HIT_REVEAL: region_list = !region_list; break;
    case HIT_REVEAL_REGION:
        hints_reveal(region_locs + regions[h->arg].first, regions[h->arg].n);
        region_list = 0;
        snprintf(note, sizeof note, "Showing the checks left in %s.", regions[h->arg].name);
        break;
    case HIT_CENTER: map_center(); break;
    case HIT_FOLLOW: mv.follow = !mv.follow; break;
    case HIT_DUNGEONS: dungeon_toggle(&dv, live); break;
    case HIT_CHIP: dungeon_chip_tap(&dv, live, h->arg & 15, h->arg >> 4); break;
    case HIT_MINI_CHIP: dungeon_chip_tap(&minidv, live, h->arg & 15, h->arg >> 4); break;
    case HIT_CONFIG: config_tap(h->arg); break;
    case HIT_FIN_MENU: case HIT_FIN_BACK: action = DUAL_MENU; break;
    case HIT_FIN_CREDITS: action = DUAL_CREDITS; break;
    }
}

static void press(float x, float y)
{
    const Hit *h = hit_at(x, y);
    memset(&P, 0, sizeof P);
    P.on = 1; P.x0 = P.x = x; P.y0 = P.y = y; P.at = SDL_GetTicks();
    if (h) { P.has_hit = 1; P.hit = *h; }
    if (card_up || picker >= 0 || y < tab_h) return;
    if (tab == TAB_CHECKS || tab == TAB_HELP || tab == TAB_CONFIG || (tab == TAB_RUN && x < W * RUN_MAP_X)) P.drag = DRAG_SCROLL;
    else if (tab == TAB_MAP && !map_dungeon && !mv.follow) P.drag = DRAG_PAN;
}

static void motion(float x, float y)
{
    float slop = 8 * u;
    if (!P.moved && fabsf(x - P.x0) < slop && fabsf(y - P.y0) < slop) return;
    P.moved = 1; P.has_hit = 0;
    if (P.drag == DRAG_SCROLL) scroll[tab] += P.y - y;
    else if (P.drag == DRAG_PAN) map_view_move(&mv, P.x - x, P.y - y, 0);
    P.x = x; P.y = y;
}

static void release(float x, float y)
{
    int tap = !P.moved;
    Hit was = P.hit;
    int had = P.has_hit;
    P.on = 0;
    if (!tap) return;
    if (card_up && finish_screen_skip()) return;
    const Hit *h = hit_at(x, y);
    if (had && h && h->id == was.id && h->arg == was.arg) { do_hit(&was); return; }
    if (tab != TAB_MAP || map_dungeon || mv.follow || y < tab_h) return;
    Uint32 now = SDL_GetTicks();
    if (last_tap && now - last_tap < DOUBLE_TAP_MS && fabsf(x - last_tap_x) < 30 * u && fabsf(y - last_tap_y) < 30 * u) { map_view_fit(&mv, map_area); last_tap = 0; }
    else { last_tap = now; last_tap_x = x; last_tap_y = y; }
}

static float finger_gap(void) { return hypotf(finger[0].x - finger[1].x, finger[0].y - finger[1].y); }

/* Two fingers on the world map pinch it around them; the mouse events SDL makes of the first one stop counting. */
static void finger_event(const SDL_TouchFingerEvent *e)
{
    int i = 0;
    while (i < n_fingers && finger[i].id != e->fingerId) i++;
    if (e->type == SDL_FINGERDOWN) {
        if (i == n_fingers && n_fingers < 2) { finger[n_fingers++].id = e->fingerId; }
        else if (i == n_fingers) return;
    } else if (i == n_fingers) return;
    if (e->type == SDL_FINGERUP) {
        if (i == 0) finger[0] = finger[1];
        n_fingers--;
        pinch_dist = 0;
        if (!n_fingers) pinching = 0;
        return;
    }
    finger[i].x = e->x * W; finger[i].y = e->y * H;
    if (n_fingers < 2 || tab != TAB_MAP || map_dungeon || card_up) return;
    float gap = finger_gap();
    if (!pinching) { pinching = 1; pinch_dist = gap; P.on = 0; return; }
    if (gap > 8 && pinch_dist > 8) map_pinch((finger[0].x + finger[1].x) / 2, (finger[0].y + finger[1].y) / 2, gap / pinch_dist);
    pinch_dist = gap;
}

int dual_event(const SDL_Event *e, SDL_Window *win, SDL_Renderer *r)
{
    Uint32 id = SDL_GetWindowID(win);
    int bw, bh, sw, sh;
    if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
        if (e->tfinger.windowID && e->tfinger.windowID != id) return 0;
        finger_event(&e->tfinger);
        return 1;
    }
    if (e->type != SDL_MOUSEBUTTONDOWN && e->type != SDL_MOUSEBUTTONUP && e->type != SDL_MOUSEMOTION) return 0;
    int mine = (e->type == SDL_MOUSEMOTION ? e->motion.windowID : e->button.windowID) == id;
    if (!mine && !(e->type == SDL_MOUSEBUTTONUP && P.on)) return 0;
    SDL_GetRendererOutputSize(r, &bw, &bh);
    SDL_GetWindowSize(win, &sw, &sh);
    float x = (e->type == SDL_MOUSEMOTION ? e->motion.x : e->button.x) * (float)bw / sw, y = (e->type == SDL_MOUSEMOTION ? e->motion.y : e->button.y) * (float)bh / sh;
    if (e->type == SDL_MOUSEBUTTONDOWN) { if (!pinching) press(x, y); }
    else if (e->type == SDL_MOUSEMOTION) { if (P.on) motion(x, y); else return 0; }
    else if (P.on) {
        if (mine) release(x, y);
        else P.on = 0;
    }
    return 1;
}
