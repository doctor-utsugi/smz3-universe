/* The second screen of a dual-screen handheld while the run list is in front: the logo, the selected run and the ladders,
 * on the run list's 320-wide grid, as on the Thor. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_gfx.h"
#include "pc_idle.h"
#include "pc_store.h"
#include "smz3_run.h"

#define ROWS 12
#define DIFF_MAX 12
#define STARS 70
#define C(hex) ((SDL_Color){(hex) >> 16 & 255, (hex) >> 8 & 255, (hex) & 255, 255})
#define BORDER C(0x25324A)
#define TEXT C(0xE8ECF2)
#define TEXT_HI C(0xFFF4DC)
#define DIM C(0x8793A6)
#define ACCENT C(0xF2A33A)
#define GREEN C(0x4CC38A)
#define BLUE C(0x4FC3F7)

typedef struct { char name[24]; int64_t ms; } Entry;

static const char *const TABS[] = {"ORIGINAL", "MONTHLY", "WEEKLY", "CUSTOM"};
static float u, tab_x[4], tab_y, tab_w;
static int tab;
static Entry local[2][ROWS];   /* finished runs on this device: 0 default rules, 1 custom rules */
static int local_n[2];
static int64_t local_mark = -1;
static struct {
    int slot, def, finished, zelda, zelda_total, metroid, metroid_total, diffs;
    int64_t ms;
    char name[24], logic[48], music[32], diff[DIFF_MAX][64];
} card = {.slot = -1};

static void text(float x, float y, float size, SDL_Color c, int align, const char *s) { gfx_text(x * u, (y - size * 0.8f) * u, size * u, c, align, s); }
static float width(float size, const char *s) { return gfx_text_width(size * u, s) / u; }
static void box(float x, float y, float w, float h, SDL_Color c) { gfx_rect(x * u, y * u, w * u, h * u, c); }

/* s cut with dots to fit w grid units at size */
static const char *fit(const char *s, float w, float size)
{
    static char out[80];
    if (width(size, s) <= w) return s;
    snprintf(out, sizeof out, "%s", s);
    for (size_t n = strlen(out); n > 1; n--) {
        strcpy(out + n - 1, "...");
        if (width(size, out) <= w) break;
    }
    return out;
}

static void clock_text(int64_t ms, char *out, size_t cap)
{
    int64_t m = ms / 60000;
    snprintf(out, cap, "%d:%02d:%02d", (int)(m / 60), (int)(m % 60), (int)(ms / 1000 % 60));
}

/* the text of "key":"value" in json, "" without it */
static void json_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat), *e = p ? strchr(p += strlen(pat), '"') : NULL;
    snprintf(out, cap, "%.*s", e ? (int)(e - p) : 0, e ? p : "");
}

static void load_card(int slot, const SmzUi *ui)
{
    static SmzRun r;
    card.slot = slot;
    card.ms = ui->slots[slot].play_ms;
    card.diffs = 0;
    if (store_run_load(slot + 1, &r)) r.settings[0] = 0;
    snprintf(card.name, sizeof card.name, "%s", ui->slots[slot].name);
    card.def = !strstr(r.settings, "\"defaultRules\":false");
    card.finished = ui->slots[slot].game == 3;
    card.zelda = ui->slots[slot].zelda_cleared; card.zelda_total = ui->slots[slot].zelda_total;
    card.metroid = ui->slots[slot].metroid_cleared; card.metroid_total = ui->slots[slot].metroid_total;
    char keys[24], music[32];
    json_str(r.settings, "gameMode", card.logic, sizeof card.logic);
    json_str(r.settings, "keysanity", keys, sizeof keys);
    if (keys[0] && strcmp(keys, "None")) strncat(card.logic, " - Keysanity", sizeof card.logic - strlen(card.logic) - 1);
    json_str(r.settings, "musicPackage", music, sizeof music);
    snprintf(card.music, sizeof card.music, "%s", !music[0] ? "Original" : !strcmp(music, "*") ? "Random" : music);
    const char *p = strstr(r.settings, "\"diff\":["), *end = p ? strchr(p, ']') : NULL;
    for (p = p ? p + 8 : NULL; p && p < end && card.diffs < DIFF_MAX; ) {
        const char *a = strchr(p, '"'), *b = a ? strchr(a + 1, '"') : NULL;
        if (!b || b > end) break;
        snprintf(card.diff[card.diffs++], sizeof card.diff[0], "%.*s", (int)(b - a - 1), a + 1);
        p = b + 1;
    }
}

static void add_local(int t, const char *name, int64_t ms)
{
    int i = local_n[t] < ROWS ? local_n[t]++ : ROWS - 1;
    if (i == ROWS - 1 && local[t][i].ms && local[t][i].ms <= ms) return;
    for (; i > 0 && local[t][i - 1].ms > ms; i--) local[t][i] = local[t][i - 1];
    snprintf(local[t][i].name, sizeof local[t][i].name, "%s", name);
    local[t][i].ms = ms;
}

static void load_local(const SmzUi *ui)
{
    int64_t mark = 0;
    for (int i = 0; i < UI_SLOTS; i++) if (ui->slots[i].used) mark += ui->slots[i].play_ms + ui->slots[i].game + 1;
    if (mark == local_mark) return;
    local_mark = mark;
    local_n[0] = local_n[1] = 0;
    static SmzRun r;
    for (int i = 0; i < UI_SLOTS; i++)
        if (ui->slots[i].used && ui->slots[i].game == 3 && !store_run_load(i + 1, &r))
            add_local(strstr(r.settings, "\"defaultRules\":false") != NULL, r.name, r.play_ms);
}

static void panel(float x, float y, float w, float h)
{
    gfx_chamfer(x * u, y * u, w * u, h * u, 6 * u, BORDER);
    gfx_chamfer((x + 0.7f) * u, (y + 0.7f) * u, (w - 1.4f) * u, (h - 1.4f) * u, 5.3f * u, C(0x141B27));
}

static void backdrop(int bw, int bh)
{
    const float xy[8] = {0, 0, (float)bw, 0, (float)bw, (float)bh, 0, (float)bh};
    const SDL_Color top = C(0x0C1220), low = C(0x0A0E15), cols[4] = {top, top, low, low};
    gfx_quad(xy, cols);
    for (int i = 0; i <= 20; i++) gfx_rect(i * 16 * u, 0, 1, (float)bh, (SDL_Color){79, 195, 247, 10});
    static float stars[STARS][3];
    if (!stars[0][2]) { srand(7); for (int i = 0; i < STARS; i++) for (int j = 0; j < 3; j++) stars[i][j] = rand() / (float)RAND_MAX; }
    float phase = SDL_GetTicks() % 40000 / 40000.0f;
    for (int i = 0; i < STARS; i++) {
        float *s = stars[i], depth = 0.3f + s[2] * 0.7f;
        int laps = 1 + (int)fminf(depth * 3, 2), cycles = 2 + (int)(depth * 8);
        float y = fmodf(s[1] + phase * laps, 1), twinkle = 0.5f + 0.5f * sinf((phase * cycles + s[0] * 10) * 2 * (float)M_PI);
        float edge = fminf(1, fminf(y * 12, (1 - y) * 12)), size = fmaxf(1, (0.5f + 0.7f * depth) * u);
        gfx_rect(s[0] * bw, y * bh, size, size, (SDL_Color){200, 220, 255, (Uint8)((40 + 110 * twinkle * depth) * edge)});
    }
}

static void header(int online, const char *version)
{
    float logo = 22, name = width(20, "SMZ3"), x = 160 - (logo + 6 + name) / 2;
    gfx_logo(x * u, 6 * u, logo * u, 1);
    text(x + logo + 6, 24.5f, 20, ACCENT, 0, "SMZ3");
    text(160, 39, 6, DIM, 1, "A LINK TO THE PAST - SUPER METROID");
    const char *net = online ? "ONLINE" : "OFFLINE";
    SDL_Color c = online ? GREEN : DIM;
    text(304, 13, 5, c, 2, net);
    float dot = 304 - width(5, net) - 4;
    box(dot - 1.3f, 10, 2.6f, 2.6f, c);
    text(dot - 4.5f, 12.6f, 4, DIM, 2, version);
    box(12, 45, 296, 0.5f, BORDER);
    box(148, 44.5f, 24, 1, ACCENT);
}

static void bar(const char *label, int v, int total, float x, float y, float w, SDL_Color c)
{
    char n[16] = "-";
    if (total > 0) snprintf(n, sizeof n, "%d/%d", v, total);
    text(x, y, 5.5f, DIM, 0, label);
    text(x + w, y, 6, TEXT, 2, n);
    box(x, y + 3, w, 2, BORDER);
    if (total > 0) box(x, y + 3, w * v / total, 2, c);
}

static void run_card(float l, float t, float w, float h)
{
    panel(l, t, w, h);
    float x = l + 9;
    char line[40];
    snprintf(line, sizeof line, "SLOT %d", card.slot + 1);
    text(x, t + 13, 5.5f, DIM, 0, line);
    text(x, t + 27, 11, TEXT, 0, fit(card.name, w - 18, 11));
    const char *badge = card.def ? "DEFAULT RULES" : "CUSTOM RULES";
    SDL_Color tint = card.def ? GREEN : ACCENT, soft = tint;
    soft.a = 40;
    box(x, t + 33, width(5.5f, badge) + 8, 9, soft);
    text(x + 4, t + 39.6f, 5.5f, tint, 0, badge);
    text(x, t + 55, 5.5f, DIM, 0, card.finished ? "FINISHED IN" : "PLAY TIME");
    clock_text(card.ms, line, sizeof line);
    text(x, t + 68, 12, card.finished ? GREEN : TEXT, 0, line);
    bar("ZELDA", card.zelda, card.zelda_total, x, t + 82, w - 18, GREEN);
    bar("METROID", card.metroid, card.metroid_total, x, t + 100, w - 18, BLUE);
    float y = t + 124;
    int room = (int)((h - 141) / 10);
    if (room < 0) room = 0;
    int lines = card.diffs > room ? (room > 0 ? room - 1 : 0) : card.diffs;
    for (int i = 0; i < lines; i++, y += 10) {
        box(x, y - 3, 1.5f, 1.5f, ACCENT);
        text(x + 5, y, 6, TEXT, 0, fit(card.diff[i], w - 22, 6));
    }
    if (card.diffs > lines && room > 0) { snprintf(line, sizeof line, "+ %d more", card.diffs - lines); text(x + 5, y, 6, DIM, 0, line); }
    box(l + 6, t + h - 17, w - 12, 0.5f, BORDER);
    text(x, t + h - 7, 6, TEXT, 0, fit(card.logic, w / 2 - 12, 6));
    text(l + w - 9, t + h - 7, 6, DIM, 2, fit(card.music, w / 2 - 6, 6));
}

static void ladder(const SmzUi *ui, float l, float t, float w, float h)
{
    panel(l, t, w, h);
    float tw = (w - 12) / 4;
    tab_y = t; tab_w = tw;
    for (int i = 0; i < 4; i++) {
        float x = l + 6 + i * tw;
        tab_x[i] = x;
        text(x + tw / 2, t + 12, 5.5f, i == tab ? ACCENT : DIM, 1, fit(TABS[i], tw - 2, 5.5f));
        if (i == tab) box(x + 4, t + 15.5f, tw - 8, 1.5f, ACCENT);
    }
    box(l + 6, t + 17, w - 12, 0.5f, BORDER);
    Entry rows[UI_LADDER_ROWS];
    int n = 0;
    if (tab < 3) for (; n < ui->ladder[tab].n; n++) {
        snprintf(rows[n].name, sizeof rows[n].name, "%s", ui->ladder[tab].row[n].name);
        rows[n].ms = ui->ladder[tab].row[n].ms;
    }
    if (tab == 3 || (tab == 0 && !n)) { n = local_n[tab == 3]; memcpy(rows, local[tab == 3], sizeof local[0]); }
    if (!n) { text(l + w / 2, t + h / 2 + 6, 6.5f, DIM, 1, tab == 3 ? "No finished custom runs" : "No times yet"); return; }
    static const SDL_Color MEDALS[3] = {{0xF2, 0xC1, 0x4E, 255}, {0xC7, 0xD0, 0xDE, 255}, {0xC9, 0x8A, 0x55, 255}};
    float row_h = 14;
    int shown = (int)((h - 24) / row_h);
    for (int i = 0; i < n && i < shown; i++) {
        float y = t + 22 + i * row_h;
        char num[8], time[24];
        if (!i) box(l + 3, y, w - 6, row_h - 1, (SDL_Color){242, 163, 58, 22});
        snprintf(num, sizeof num, "%d", i + 1);
        text(l + 14, y + 9.5f, 7, i < 3 ? MEDALS[i] : DIM, 1, num);
        clock_text(rows[i].ms, time, sizeof time);
        text(l + 24, y + 9.5f, 6.5f, i ? TEXT : TEXT_HI, 0, fit(rows[i].name, w - 34 - width(6.5f, time), 6.5f));
        text(l + w - 8, y + 9.5f, 6.5f, i ? TEXT : ACCENT, 2, time);
    }
}

void idle_draw(const SmzUi *ui, int bw, int bh, const char *version)
{
    u = bw / 320.0f;
    float gh = bh / u, top = 52, body = gh - top - 8;
    load_local(ui);
    backdrop(bw, bh);
    header(ui->ladder[0].loaded == 1, version);
    int slot = (ui->screen == UI_SLOTS_SCREEN && !ui->daily_sel) || ui->screen == UI_SLOT_MENU ? ui->slot_cursor : -1;
    if (slot >= 0 && ui->slots[slot].used) {
        if (card.slot != slot || card.ms != ui->slots[slot].play_ms || strcmp(card.name, ui->slots[slot].name)) load_card(slot, ui);
        run_card(12, top, 140, body);
        ladder(ui, 158, top, 150, body);
    } else ladder(ui, 12, top, 296, body);
}

void idle_tap(float x, float y)
{
    if (y < tab_y * u || y > (tab_y + 18) * u) return;
    for (int i = 0; i < 4; i++) if (x >= tab_x[i] * u && x < (tab_x[i] + tab_w) * u) tab = i;
}
