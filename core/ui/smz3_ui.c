#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "smz3_ui.h"
#include "smz3_goal.h"
#include "smz3_run.h"
#include "smz3_sm.h"

#define ROW_H 20
#define LIST_Y 44
#define ROWS_SHOWN (7 + (UI_H - 240) / ROW_H)
#define GRID_COLS 5
#define GRID_ROWS (3 + (UI_H - 240) / GRID_CELL)
#define GRID_X 10
#define GRID_Y 28
#define GRID_CELL 60

static void put(SmzUi *u, int kind, int x, int y, int w, int h, int color, int align, const char *text)
{
    if (u->cmd_count >= UI_MAX_CMDS) return;
    UiCmd *c = &u->cmd[u->cmd_count++];
    c->kind = kind; c->x = x; c->y = y; c->w = w; c->h = h; c->color = color; c->align = align;
    snprintf(c->text, sizeof c->text, "%s", text ? text : "");
}
static void hit(SmzUi *u, int kind, int idx, int x, int y, int w, int h)
{
    if (u->hit_count >= UI_MAX_HITS) return;
    UiHit *t = &u->hit[u->hit_count++];
    t->x = x; t->y = y; t->w = w; t->h = h; t->kind = kind; t->idx = idx;
}
int smz_ui_w = 320;
void ui_set_width(int w) { smz_ui_w = w < 320 ? 320 : w > 432 ? 432 : w; }
int smz_ui_h = 240;
void ui_set_height(int h) { smz_ui_h = h < 240 ? 240 : h > 720 ? 720 : h; }

static void rect(SmzUi *u, int x, int y, int w, int h, int color) { put(u, 0, x, y, w, h, color, 0, NULL); }
static void text(SmzUi *u, int x, int y, int color, int align, const char *t) { put(u, 1, x, y, 0, 0, color, align, t); }
/* A country as its flag (12x9, its left edge at x) when the host has the pictures, else as its code. */
static void flag(SmzUi *u, int x, int y, int color, const char *cc)
{
    if (!cc[0]) return;
    if (!u->flags) { text(u, x, y, color, 0, cc); return; }
    char key[16];
    snprintf(key, sizeof key, "flag/%s", cc);
    put(u, 2, x, y, 12, 9, color, 0, key);
}

/* The button legend of a screen, "A Load  B Back": every host draws it as its bar with the buttons of the device in use. */
static void legend(SmzUi *u, const char *s) { text(u, 6, UI_H - 12, C_DIM, 0, s); }

/* The games the server takes: all three have a daily, ladders and races. */
static int online_game(const SmzUi *u) { return u->game_mode >= 0 && u->game_mode < SMZ_MODE_COUNT; }
static int sm_mode(const SmzUi *u) { return u->game_mode == SMZ_MODE_SM; }
static int z3_mode(const SmzUi *u) { return u->game_mode == SMZ_MODE_Z3; }
static int single_game(const SmzUi *u) { return u->game_mode != SMZ_MODE_SMZ3; }
#define DAILY_ON(u) ((u)->daily_on && online_game(u))
#define LADDER_ON(u) ((u)->ladder_on && online_game(u))

void ui_init(SmzUi *u)
{
    memset(u, 0, sizeof *u);
    u->world_cursor = u->world_top = -1;
    for (int i = 0; i < UI_OPT_COUNT; i++) u->value[i] = UI_OPTS[i].def;
    for (int g = 0; g < 4; g++) { strcpy(u->sprites[g][0], g == 2 ? "None" : "Original"); u->sprite_count[g] = 1; }
    strcpy(u->sprites[3][1], "Random"); u->sprite_count[3] = 2;
    u->slot_count = 4;
    u->menu_music = 1;
    u->game_state[SMZ_MODE_Z3] = u->game_state[SMZ_MODE_SM] = UI_GAME_SOON;   /* until their generators are in */
    u->screen = UI_SLOTS_SCREEN;
    u->rng = (uint32_t)time(NULL) * 2654435761u | 1;
    ui_saved_load(u, NULL);
}

void ui_set_slot_count(SmzUi *u, int n)
{
    u->launching = 0;
    u->slot_count = n < 1 ? 1 : n > UI_SLOTS ? UI_SLOTS : n;
    if (u->slot_cursor >= u->slot_count) u->slot_cursor = 0;
}

void ui_set_slot(SmzUi *u, int i, int used, const char *name, const char *summary)
{
    if (i < 0 || i >= UI_SLOTS) return;
    u->slots[i].used = used;
    u->slots[i].stats = 0;
    snprintf(u->slots[i].name, sizeof u->slots[i].name, "%s", name ? name : "");
    snprintf(u->slots[i].summary, sizeof u->slots[i].summary, "%s", summary ? summary : "");
}

void ui_set_slot_online(SmzUi *u, int i, int kind) { if (i >= 0 && i < UI_SLOTS) u->slots[i].online = kind; }

void ui_set_slot_stats(SmzUi *u, int i, int zc, int zt, int mc, int mt, int64_t ms, int game, int bosses, int crystals)
{
    if (i < 0 || i >= UI_SLOTS) return;
    UiSlot *t = &u->slots[i];
    t->stats = 1; t->zelda_cleared = zc; t->zelda_total = zt; t->metroid_cleared = mc; t->metroid_total = mt;
    t->play_ms = ms; t->game = game; t->boss_mask = bosses; t->crystal_mask = crystals;
    t->crystals_need = 7; t->bosses_need = 4;
    t->link_sprite[0] = t->samus_sprite[0] = 0;
}

static void json_str(const char *j, const char *key, char *out, size_t cap)
{
    const char *p = j ? strstr(j, key) : NULL;
    out[0] = 0;
    if (!p) return;
    p += strlen(key);
    size_t n = strcspn(p, "\"");
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

static int json_int(const char *j, const char *key, int def)
{
    const char *p = j ? strstr(j, key) : NULL;
    return p ? atoi(p + strlen(key)) : def;
}

void ui_set_slot_rules(SmzUi *u, int i, const char *j)
{
    if (i < 0 || i >= UI_SLOTS) return;
    UiSlot *t = &u->slots[i];
    int ganon = json_int(j, "\"ganonCrystals\":", 7), tower = json_int(j, "\"ganonsTowerCrystals\":", 7);
    if (j && strstr(j, "\"game\":\"z3\"")) {   /* alttpr's keys hold strings; "random" counts as seven */
        const char *g = strstr(j, "\"ganon_open\":\""), *w = strstr(j, "\"tower_open\":\"");
        ganon = g && g[14] >= '0' && g[14] <= '7' ? g[14] - '0' : 7;
        tower = w && w[14] >= '0' && w[14] <= '7' ? w[14] - '0' : 7;
    }
    t->crystals_need = ganon > tower ? ganon : tower;
    t->bosses_need = json_int(j, "\"tourianBosses\":", 4);
    json_str(j, "\"linkSprite\":\"", t->link_sprite, sizeof t->link_sprite);
    json_str(j, "\"samusSprite\":\"", t->samus_sprite, sizeof t->samus_sprite);
    if (t->crystals_need < 0 || t->crystals_need > 7) t->crystals_need = 7;
    if (t->bosses_need < 0 || t->bosses_need > 4) t->bosses_need = 4;
}

void ui_set_sprites(SmzUi *u, int game, const char *const *names, int n)
{
    if (game < 0 || game > 3) return;
    int lead = game == 3 ? 2 : 1;   /* music packages start with "Original" and "Random" */
    u->sprite_count[game] = lead;
    for (int i = 0; i < n && u->sprite_count[game] < UI_MAX_SPRITES; i++)
        snprintf(u->sprites[game][u->sprite_count[game]++], 32, "%s", names[i]);
    /* alphabetical (case-insensitive) after the leading entries */
    for (int i = lead + 1; i < u->sprite_count[game]; i++) {
        char t[32];
        memcpy(t, u->sprites[game][i], 32);
        int j = i - 1;
        while (j >= lead && strcasecmp(u->sprites[game][j], t) > 0) { memcpy(u->sprites[game][j + 1], u->sprites[game][j], 32); j--; }
        memcpy(u->sprites[game][j + 1], t, 32);
    }
}

#define LOGO_MS 2000
#define HOME_ROWS 5
static const char *const HOME_NAMES[HOME_ROWS] = {"Start / Load game", "Online draft", "Championship", "Options", "Quit"};
/* Quit is a row only where the host can close the app (the PC builds). */
static int home_rows(const SmzUi *u) { return u->options_mask & UI_OPT_QUIT ? 5 : 4; }
static const struct { int bit; const char *name, *desc; } OPTION_ROWS[] = {   /* bit 0: the Player row, -1: the Update row, -2: Credits, -3: Cloud sync */
    {-1, "Update", ""}, {UI_OPT_BUTTONS, "Buttons and keys", ""}, {UI_OPT_VIDEO, "Video", "resolution, screen"}, {UI_OPT_SOUND, "Menu music", ""},
    {UI_OPT_SEED, "Seed code", "on the RUN tab"},
    {0, "Player", "name, country"}, {-3, "Cloud sync", "RomM"}, {-2, "Credits", "authors, licences"}, {UI_OPT_QUIT, "Quit", ""}};
/* The Credits screen: whose work the app is built on, and under which licence (THIRD_PARTY.md has the notices).
   Lines starting with '#' are headings; keep each line within 40 characters for the 320-pixel screens. */
static const char *const CREDITS_LINES[] = {
    "#SMZ3 Universe made by Doctor Utsugi",
    "",
    "#Randomizers",
    "SMZ3 and Super Metroid randomizers:",
    "  Thomas Backmark (tewtal) - MIT",
    "Super Metroid base patch: tewtal - MIT",
    "A Link to the Past randomizer (alttpr):",
    "  sporchia - MIT, with thanks to",
    "  Dessyreqt, Christos, Smallhacker",
    "  and KatDevsGames",
    "z3randomizer base patch: KatDevsGames,",
    "  Equilateral IT - MIT",
    "Enemizer boss shuffle: Zarby89 - MIT",
    "Map Rando: maddo, kyleb - MIT",
    "Map Rando logic: sm-json-data, by the",
    "  ALttP and SM randomizer communities",
    "  - CC BY 4.0",
    "SMZ3 Cas' Randomizer patches, map",
    "  icons and texts: TheTrackerCouncil,",
    "  Laura Verdoes - MIT",
    "#Sprites",
    "Link and Samus sprites: their authors,",
    "  collected in SMZ3 Cas' Sprites",
    "Item icons: SMZ3 Cas' Sprites",
    "Tracker configs: SMZ3 Cas' Configs",
    "  (TheTrackerCouncil)",
    "Hash icons: z3randomizer",
    "#Emulation",
    "Snes9x: Gary Henderson, Jerremy Koot,",
    "  BearOso and the Snes9x team",
    "  - Snes9x licence, non-commercial",
    "Snes9x libretro core: libretro",
    "snes9x_3ds: bubble2k16, matbo87",
    "  - Snes9x licence, non-commercial",
    "rcheevos: RetroAchievements.org - MIT",
    "Lemuroid, LibretroDroid: Swordfish90",
    "  - GPL-3.0",
    "#Tools and libraries",
    "asar: Alcaro, based on xkas by byuu",
    "  - LGPL-3.0",
    "SDL2: Sam Lantinga - zlib",
    "stb_image: Sean Barrett - public domain",
    "dr_mp3: David Reid - public domain",
    "libctru, citro3d: devkitPro - zlib",
    "libpng, zlib",
    "#Music",
    "Menu music: Doctor Utsugi",
    "#The games",
    "Super Metroid and The Legend of Zelda:",
    "  A Link to the Past (c) Nintendo.",
    "  This app is not made, endorsed or",
    "  supported by Nintendo. No ROM is",
    "  included: it uses your own copies.",
};
#define CREDITS_COUNT (int)(sizeof CREDITS_LINES / sizeof CREDITS_LINES[0])
#define CREDITS_PITCH 16
#define CREDITS_SHOWN ((UI_H - 34 - 22) / CREDITS_PITCH)
#define OPTION_KINDS (int)(sizeof OPTION_ROWS / sizeof OPTION_ROWS[0])

void ui_home_enable(SmzUi *u, int mask) { u->home_on = 1; u->options_mask = mask; }
void ui_menu_music(SmzUi *u, int on) { u->menu_music = on; }
void ui_seed_code(SmzUi *u, int on) { u->seed_code = on; }
void ui_home_open(SmzUi *u) { u->screen = UI_LOGO; u->logo_ms = LOGO_MS; u->home_cursor = 0; }
static int roms_missing(const SmzUi *u) { return u->roms_on && (u->game_state[SMZ_MODE_SM] == UI_GAME_NO_ROM || u->game_state[SMZ_MODE_Z3] == UI_GAME_NO_ROM); }
void ui_game_state(SmzUi *u, int mode, int state)
{
    int was_missing = roms_missing(u);
    if (mode >= 0 && mode < SMZ_MODE_COUNT) u->game_state[mode] = state;
    if (u->screen == UI_ROMS && u->roms_auto && !roms_missing(u)) u->screen = UI_GAMES;   /* the host's look ended after the logo */
    else if (u->screen == UI_ROMS && was_missing && !roms_missing(u)) u->roms_cursor = 1;   /* the last one found: Play takes the focus */
}
void ui_roms_screen(SmzUi *u, int on, const char *hint) { u->roms_on = on; snprintf(u->roms_hint, sizeof u->roms_hint, "%s", hint ? hint : ""); }
void ui_roms_note(SmzUi *u, const char *note) { snprintf(u->notice, sizeof u->notice, "%s", note ? note : ""); }

static void roms_input(SmzUi *u, UiButton b)
{
    u->roms_auto = 0;
    if (b == BTN_LEFT || b == BTN_RIGHT) u->roms_cursor ^= 1;
    else if (b == BTN_B) u->screen = UI_GAMES;
    else if (b == BTN_A && u->roms_cursor == 0) u->action = ACT_ROMS;
    else if (b == BTN_A) u->screen = UI_GAMES;
}
void ui_tick(SmzUi *u, int ms)
{
    if (u->screen == UI_LOGO && (u->logo_ms -= ms) <= 0) { u->screen = roms_missing(u) ? UI_ROMS : UI_GAMES; u->roms_auto = 1; u->roms_cursor = 0; }
    if (u->screen == UI_GAMES) {   /* the ring turns to the front card the short way round, easing out to a stop in TURN_MS */
        enum { TURN_MS = 320 };
        int want = u->game_cursor * 1000;
        if ((u->game_from + u->game_delta - want) % 3000) {   /* a new front card: the turn starts again from where the ring is */
            u->game_from = u->game_spin;
            u->game_delta = ((want - u->game_spin) % 3000 + 4500) % 3000 - 1500;
            u->game_ms = 0;
        }
        u->game_ms = u->game_ms + ms < TURN_MS ? u->game_ms + ms : TURN_MS;
        float t = 1.0f - (float)u->game_ms / TURN_MS;
        u->game_spin = ((u->game_from + (int)lroundf(u->game_delta * (1.0f - t * t * t))) % 3000 + 3000) % 3000;
    }
    if (u->screen == UI_RACE_LIST && (u->race.find_ms += ms) >= 5000 && u->action == ACT_NONE) { u->race.find_ms = 0; u->action = ACT_RACE_FIND; }   /* the list stays fresh */
}

/* The Options rows this host has, as indices into OPTION_ROWS. */
static int option_rows(const SmzUi *u, int *kind)
{
    int n = 0;
    for (int i = 0; i < OPTION_KINDS; i++) {
        int bit = OPTION_ROWS[i].bit;
        if (bit == -2 || (bit == -3 ? u->cloud.on : bit == -1 ? u->update[0] : bit ? u->options_mask & bit : u->player_on)) kind[n++] = i;
    }
    return n;
}

static int paused_row(const SmzUi *u);

static const char *const GAME_NAMES[SMZ_MODE_COUNT] = {"SMZ3", "A Link to the Past", "Super Metroid"};
static const char *const GAME_LINES[SMZ_MODE_COUNT] = {"Zelda + Metroid", "Randomizer", "Randomizer"};
static const char *const GAME_PICS[SMZ_MODE_COUNT] = {"logo", "link/Original", "samus/Original"};
static const char *const GAME_SHORT[SMZ_MODE_COUNT] = {"SMZ3", "ALttP", "Super Metroid"};

/* Entering another game: its own monthly and weekly seeds, and none of the last game's daily, tables or world goals (the
 * host asks the server for this game's: ui_set_monthly_game, ui_daily_set, ui_ladder_load_json, ui_world_load_json). */
static void slot_view(SmzUi *u);

static void game_switch(SmzUi *u, int mode)
{
    u->game_mode = mode;
    u->slot_cursor = u->last_slot[mode]; u->daily_sel = 0; slot_view(u);   /* the slot opened last in this game */
    const UiGamePeriods *g = &u->periods[mode];
    snprintf(u->monthly_label, sizeof u->monthly_label, "%s", g->monthly_label);
    u->monthly_seed = g->monthly_seed; u->monthly_days = g->monthly_days;
    snprintf(u->weekly_label, sizeof u->weekly_label, "%s", g->weekly_label);
    u->weekly_seed = g->weekly_seed; u->weekly_days = g->weekly_days;
    memset(&u->daily, 0, sizeof u->daily);
    u->daily_has_run = 0;
    for (int t = 0; t < 3; t++) { u->ladder[t].loaded = 0; u->ladder[t].n = 0; }
    u->ladder_preset = 0;
    memset(&u->world, 0, sizeof u->world); memset(&u->world_prev, 0, sizeof u->world_prev);   /* each game has its own world goals */
    u->world_month = 0; u->world_cursor = u->world_top = -1;
}

void ui_game_enter(SmzUi *u, int mode)
{
    if (mode < 0 || mode >= SMZ_MODE_COUNT) return;
    u->game_cursor = u->picked = mode;
    if (u->game_mode != mode) game_switch(u, mode);
}

void ui_memory_load(SmzUi *u, const char *text)
{
    int g = 0, s[3] = {0, 0, 0};
    if (!text || sscanf(text, "%d %d %d %d", &g, &s[0], &s[1], &s[2]) < 1 || g < 0 || g >= SMZ_MODE_COUNT) return;
    u->picked = u->game_cursor = g;
    u->game_spin = u->game_from = g * 1000; u->game_delta = 0; u->game_ms = 1000;   /* already facing it, no turn */
    for (int i = 0; i < 3; i++) u->last_slot[i] = s[i] >= 0 && s[i] < UI_SLOTS ? s[i] : 0;
    if (u->game_mode == SMZ_MODE_SMZ3) { u->slot_cursor = u->last_slot[0]; slot_view(u); }
}

int ui_memory_text(const SmzUi *u, char *out, size_t cap)
{
    return snprintf(out, cap, "%d %d %d %d", u->picked, u->last_slot[0], u->last_slot[1], u->last_slot[2]);
}

static void games_input(SmzUi *u, UiButton b)
{
    if (b == BTN_LEFT || b == BTN_RIGHT || b == BTN_L || b == BTN_R) u->game_cursor = (u->game_cursor + (b == BTN_LEFT || b == BTN_L ? SMZ_MODE_COUNT - 1 : 1)) % SMZ_MODE_COUNT;
    else if (b != BTN_A) return;
    else if (u->game_state[u->game_cursor] == UI_GAME_SOON) snprintf(u->notice, sizeof u->notice, "%s is coming soon", GAME_NAMES[u->game_cursor]);
    else if (u->game_state[u->game_cursor] == UI_GAME_NO_ROM && u->roms_on) { u->screen = UI_ROMS; u->roms_cursor = 0; u->roms_auto = 0; }
    else if (u->game_state[u->game_cursor] == UI_GAME_NO_ROM) snprintf(u->notice, sizeof u->notice, "Add the %s ROM first", u->game_cursor == SMZ_MODE_SM ? "Super Metroid" : "Zelda");
    else {
        if (u->game_mode != u->game_cursor) game_switch(u, u->game_cursor);
        u->picked = u->game_cursor;
        u->notice[0] = 0; u->screen = UI_HOME; u->home_cursor = 0;
        if (!online_game(u)) u->daily_sel = 0;
    }
}

static void home_input(SmzUi *u, UiButton b)
{
    if (b == BTN_B) { u->screen = UI_GAMES; u->notice[0] = 0; return; }
    int rows = home_rows(u);
    if (u->home_cursor >= rows) u->home_cursor = 0;
    if (b == BTN_UP || b == BTN_DOWN) u->home_cursor = (u->home_cursor + (b == BTN_UP ? rows - 1 : 1)) % rows;
    else if (b != BTN_A) return;
    else if (u->home_cursor == 4) { u->action = ACT_OPTIONS; u->action_slot = UI_OPT_QUIT; }
    else if (u->home_cursor == 0) u->screen = UI_SLOTS_SCREEN;
    else if (u->home_cursor == 3) { u->screen = UI_OPTIONS; u->options_cursor = 0; }
    else if (!online_game(u)) snprintf(u->notice, sizeof u->notice, "Online races come later for this game");
    else if (u->race.on) {   /* one kind of race, up to 8: the first screen is there only to pick a paused race back up */
        u->race.format = u->home_cursor == 1 ? RACE_DRAFT : 0; u->race.size = 8; u->race.cursor = 0;
        u->screen = paused_row(u) ? UI_RACE_SIZE : UI_RACE_WAY;
        if (!u->player[0]) { u->name_for = 6; u->name[0] = 0; u->name_len = 0; u->key_cursor = 0; u->screen = UI_NAME; }   /* the others see this name */
    }
    else snprintf(u->notice, sizeof u->notice, "Online races need a connection");
}

static void credits_input(SmzUi *u, UiButton b)
{
    int last = CREDITS_COUNT > CREDITS_SHOWN ? CREDITS_COUNT - CREDITS_SHOWN : 0, *s = &u->credits_scroll;
    if (b == BTN_B || b == BTN_A) u->screen = UI_OPTIONS;
    else if (b == BTN_DOWN || b == BTN_UP) *s += b == BTN_DOWN ? 1 : -1;
    else if (b == BTN_RIGHT || b == BTN_R || b == BTN_LEFT || b == BTN_L) *s += (b == BTN_RIGHT || b == BTN_R ? 1 : -1) * CREDITS_SHOWN;
    if (*s > last) *s = last;
    if (*s < 0) *s = 0;
}

/* The Cloud sync rows for the sign-in state: CLOUD_* each. */
static int cloud_rows(const SmzUi *u, int *row)
{
    int n = 0;
    if (u->cloud.state == UI_CLOUD_OUT) { row[n++] = CLOUD_ADDRESS; row[n++] = CLOUD_SIGN_IN; }
    else if (u->cloud.state == UI_CLOUD_WAITING) row[n++] = CLOUD_CANCEL;
    else { row[n++] = CLOUD_SYNC; row[n++] = CLOUD_CODE; row[n++] = CLOUD_LINK; row[n++] = CLOUD_SIGN_OUT; }
    return n;
}

static void cloud_input(SmzUi *u, UiButton b)
{
    int row[6], n = cloud_rows(u, row), *c = &u->cloud.cursor;
    if (b == BTN_B) { u->screen = UI_OPTIONS; return; }
    if (b == BTN_UP || b == BTN_DOWN) { *c = (*c + (b == BTN_UP ? n - 1 : 1)) % n; return; }
    if (b != BTN_A || *c >= n) return;
    if (row[*c] == CLOUD_LINK) { u->name_for = 4; u->name[0] = 0; u->name_len = 0; u->key_cursor = 0; u->screen = UI_NAME; return; }
    if (row[*c] == CLOUD_ADDRESS || (row[*c] == CLOUD_SIGN_IN && !u->cloud.server[0])) {   /* each player's own RomM: no default */
        u->name_for = 5;
        snprintf(u->name, sizeof u->name, "%s", u->cloud.server);
        for (char *p = u->name; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        u->name_len = (int)strlen(u->name);
        u->key_cursor = 0;
        u->screen = UI_NAME;
        return;
    }
    u->action = ACT_CLOUD;
    u->action_slot = row[*c];
    u->cloud.msg[0] = 0;
}

static void cloud_conflict_input(SmzUi *u, UiButton b)
{
    int *c = &u->cloud.cursor;
    if (b == BTN_B) { u->screen = UI_SLOTS_SCREEN; return; }
    if (b == BTN_UP || b == BTN_DOWN) *c = !*c;
    else if (b == BTN_A) { u->action = ACT_CLOUD; u->action_slot = *c ? CLOUD_KEEP_CLOUD : CLOUD_KEEP_HERE; u->screen = UI_SLOTS_SCREEN; }
}

void ui_cloud_enable(SmzUi *u, int on) { u->cloud.on = on; }

void ui_cloud_state(SmzUi *u, int state, const char *user, const char *server, const char *code, const char *url)
{
    u->cloud.state = state;
    snprintf(u->cloud.user, sizeof u->cloud.user, "%s", user ? user : "");
    snprintf(u->cloud.server, sizeof u->cloud.server, "%s", server ? server : "");
    snprintf(u->cloud.code, sizeof u->cloud.code, "%s", code ? code : "");
    snprintf(u->cloud.url, sizeof u->cloud.url, "%s", url ? url : "");
    if (u->cloud.cursor >= 4 || state != UI_CLOUD_IN) u->cloud.cursor = 0;
    if (state == UI_CLOUD_WAITING) u->cloud.msg[0] = 0;   /* the code replaces "Asking RomM for a code..." */
}

void ui_cloud_msg(SmzUi *u, const char *msg) { snprintf(u->cloud.msg, sizeof u->cloud.msg, "%s", msg ? msg : ""); }

void ui_cloud_conflict(SmzUi *u, int slot, const char *device, const char *when)
{
    u->cloud.slot = slot;
    u->cloud.cursor = 0;
    snprintf(u->cloud.dev, sizeof u->cloud.dev, "%s", device && *device ? device : "another device");
    snprintf(u->cloud.when, sizeof u->cloud.when, "%s", when ? when : "");
    u->screen = UI_CLOUD_CONFLICT;
}

static void options_input(SmzUi *u, UiButton b)
{
    int kind[OPTION_KINDS], n = option_rows(u, kind), *c = &u->options_cursor;
    if (b == BTN_B) { u->screen = UI_HOME; return; }
    if (!n) return;
    if (b == BTN_UP || b == BTN_DOWN) *c = (*c + (b == BTN_UP ? n - 1 : 1)) % n;
    else if (b == BTN_A && *c < n) {
        int bit = OPTION_ROWS[kind[*c]].bit;
        if (bit == -2) { u->credits_scroll = 0; u->screen = UI_CREDITS; return; }
        if (bit == -3) { u->cloud.cursor = 0; u->cloud.msg[0] = 0; u->screen = UI_CLOUD; return; }
        if (bit < 0) {
            if (u->update_install) u->action = ACT_UPDATE;
            else snprintf(u->notice, sizeof u->notice, "smz3-universe.duckdns.org/download");
            return;
        }
        if (bit == UI_OPT_SOUND) u->menu_music = !u->menu_music;
        if (bit == UI_OPT_SEED) u->seed_code = !u->seed_code;
        if (bit) { u->action = ACT_OPTIONS; u->action_slot = bit; return; }
        u->player_opts = 1;
        u->name_for = 1;
        snprintf(u->name, sizeof u->name, "%s", u->player);
        u->name_len = (int)strlen(u->name);
        u->key_cursor = 0;
        u->screen = UI_NAME;
    }
}

static int sprite_opt(int game);
/* "v0.9.0" -> 9000 (major, minor, patch); -1 when it is no release tag ("dev"). */
static long version_rank(const char *v)
{
    int a = 0, b = 0, c = 0;
    if (!v || (*v != 'v' && *v != 'V') || sscanf(v + 1, "%d.%d.%d", &a, &b, &c) < 2) return -1;
    return a * 1000000L + b * 1000L + c;
}

int ui_update_load_json(SmzUi *u, const char *json, const char *mine, int can_install)
{
    char tag[16];
    json_str(json, "\"tag\":\"", tag, sizeof tag);
    long have = version_rank(mine), got = version_rank(tag);
    if (have < 0 || got <= have) { u->update[0] = 0; return 0; }
    snprintf(u->update, sizeof u->update, "%s", tag);
    u->update_install = can_install;
    return 1;
}

void ui_race_enable(SmzUi *u, int on) { u->race.on = on; }
const char *ui_race_format_key(const SmzUi *u) { return u->race.format == RACE_DRAFT ? "draft" : u->race.format == 1 ? "sprint" : u->race.format == 2 ? "half" : "full"; }

/* The paused race row: Championship only, since a Draft never pauses. */
static int paused_row(const SmzUi *u) { return u->race.paused_code[0] && u->race.format != RACE_DRAFT; }

static void race_reset(SmzUi *u) { memset(&u->race.room, 0, sizeof u->race.room); u->race.getting_ready = u->race.started = 0; }

void ui_race_end(SmzUi *u) { race_reset(u); u->screen = UI_HOME; }

void ui_race_set_paused(SmzUi *u, const char *code, int64_t resume_at)
{
    snprintf(u->race.paused_code, sizeof u->race.paused_code, "%s", code ? code : "");
    u->race.paused_resume = resume_at;
}

void ui_race_fail(SmzUi *u, const char *msg)
{
    int kicked = !strcmp(msg, "not in this race"), gone = kicked || !strcmp(msg, "no such race");
    int in_room = u->screen == UI_RACE_ROOM || u->screen == UI_RACE_READY || (u->screen == UI_SPRITES && u->back == UI_RACE_READY);
    if (gone && in_room) {   /* removed by the creator, or the room expired: out of it, and the host stops asking */
        u->action = ACT_RACE_LEAVE;
        race_reset(u);
        u->screen = UI_RACE_WAY; u->race.cursor = 0;
        snprintf(u->notice, sizeof u->notice, "%s", kicked ? "You were removed from the room" : "The room closed");
        return;
    }
    snprintf(u->notice, sizeof u->notice, "%.31s", msg);
    u->race.getting_ready = 0;
    if (u->screen == UI_BUSY || u->screen == UI_RACE_PUBLIC || u->screen == UI_RACE_LIST) { u->screen = UI_RACE_WAY; u->race.cursor = 0; }
}

void ui_race_rooms(SmzUi *u, const char *json)
{
    u->race.room_count = smz_race_rooms(json, u->race.rooms, RACE_ROOMS);
    int n = 0;   /* Online draft lists Drafts, Championship the rest */
    for (int i = 0; i < u->race.room_count; i++)
        if (!strcmp(u->race.rooms[i].format, "draft") == (u->race.format == RACE_DRAFT)) u->race.rooms[n++] = u->race.rooms[i];
    u->race.room_count = n;
    u->race.rooms_loaded = json ? 1 : -1;
    if (u->race.cursor >= u->race.room_count) u->race.cursor = 0;
}

static int race_rows(const SmzUi *u);

/* A server answer: the room's state picks the screen while the player is on the race screens. */
void ui_race_update(SmzUi *u, const char *json, int64_t local_now)
{
    SmzRace *r = &u->race.room;
    int was = r->state, was_n = r->n;
    if (smz_race_read(r, json, local_now)) return;
    if (r->state != was) u->notice[0] = 0;   /* an error from the step before no longer applies */
    const SmzRacePlayer *me = smz_race_me(r);
    if (me && me->ready) u->race.getting_ready = 0;
    int on_race = u->screen == UI_BUSY || (u->screen >= UI_RACE_SIZE && u->screen <= UI_RACE_READY) || (u->screen == UI_SPRITES && u->back == UI_RACE_READY);
    if (!on_race) return;
    if (r->state == RACE_OPEN && u->screen != UI_RACE_ROOM) { u->screen = UI_RACE_ROOM; u->race.cursor = 0; }
    else if (r->state == RACE_CLOSED && u->screen != UI_RACE_READY && u->screen != UI_SPRITES) { u->screen = UI_RACE_READY; u->race.cursor = was == RACE_CLOSED ? u->race.cursor : 0; }
    else if (r->state == RACE_RUNNING && !u->race.started) { u->race.started = 1; u->action = ACT_RACE_START; }
    else if (r->state == RACE_CANCELLED) { race_reset(u); u->screen = UI_RACE_WAY; u->race.cursor = 0; snprintf(u->notice, sizeof u->notice, "The room closed"); }
    if (u->screen == UI_RACE_ROOM && r->n != was_n && u->race.cursor == was_n) u->race.cursor = r->n;   /* Start race stays picked, not the newcomer */
    int rows = race_rows(u);
    if (u->race.cursor >= rows) u->race.cursor = rows ? rows - 1 : 0;
}

static int race_rows(const SmzUi *u)
{
    switch (u->screen) {
    case UI_RACE_SIZE: return 1 + paused_row(u);
    case UI_RACE_FORMAT: case UI_RACE_LENGTH: case UI_RACE_PUBLIC: return 2;
    case UI_RACE_WAY: return 3;
    case UI_RACE_LIST: return u->race.room_count;
    case UI_RACE_ROOM: return u->race.room.n + (!strcmp(u->race.room.you, u->race.room.creator));   /* the creator also has Close */
    case UI_RACE_READY: return 3;
    default: return 0;
    }
}

static void race_input(SmzUi *u, UiButton b)
{
    int n = race_rows(u), *c = &u->race.cursor;
    SmzRace *r = &u->race.room;
    if ((b == BTN_UP || b == BTN_DOWN) && n) { *c = (*c + (b == BTN_UP ? n - 1 : 1)) % n; return; }
    if (b == BTN_B) {
        switch (u->screen) {
        case UI_RACE_SIZE: u->screen = UI_HOME; break;
        case UI_RACE_WAY: if (paused_row(u)) { u->screen = UI_RACE_SIZE; *c = paused_row(u); } else u->screen = UI_HOME; break;
        case UI_RACE_FORMAT: u->screen = UI_RACE_WAY; *c = 0; break;
        case UI_RACE_LENGTH: u->screen = UI_RACE_FORMAT; *c = 1; break;
        case UI_RACE_PUBLIC:
            if (u->race.format == RACE_DRAFT || sm_mode(u)) { u->screen = UI_RACE_WAY; *c = 0; break; }
            u->screen = u->race.format ? UI_RACE_LENGTH : UI_RACE_FORMAT; *c = u->race.format ? u->race.format - 1 : 0;
            break;
        case UI_RACE_LIST: u->screen = UI_RACE_WAY; *c = 2; break;
        case UI_RACE_ROOM: case UI_RACE_READY: u->action = ACT_RACE_LEAVE; race_reset(u); u->screen = UI_RACE_WAY; *c = 0; break;
        default: break;
        }
        return;
    }
    if (b != BTN_A) return;
    switch (u->screen) {
    case UI_RACE_SIZE: {
        int k = *c - paused_row(u);   /* -1: the paused race */
        if (k < 0) { u->action = ACT_RACE_RESUME; break; }
        u->race.size = 8; u->screen = UI_RACE_WAY; *c = 0;
        break;
    }
    case UI_RACE_WAY:
        if (*c == 0) { u->race.format = u->race.format == RACE_DRAFT ? RACE_DRAFT : 0; u->screen = u->race.format == RACE_DRAFT || sm_mode(u) ? UI_RACE_PUBLIC : UI_RACE_FORMAT; *c = 0; }   /* Super Metroid: Full only */
        else if (*c == 1) { u->name_for = 2; u->name[0] = 0; u->name_len = 0; u->key_cursor = 0; u->screen = UI_NAME; }
        else { u->race.rooms_loaded = 0; u->race.room_count = 0; u->screen = UI_RACE_LIST; *c = 0; u->race.find_ms = 0; u->action = ACT_RACE_FIND; }
        break;
    case UI_RACE_FORMAT:
        if (*c == 0) { u->race.format = 0; u->screen = UI_RACE_PUBLIC; }
        else u->screen = UI_RACE_LENGTH;
        *c = 0;
        break;
    case UI_RACE_LENGTH: u->race.format = 1 + *c; u->screen = UI_RACE_PUBLIC; *c = 0; break;
    case UI_RACE_PUBLIC: u->race.is_public = *c; race_reset(u); u->action = ACT_RACE_CREATE; u->screen = UI_BUSY; snprintf(u->status, sizeof u->status, "Making the room..."); break;
    case UI_RACE_LIST:
        if (*c < u->race.room_count) { snprintf(u->race.code, sizeof u->race.code, "%s", u->race.rooms[*c].code); race_reset(u); u->action = ACT_RACE_JOIN; u->screen = UI_BUSY; snprintf(u->status, sizeof u->status, "Joining..."); }
        break;
    case UI_RACE_ROOM:
        if (*c == r->n) { if (r->n < 2) snprintf(u->notice, sizeof u->notice, "Needs 2 players"); else u->action = ACT_RACE_CLOSE; }
        else if (!strcmp(r->you, r->creator) && strcmp(r->p[*c].pid, r->you)) { u->action = ACT_RACE_KICK; u->action_slot = *c; }
        break;
    case UI_RACE_READY: {
        const SmzRacePlayer *me = smz_race_me(r);
        if (me && me->ready) break;
        if (*c < 2) { u->sprite_game = *c; u->sprite_cursor = u->value[sprite_opt(*c)]; u->back = UI_RACE_READY; u->screen = UI_SPRITES; }
        else if (!u->race.getting_ready) { u->race.getting_ready = 1; u->action = ACT_RACE_READY; }
        break;
    }
    default: break;
    }
}

void ui_busy(SmzUi *u, const char *msg) { u->screen = UI_BUSY; snprintf(u->status, sizeof u->status, "%s", msg); }
const char *ui_run_label(const SmzUi *u) { return u->start_label; }
void ui_idle(SmzUi *u) { u->screen = UI_SLOTS_SCREEN; }

/* indices into UI_OPTS of the options shown at one level */
static int visible(const SmzUi *u, int level, int *idx)
{
    int n = 0;
    for (int k = 0; k < UI_OPT_COUNT; k++) {
        int i = k;
        if (level != LEVEL_ITEMS) {
            const char *key = k < 24 ? (sm_mode(u) ? UI_TABS_SM : z3_mode(u) ? UI_TABS_Z3 : UI_TABS)[level][k] : NULL;
            if (!key) break;
            for (i = 0; i < UI_OPT_COUNT && strcmp(UI_OPTS[i].key, key); i++) {}
            if (i == UI_OPT_COUNT) continue;
        } else if (UI_OPTS[i].level != LEVEL_ITEMS) continue;
        if (ui_opt_hidden(u, i)) continue;
        if (i == UIO_MUSIC && u->sprite_count[3] <= 2) continue;   /* no music package installed: nothing to pick (the 3DS has none) */
        if (i == UIO_PLANDO && u->sprite_count[2] < 2) continue;   /* no plando file: only None */
        if (i == UIO_FILTER && (u->no_opts & UI_NO_FILTER)) continue;
        idx[n++] = i;
    }
    return n;
}

/* A single game's Create screen has no saved customs (they carry SMZ3 options) and skips the tabs it has nothing in. */
static int save_row(const SmzUi *u) { return !single_game(u); }
static int tab_shown(const SmzUi *u, int level) { int idx[UI_MAX_OPTS]; return visible(u, level, idx) > 0; }
static void tab_step(SmzUi *u, int d)
{
    for (int l = u->level + d; l >= LEVEL_QUICK && l <= LEVEL_ITEMS; l += d) if (tab_shown(u, l)) { u->level = l; return; }
}

static void change(SmzUi *u, int opt, int dir, int big)
{
    const UiOptDef *d = &UI_OPTS[opt];
    int *v = &u->value[opt];
    switch (d->kind) {
    case OPT_BOOL: *v = !*v; break;
    case OPT_SEED: { long n = (long)*v + dir * (big ? 1000 : 1); if (n < 0) n = 0; if (n > d->max) n = d->max; *v = (int)n; break; }
    case OPT_SPRITE: break;
    default: {
        int max = opt == UIO_SPAZ_REQ ? u->value[UIO_SPAZ_POOL] : d->max;   /* no more required than placed */
        int n = *v + dir; if (n < d->min) n = max; if (n > max) n = d->min; *v = n;
    }
    }
    if (u->value[UIO_SPAZ_REQ] > u->value[UIO_SPAZ_POOL]) u->value[UIO_SPAZ_REQ] = u->value[UIO_SPAZ_POOL];
}

/* slot rows on screen: the Daily row takes the first place */
static int world_row(const SmzUi *u) { return LADDER_ON(u) && DAILY_ON(u); }   /* World goals beside the Daily challenge, in its row */
static int slots_shown(const SmzUi *u) { return UI_SLOTS_SHOWN + (UI_H - 240) / 47 - (DAILY_ON(u) != 0); }

#define SAVED_ROWS (5 + (UI_H - 240) / 38)

static void slot_view(SmzUi *u)
{
    int shown = slots_shown(u);
    if (u->slot_cursor < u->slot_scroll) u->slot_scroll = u->slot_cursor;
    if (u->slot_cursor >= u->slot_scroll + shown) u->slot_scroll = u->slot_cursor - shown + 1;
    if (u->slot_scroll > u->slot_count - shown) u->slot_scroll = u->slot_count - shown;
    if (u->slot_scroll < 0) u->slot_scroll = 0;
}

static int sprite_opt(int game)
{
    return game == 3 ? UIO_MUSIC : game == 2 ? UIO_PLANDO : game == 1 ? UIO_SAMUS : UIO_LINK;
}

static void move(int *cur, int *scroll, int n, int dir)
{
    *cur = (*cur + dir + n) % n;
    if (*cur < *scroll) *scroll = *cur;
    if (*cur >= *scroll + ROWS_SHOWN) *scroll = *cur - ROWS_SHOWN + 1;
    if (*cur == 0) *scroll = 0;
}


/* ---- run modes and saved customs */

typedef struct { const char *name, *desc, *spec; } UiPreset;
static const UiPreset CHILL = {"Cozy run", "Zelda keys shuffled, easy wall jumps", "Keysanity=2,WallJumpDifficulty=0,LightDarkRooms=1,SaveStates=1,Rewind=1,ShowReachable=1,ShowFixedKeys=1"};
static const UiPreset ORIGINAL = {"Original", "The default randomizer rules", ""};
static const UiPreset SM_CHILL = {"Cozy run", "Casual logic, save states, rewind", "SmLogic=0,SaveStates=1,Rewind=1"};
static const UiPreset SM_ORIGINAL = {"Original", "Tournament logic, major/minor split", ""};
/* A Link to the Past: Original is alttpr.com's default preset; Cozy the easiest the first-release options allow */
static const UiPreset Z3_CHILL = {"Cozy run", "Sword from the start, dark rooms lit", "Z3Swords=1,Z3Placement=0,LightDarkRooms=1,SaveStates=1,Rewind=1"};
static const UiPreset Z3_ORIGINAL = {"Original", "alttpr.com's default rules", ""};
static const UiPreset *chill(const SmzUi *u) { return sm_mode(u) ? &SM_CHILL : z3_mode(u) ? &Z3_CHILL : &CHILL; }
static const UiPreset *original(const SmzUi *u) { return sm_mode(u) ? &SM_ORIGINAL : z3_mode(u) ? &Z3_ORIGINAL : &ORIGINAL; }
static const UiPreset WEEKLY = {"Weekly run", "Short run: 2 crystals, 1 boss", "GanonCrystals=2,GtCrystals=2,TourianBosses=1"};
static const UiPreset SM_WEEKLY = {"Weekly run", "Original rules, Mother Brain", ""};   /* Super Metroid's only goal: a Split seed */
static const UiPreset Z3_WEEKLY = {"Weekly run", "Fast Ganon, 2 crystals", "Z3Goal=1,Z3TowerOpen=2,Z3GanonOpen=2"};
static const UiPreset *weekly(const SmzUi *u) { return sm_mode(u) ? &SM_WEEKLY : z3_mode(u) ? &Z3_WEEKLY : &WEEKLY; }
static void spec_values(int *value, const char *spec);
static const UiPreset BUILTIN[] = {
    {"Keysanity (both games)", "", "Keysanity=1"},
    {"Dungeons + Metroid", "", "ItemPlacement=1"},
    {"Opposite game", "", "ItemPlacement=4"},
    {"All Dungeons", "", "GameMode=1"},
    {"Spazer Hunt", "", "GameMode=2"},
    {"Quick finish", "", "GanonCrystals=4,GtCrystals=4,TourianBosses=2"},
    {"Open doors", "", "PyramidHole=1,TourianBossDoor=1"},
    {"Casual", "", "WallJumpDifficulty=0,LightDarkRooms=1,SaveStates=1"},
    {"Hardcore", "", "Keysanity=1,HelpMode=2,WallJumpDifficulty=3"},
};

/* SahasrahBot's presets/alttpr casualboots, standard and adkeys (tournament and spoilers aside, which every online run sets), and
 * Map Rando's Community Race Season 5 and Default full-settings presets as the hosts name them. SMZ3 races Original only. */
static const UiLadderPreset Z3_PRESETS[] = {
    {"casual_boots", "Casual Boots", "Boots", "Standard, assured sword, Pegasus Boots", "Z3WorldState=0,Z3Swords=1,Z3Hints=0,Z3StartBoots=1"},
    {"standard", "Standard", "Standard", "Standard start, randomized swords", "Z3WorldState=0,Z3Hints=0"},
    {"ad_keys", "AD Keys", "AD Keys", "All Dungeons, full keysanity", "Z3Goal=2,Z3DungeonItems=3,Z3Hints=0"},
};
static const UiLadderPreset SM_PRESETS[] = {
    {"mr_s5", "Map Rando Season 5", "Season 5", "The community race league's settings", "SmGenerator=1,SmMrPreset=1"},
    {"mr_beginner", "Map Rando Beginner", "Beginner", "Map Rando's default settings", "SmGenerator=1,SmMrPreset=0"},
};

int ui_ladder_presets(int mode, const UiLadderPreset **out)
{
    *out = mode == SMZ_MODE_Z3 ? Z3_PRESETS : SM_PRESETS;
    return mode == SMZ_MODE_Z3 ? (int)(sizeof Z3_PRESETS / sizeof *Z3_PRESETS) : mode == SMZ_MODE_SM ? (int)(sizeof SM_PRESETS / sizeof *SM_PRESETS) : 0;
}

static int opt_by_key(const char *key, size_t len)
{
    for (int i = 0; i < UI_OPT_COUNT; i++) if (!strncmp(UI_OPTS[i].key, key, len) && !UI_OPTS[i].key[len]) return i;
    return -1;
}

/* A Map Rando seed takes no sprite: its ROM is upstream's own */
static int maprando_run(const SmzUi *u) { int i = opt_by_key("SmGenerator", 11); return sm_mode(u) && i >= 0 && u->value[i]; }

/* the choices a saved custom does not carry: they depend on the files of this device */
static int spec_skips(int i) { return i == 0 || i == UIO_LINK || i == UIO_SAMUS || i == UIO_PLANDO || i == UIO_MUSIC; }

void ui_expected_values(const SmzUi *u, int *want)
{
    for (int i = 0; i < UI_OPT_COUNT; i++) want[i] = UI_OPTS[i].def;
    if (u->run_mode == 5) spec_values(want, weekly(u)->spec);   /* the weekly run's own rules */
}

static void reset_values(SmzUi *u) { for (int i = 0; i < UI_OPT_COUNT; i++) u->value[i] = UI_OPTS[i].def; }

int ui_run_preset(const SmzUi *u)
{
    int want[UI_MAX_OPTS];
    ui_expected_values(u, want);
    if (ui_rules_are(u, want)) return 0;
    if (u->run_mode >= 4) return -1;   /* the monthly and weekly have their own tables only */
    const UiLadderPreset *p;
    for (int k = 0, n = ui_ladder_presets(u->game_mode, &p); k < n; k++) {
        for (int i = 0; i < UI_OPT_COUNT; i++) want[i] = UI_OPTS[i].def;
        spec_values(want, p[k].spec);
        if (ui_rules_are(u, want)) return k + 1;
    }
    return -1;
}

const char *ui_run_preset_key(const SmzUi *u)
{
    const UiLadderPreset *p;
    int k = ui_run_preset(u);
    return k > 0 && k <= ui_ladder_presets(u->game_mode, &p) ? p[k - 1].key : "";
}

const char *ui_ladder_preset_key(const SmzUi *u)
{
    const UiLadderPreset *p;
    int k = u->ladder_preset;
    return k > 0 && k <= ui_ladder_presets(u->game_mode, &p) ? p[k - 1].key : "";
}

static void apply_spec(SmzUi *u, const char *spec) { spec_values(u->value, spec); }

static void spec_values(int *value, const char *spec)
{
    while (*spec) {
        const char *eq = strchr(spec, '='), *end = strchr(spec, ',');
        if (!eq) break;
        if (!end) end = spec + strlen(spec);
        if (eq < end) {
            int i = opt_by_key(spec, (size_t)(eq - spec));
            if (i >= 0 && !spec_skips(i)) {
                int v = atoi(eq + 1);
                value[i] = v < UI_OPTS[i].min ? UI_OPTS[i].min : v > UI_OPTS[i].max ? UI_OPTS[i].max : v;
            }
        }
        spec = *end ? end + 1 : end;
    }
    if (value[UIO_SPAZ_REQ] > value[UIO_SPAZ_POOL]) value[UIO_SPAZ_REQ] = value[UIO_SPAZ_POOL];
}

static void make_spec(const SmzUi *u, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < UI_OPT_COUNT; i++) {
        if (spec_skips(i) || ui_opt_hidden(u, i) || u->value[i] == UI_OPTS[i].def) continue;
        int w = snprintf(out + n, cap - n, "%s%s=%d", n ? "," : "", UI_OPTS[i].key, u->value[i]);
        if (w < 0 || n + (size_t)w >= cap) { out[n] = 0; return; }
        n += (size_t)w;
    }
}

static void fmt_value(const SmzUi *u, int opt, int v, char *out, size_t n);

/* "Label: value" for each option of a spec, as much as fits `cap` */
static void spec_summary(const SmzUi *u, const char *spec, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    if (!*spec) { snprintf(out, cap, "Default rules"); return; }
    while (*spec) {
        const char *eq = strchr(spec, '='), *end = strchr(spec, ',');
        if (!eq) break;
        if (!end) end = spec + strlen(spec);
        int i = eq < end ? opt_by_key(spec, (size_t)(eq - spec)) : -1;
        if (i >= 0) {
            char v[44], part[80];
            fmt_value(u, i, atoi(eq + 1), v, sizeof v);
            snprintf(part, sizeof part, "%s%s: %s", n ? ", " : "", UI_OPTS[i].label, v);
            if (n + strlen(part) + 3 >= cap) {   /* what fits of this one, then ".." */
                size_t room = cap - n - 3;
                if (room > strlen(part)) room = strlen(part);
                snprintf(out + n, cap - n, "%.*s..", (int)room, part);
                return;
            }
            memcpy(out + n, part, strlen(part) + 1);
            n += strlen(part);
        }
        spec = *end ? end + 1 : end;
    }
}

static int saved_add(SmzUi *u, const char *name, const char *spec)
{
    int i;
    for (i = 0; i < u->saved_count && strcasecmp(u->saved[i].name, name); i++) {}
    if (i == u->saved_count) {
        if (i >= UI_MAX_SAVED) return -1;
        u->saved_count++;
    }
    snprintf(u->saved[i].name, sizeof u->saved[i].name, "%s", name);
    snprintf(u->saved[i].spec, sizeof u->saved[i].spec, "%s", spec);
    u->saved_dirty = 1;
    return 0;
}

void ui_saved_load(SmzUi *u, const char *text)
{
    u->saved_count = 0;
    u->saved_cursor = u->saved_scroll = 0;
    if (!text) {
        for (size_t i = 0; i < sizeof BUILTIN / sizeof BUILTIN[0]; i++) saved_add(u, BUILTIN[i].name, BUILTIN[i].spec);
        return;
    }
    while (*text && u->saved_count < UI_MAX_SAVED) {
        const char *nl = strchr(text, '\n'), *tab = strchr(text, '\t');
        if (!nl) nl = text + strlen(text);
        if (tab && tab < nl) {
            UiSaved *s = &u->saved[u->saved_count++];
            snprintf(s->name, sizeof s->name, "%.*s", (int)(tab - text), text);
            snprintf(s->spec, sizeof s->spec, "%.*s", (int)(nl - tab - 1), tab + 1);
        }
        text = *nl ? nl + 1 : nl;
    }
    u->saved_dirty = 0;
}

int ui_saved_text(const SmzUi *u, char *out, size_t cap)
{
    size_t n = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < u->saved_count; i++) {
        int w = snprintf(out + n, cap - n, "%s\t%s\n", u->saved[i].name, u->saved[i].spec);
        if (w < 0 || n + (size_t)w >= cap) return -1;
        n += (size_t)w;
    }
    return (int)n;
}

int ui_saved_dirty(SmzUi *u) { int d = u->saved_dirty; u->saved_dirty = 0; return d; }

static void saved_remove(SmzUi *u, int i)
{
    for (; i + 1 < u->saved_count; i++) u->saved[i] = u->saved[i + 1];
    u->saved_count--;
    u->saved_dirty = 1;
}

static const struct { const char *code, *name; } COUNTRIES[] = {
    {"", "No flag"}, {"AR","Argentina"}, {"AU","Australia"}, {"AT","Austria"}, {"BE","Belgium"}, {"BR","Brazil"}, {"CA","Canada"},
    {"CL","Chile"}, {"CN","China"}, {"CO","Colombia"}, {"CZ","Czechia"}, {"DK","Denmark"}, {"FI","Finland"}, {"FR","France"},
    {"DE","Germany"}, {"GR","Greece"}, {"HU","Hungary"}, {"IN","India"}, {"IE","Ireland"}, {"IT","Italy"}, {"JP","Japan"},
    {"MX","Mexico"}, {"NL","Netherlands"}, {"NZ","New Zealand"}, {"NO","Norway"}, {"PE","Peru"}, {"PL","Poland"}, {"PT","Portugal"},
    {"RO","Romania"}, {"RU","Russia"}, {"ZA","South Africa"}, {"KR","South Korea"}, {"ES","Spain"}, {"SE","Sweden"}, {"CH","Switzerland"},
    {"TR","Turkey"}, {"UA","Ukraine"}, {"GB","United Kingdom"}, {"US","United States"}, {"UY","Uruguay"}, {"VE","Venezuela"},
};
#define COUNTRY_COUNT ((int)(sizeof COUNTRIES / sizeof COUNTRIES[0]))
#define COUNTRY_ROWS (8 + (UI_H - 240) / 25)

void ui_player_enable(SmzUi *u, int on) { u->player_on = on; }

void ui_ladder_enable(SmzUi *u, int on) { u->ladder_on = on; }

void ui_daily_enable(SmzUi *u, int on) { u->daily_on = on; if (!on) u->daily_sel = 0; slot_view(u); }
static int daily_button(const SmzUi *u);
void ui_daily_set(SmzUi *u, const SmzDailyInfo *d, int has_run)
{
    int had = daily_button(u);
    int64_t began = !d->began_at && !strcmp(d->window, u->daily.window) ? u->daily.began_at : d->began_at;   /* the host's answers do not carry it */
    u->daily = *d; u->daily_has_run = has_run; u->daily.began_at = began;
    if (u->screen == UI_DAILY && !had && daily_button(u)) u->daily_cursor = 0;   /* the answer came while the screen was open */
}

void ui_daily_began(SmzUi *u, int64_t began_at) { u->daily.began_at = began_at; }

/* What the Daily screen's main button does: 0 nothing (finished, or no window open), 1 Start, 2 Continue. */
static int daily_button(const SmzUi *u)
{
    const SmzDailyInfo *d = &u->daily;
    if (d->loaded != 1 || !d->window[0] || d->state == DAILY_DONE || d->state == DAILY_LATE) return 0;
    if (d->state == DAILY_OPEN) return u->daily_has_run ? 2 : 1;
    return 2;   /* started (or past the window): the attempt goes on, also from another device's start */
}

static void daily_input(SmzUi *u, UiButton b)
{
    int go = daily_button(u);
    if (u->daily_info) {   /* the Info page has only Back */
        if (b == BTN_A || b == BTN_B) u->daily_info = 0;
        return;
    }
    int *c = &u->daily_cursor, skins = go == 1;   /* before the start, 3 and 4 pick the Link and Samus the run is made with */
    if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_UP) { if (skins && *c < 3) *c = *c == 2 ? 4 : 3; }
    else if (b == BTN_DOWN) { if (*c >= 3) *c = *c == 4 ? 2 : 0; }
    else if (b == BTN_LEFT) { if (*c == 4) *c = 3; else if (*c < 3 && *c > (go ? 0 : 1)) (*c)--; }
    else if (b == BTN_RIGHT) { if (*c == 3) *c = 4; else if (*c < 2) (*c)++; }
    else if (b == BTN_A) {
        if (*c >= 3) { int g = *c - 3; u->sprite_game = g; u->sprite_cursor = u->value[sprite_opt(g)]; u->back = UI_DAILY; u->screen = UI_SPRITES; }
        else if (go && u->daily_cursor == 0) { u->action = ACT_DAILY; u->screen = UI_SLOTS_SCREEN; }
        else if (u->daily_cursor == 1) u->daily_info = 1;
        else u->screen = UI_SLOTS_SCREEN;
    }
}

void ui_ladder_begin(SmzUi *u, int tab, const char *label, int ok)
{
    if (tab < 0 || tab > 2) return;
    u->ladder[tab].loaded = ok ? 1 : -1;
    u->ladder[tab].n = 0;
    snprintf(u->ladder[tab].label, sizeof u->ladder[tab].label, "%s", label ? label : "");
}

void ui_ladder_add(SmzUi *u, int tab, const char *name, const char *country, int64_t ms)
{
    if (tab < 0 || tab > 2 || u->ladder[tab].n >= UI_LADDER_ROWS) return;
    int i = u->ladder[tab].n++;
    snprintf(u->ladder[tab].row[i].name, sizeof u->ladder[tab].row[i].name, "%s", name ? name : "");
    snprintf(u->ladder[tab].row[i].cc, sizeof u->ladder[tab].row[i].cc, "%s", country ? country : "");
    u->ladder[tab].row[i].ms = ms;
}

static int json_text(const char *from, const char *to, const char *key, char *out, size_t cap)
{
    char pat[24];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(from, pat);
    if (!p || (to && p >= to)) return 0;
    p += strlen(pat);
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; }
    out[n] = 0;
    return 1;
}

void ui_ladder_load_json(SmzUi *u, int tab, const char *json)
{
    if (!json || !*json) { ui_ladder_begin(u, tab, "", 0); return; }
    char label[16] = "", preset[24] = "";
    const char *runs = strstr(json, "\"runs\":");
    if (tab == 0 && runs) json_text(json, runs, "preset", preset, sizeof preset);
    if (tab == 0 && runs && strcmp(preset, ui_ladder_preset_key(u))) return;   /* asked for before another table was picked */
    json_text(json, runs, "month", label, sizeof label) || json_text(json, runs, "week", label, sizeof label);
    ui_ladder_begin(u, tab, label, 1);
    for (const char *p = runs ? strchr(runs, '[') : NULL; p && (p = strchr(p, '{')); ) {
        const char *end = strchr(p, '}');
        if (!end) break;
        char name[24] = "", cc[3] = "";
        json_text(p, end, "player", name, sizeof name);
        json_text(p, end, "country", cc, sizeof cc);
        const char *t = strstr(p, "\"timeMs\":");
        ui_ladder_add(u, tab, name, cc, t && t < end ? atoll(t + 9) : 0);
        p = end + 1;
    }
}

static int64_t json_num(const char *from, const char *to, const char *key)
{
    char pat[24];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(from, pat);
    return p && (!to || p < to) ? atoll(p + strlen(pat)) : 0;
}

/* The month before (the "prev" object): its goals, each with its top 10 as "name|CC|count;...". */
static void world_prev_load(SmzUi *u, const char *prev)
{
    memset(&u->world_prev, 0, sizeof u->world_prev);
    const char *goals = prev ? strstr(prev, "\"goals\":") : NULL;
    if (!goals) return;
    json_text(prev, goals, "month", u->world_prev.month, sizeof u->world_prev.month);
    u->world_prev.counted = (int)json_num(prev, goals, "counted");
    u->world_prev.gems = (int)json_num(prev, goals, "gems");
    for (const char *p = strchr(goals, '['); p && (p = strchr(p, '{')) && u->world_prev.n < UI_WORLD_GOALS; ) {
        const char *end = strchr(p, '}');
        if (!end) break;
        typeof(u->world_prev.g[0]) *g = &u->world_prev.g[u->world_prev.n++];
        json_text(p, end, "key", g->key, sizeof g->key);
        json_text(p, end, "title", g->title, sizeof g->title);
        json_text(p, end, "sprite", g->sprite, sizeof g->sprite);
        g->target = json_num(p, end, "target"); g->total = json_num(p, end, "total"); g->mine = json_num(p, end, "mine");
        g->rank = (int)json_num(p, end, "rank"); g->players = (int)json_num(p, end, "players");
        char top[640] = "";
        json_text(p, end, "top", top, sizeof top);
        for (char *e = top; *e && g->nt < 10; ) {
            char *next = strchr(e, ';'), *bar1 = strchr(e, '|');
            if (next) *next = 0;
            char *bar2 = bar1 ? strchr(bar1 + 1, '|') : NULL;
            if (bar1 && bar2) {
                *bar1 = *bar2 = 0;
                snprintf(g->top[g->nt].name, sizeof g->top[0].name, "%s", e);
                snprintf(g->top[g->nt].cc, sizeof g->top[0].cc, "%s", bar1 + 1);
                g->top[g->nt++].n = atoll(bar2 + 1);
            }
            if (!next) break;
            e = next + 1;
        }
        p = end + 1;
        if (*p == ']') break;
    }
    if (!u->world_prev.n) u->world_month = 0;
}

void ui_world_load_json(SmzUi *u, const char *json)
{
    if (!json || !*json) { if (u->world.loaded != 1) u->world.loaded = -1; return; }   /* a failed refresh keeps what was shown */
    const char *goals = strstr(json, "\"goals\":");
    if (!goals) { u->world.loaded = -1; return; }
    memset(&u->world, 0, sizeof u->world);
    json_text(json, goals, "month", u->world.month, sizeof u->world.month);
    u->world.ends_at = json_num(json, goals, "endsAt");
    const char *after = strchr(goals, ']');
    u->world.counted = (int)json_num(after ? after : goals, NULL, "counted");
    u->world.gems_if_met = (int)json_num(after ? after : goals, NULL, "gemsIfMet");
    for (const char *p = strchr(goals, '['); p && (p = strchr(p, '{')) && (!after || p < after) && u->world.n < UI_WORLD_GOALS; ) {
        const char *end = strchr(p, '}');
        if (!end) break;
        typeof(u->world.g[0]) *g = &u->world.g[u->world.n++];
        json_text(p, end, "key", g->key, sizeof g->key);
        json_text(p, end, "title", g->title, sizeof g->title);
        json_text(p, end, "sprite", g->sprite, sizeof g->sprite);
        g->target = json_num(p, end, "target"); g->total = json_num(p, end, "total");
        g->min = json_num(p, end, "min"); g->mine = json_num(p, end, "mine");
        p = end + 1;
    }
    u->world.loaded = 1;
    world_prev_load(u, strstr(after ? after : goals, "\"prev\":"));
}

int ui_world_showing(const SmzUi *u) { return (u->screen == UI_LADDER && u->ladder_tab == 3) || (u->screen == UI_SLOTS_SCREEN && world_row(u)); }

/* "November" from "2026-11". */
static const char *month_name(const char *ym)
{
    static const char *const M[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    int m = strlen(ym) >= 7 ? atoi(ym + 5) : 0;
    return m >= 1 && m <= 12 ? M[m - 1] : "";
}

static void thousands(char *out, size_t cap, int64_t v)
{
    char d[24];
    snprintf(d, sizeof d, "%lld", (long long)v);
    size_t n = strlen(d), o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; i++) { if (i && (n - i) % 3 == 0) out[o++] = ','; out[o++] = d[i]; }
    out[o] = 0;
}

static void gem(SmzUi *u, int x, int y, int have, int red);
static void rupeergy(SmzUi *u, int x, int y);
static void title(SmzUi *u, const char *t);

/* "4.1%" of total. */
static void share(char *out, size_t cap, int64_t part, int64_t total)
{
    int t = total > 0 ? (int)(part * 1000 / total) : 0;
    snprintf(out, cap, "%d.%d%%", t / 10, t % 10);
}

static void world_tab(SmzUi *u)
{
    char s[64], a[16], b[16];
    if (u->world.loaded == 0) { text(u, UI_W / 2, 120, C_DIM, 1, "Loading ..."); return; }
    if (u->world.loaded < 0) { text(u, UI_W / 2, 120, C_WARN, 1, "No connection"); return; }
    int last = u->world_month && u->world_prev.n;
    /* the month switch */
    static const char *const MONTHS[2] = {"This month", "Last month"};
    for (int m = 0; m < 1 + (u->world_prev.n > 0); m++) {
        int x = 6 + m * 78, on = m == last;
        rect(u, x, 57, 74, 13, u->world_cursor < 0 && on ? C_SEL : on ? C_PANEL : C_BG);
        text(u, x + 37, 57, on ? C_TEXT : C_DIM, 1, MONTHS[m]);
        hit(u, H_WMONTH, m, x, 57, 74, 13);
    }
    if (last) {
        if (u->world_prev.counted) { snprintf(s, sizeof s, "You counted in %d: +%d", u->world_prev.counted, u->world_prev.gems); text(u, UI_W - 20, 58, C_GREEN, 2, s); rupeergy(u, UI_W - 16, 57); }
        else text(u, UI_W - 8, 58, C_DIM, 2, "You did not count");
    } else {
        int64_t left = u->world.ends_at - (int64_t)time(NULL) * 1000;
        int days = left > 0 ? (int)((left + 86399999) / 86400000) : 0;
        snprintf(s, sizeof s, "%d day%s left", days, days == 1 ? "" : "s");
        if (u->world.counted) {
            snprintf(s + strlen(s), sizeof s - strlen(s), "  %d if met", u->world.gems_if_met);
            rupeergy(u, UI_W - 16, 57);
        }
        text(u, UI_W - (u->world.counted ? 20 : 8), 58, left > 0 ? C_CYAN : C_DIM, 2, left > 0 ? s : "Ended: rupeergies paid");
    }
    int n = last ? u->world_prev.n : u->world.n;
    for (int i = 0; i < n && i < 4; i++) {
        const char *title_ = last ? u->world_prev.g[i].title : u->world.g[i].title, *sprite = last ? u->world_prev.g[i].sprite : u->world.g[i].sprite;
        int64_t target = last ? u->world_prev.g[i].target : u->world.g[i].target, total = last ? u->world_prev.g[i].total : u->world.g[i].total;
        int y = 74 + i * 38, met = target > 0 && total >= target;
        rect(u, 6, y, UI_W - 12, 36, u->world_cursor == i ? C_SEL : C_PANEL);
        if (met) rect(u, 6, y, 3, 36, C_GOLD);
        else if (last) rect(u, 6, y, 3, 36, C_WARN);
        hit(u, H_WGOAL, i, 6, y, UI_W - 12, 36);
        rect(u, 10, y + 2, 32, 32, C_BG);
        if (sprite[0]) { snprintf(s, sizeof s, "goal/%s", sprite); put(u, 2, 10, y + 2, 32, 32, C_TEXT, 0, s); }
        text(u, 48, y + 3, C_TEXT, 0, title_);
        thousands(a, sizeof a, total); thousands(b, sizeof b, target);
        snprintf(s, sizeof s, "%s / %s", a, b);
        text(u, UI_W - 10, y + 3, met ? C_ACCENT : last ? C_WARN : C_TEXT, 2, last ? (met ? "MET" : "MISSED") : met ? "Met" : s);
        int bw = last ? UI_W - 60 - 112 : UI_W - 60;
        rect(u, 48, y + 17, bw, 5, C_BG);
        int fill = target > 0 ? (int)(bw * (total > target ? target : total) / target) : 0;
        if (fill) rect(u, 48, y + 17, fill, 5, met ? C_ACCENT : C_GREEN);
        if (last) {
            const typeof(u->world_prev.g[0]) *g = &u->world_prev.g[i];
            text(u, 48 + bw + 6, y + 15, C_DIM, 0, s);
            if (g->rank) { share(a, sizeof a, g->mine, g->total); snprintf(s, sizeof s, "You %s  #%d of %d", a, g->rank, g->players); }
            else snprintf(s, sizeof s, "You did not take part");
            text(u, 48, y + 24, g->rank ? C_GREEN : C_DIM, 0, s);
            continue;
        }
        const typeof(u->world.g[0]) *w = &u->world.g[i];
        thousands(a, sizeof a, w->mine);
        if (w->mine >= w->min) snprintf(s, sizeof s, "You %s  counts", a);
        else { thousands(b, sizeof b, w->min - w->mine); snprintf(s, sizeof s, "You %s  -  %s more to count", a, b); }
        text(u, 48, y + 24, w->mine >= w->min ? C_GREEN : C_DIM, 0, s);
    }
}

/* Last month's top 10 of one goal: rank, flag, name, what they gave and their share of the total. */
static void world_top10(SmzUi *u)
{
    const typeof(u->world_prev.g[0]) *g = &u->world_prev.g[u->world_top];
    char s[80], a[16], b[16];
    snprintf(s, sizeof s, "%s  -  %s", month_name(u->world_prev.month), g->title);
    title(u, s);
    int met = g->target > 0 && g->total >= g->target;
    thousands(a, sizeof a, g->total); thousands(b, sizeof b, g->target);
    snprintf(s, sizeof s, "%s / %s  %s", a, b, met ? "MET" : "MISSED");
    text(u, UI_W - 8, 32, met ? C_ACCENT : C_WARN, 2, s);
    if (!g->nt) { text(u, UI_W / 2, 120, C_DIM, 1, "Nobody took part"); return; }
    int64_t best = g->top[0].n, shown = 0;
    int bx = UI_W - 12 - 56 - 120, bw = 120;
    for (int i = 0; i < g->nt; i++) {
        int y = 46 + i * 17, me = g->rank == i + 1;
        rect(u, 6, y, UI_W - 12, 15, me ? C_SEL : i % 2 ? C_BG : C_PANEL);
        snprintf(s, sizeof s, "#%d", i + 1);
        text(u, 30, y + 3, i < 3 ? C_ACCENT : C_DIM, 2, s);
        flag(u, 36, y + 3, C_ACCENT, g->top[i].cc);
        text(u, 54, y + 3, C_TEXT, 0, g->top[i].name);
        thousands(a, sizeof a, g->top[i].n);
        text(u, bx - 8, y + 3, C_DIM, 2, a);
        rect(u, bx, y + 6, bw, 4, C_BG);
        int fill = best > 0 ? (int)(bw * g->top[i].n / best) : 0;
        if (fill) rect(u, bx, y + 6, fill, 4, i < 3 ? C_ACCENT : C_GREEN);
        share(a, sizeof a, g->top[i].n, g->total);
        text(u, UI_W - 12, y + 3, C_TEXT, 2, a);
        shown += g->top[i].n;
    }
    if (g->players > g->nt) {
        thousands(a, sizeof a, g->total - shown); share(b, sizeof b, g->total - shown, g->total);
        snprintf(s, sizeof s, "Everyone else: %s (%s) from %d players", a, b, g->players - g->nt);
        text(u, 8, 46 + 10 * 17 + 2, C_DIM, 0, s);
    } else if (g->rank > 10) {
        share(a, sizeof a, g->mine, g->total);
        snprintf(s, sizeof s, "You: #%d  %s", g->rank, a);
        text(u, 8, 46 + 10 * 17 + 2, C_GREEN, 0, s);
    }
}

static void ladder_open(SmzUi *u)
{
    u->screen = UI_LADDER;
    u->ladder_scroll = 0;
    for (int t = 0; t < 3; t++) if (u->ladder[t].loaded < 0) u->ladder[t].loaded = 0;
    if (u->world.loaded < 0) u->world.loaded = 0;
    u->action = ACT_LADDER;
}

static void ladder_input(SmzUi *u, UiButton b)
{
    int n = u->ladder_tab < 3 ? u->ladder[u->ladder_tab].n : 0;
    if (u->ladder_tab == 3 && u->world_top >= 0) { if (b == BTN_B || b == BTN_A) u->world_top = -1; return; }
    int wn = u->world_month && u->world_prev.n ? u->world_prev.n : u->world.n;
    if (wn > 4) wn = 4;
    if (u->ladder_tab == 3 && (b == BTN_UP || b == BTN_DOWN)) {   /* the month switch, then the goals */
        u->world_cursor = (u->world_cursor + 1 + (b == BTN_UP ? wn : 1)) % (wn + 1) - 1;
        return;
    }
    if (u->ladder_tab == 3 && b == BTN_A) {
        if (u->world_cursor < 0 && u->world_prev.n) u->world_month = !u->world_month;
        else if (u->world_cursor >= 0 && u->world_month && u->world_prev.n) u->world_top = u->world_cursor;
        if (u->world_cursor >= (u->world_month && u->world_prev.n ? u->world_prev.n : u->world.n)) u->world_cursor = -1;
        return;
    }
    const UiLadderPreset *p;
    int presets = ui_ladder_presets(u->game_mode, &p);
    if (b == BTN_B || b == BTN_SELECT) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_A && u->ladder_tab == 0 && presets) {
        u->ladder_preset = (u->ladder_preset + 1) % (presets + 1);
        u->ladder[0].loaded = 0; u->ladder[0].n = 0; u->ladder_scroll = 0;
        u->action = ACT_LADDER;
    }
    else if (b == BTN_LEFT || b == BTN_L) { u->ladder_tab = (u->ladder_tab + 3) % 4; u->ladder_scroll = 0; }
    else if (b == BTN_RIGHT || b == BTN_R) { u->ladder_tab = (u->ladder_tab + 1) % 4; u->ladder_scroll = 0; }
    else if (b == BTN_DOWN && u->ladder_scroll + UI_LADDER_SHOWN < n) u->ladder_scroll++;
    else if (b == BTN_UP && u->ladder_scroll > 0) u->ladder_scroll--;
}

void ui_player_load(SmzUi *u, const char *text)
{
    u->player[0] = u->country[0] = 0;
    if (!text) return;
    const char *tab = strchr(text, '\t');
    size_t n = tab ? (size_t)(tab - text) : strcspn(text, "\n");
    if (n >= sizeof u->player) n = sizeof u->player - 1;
    memcpy(u->player, text, n); u->player[n] = 0;
    if (tab && tab[1] >= 'A' && tab[1] <= 'Z' && tab[2] >= 'A' && tab[2] <= 'Z') { u->country[0] = tab[1]; u->country[1] = tab[2]; u->country[2] = 0; }
    const char *t2 = tab ? strchr(tab + 1, '\t') : NULL;
    u->online_mode = t2 && t2[1] >= '0' && t2[1] <= '2' ? t2[1] - '0' : 0;
}

void ui_set_monthly_game(SmzUi *u, int mode, const char *label, int seed, int days_left)
{
    if (mode < 0 || mode >= SMZ_MODE_COUNT) return;
    UiGamePeriods *g = &u->periods[mode];
    g->monthly_days = days_left;
    snprintf(g->monthly_label, sizeof g->monthly_label, "%s", label ? label : "");
    g->monthly_seed = seed > 0 ? seed : 0;
    if (mode != u->game_mode) return;
    u->monthly_days = g->monthly_days;
    snprintf(u->monthly_label, sizeof u->monthly_label, "%s", g->monthly_label);
    u->monthly_seed = g->monthly_seed;
}

void ui_set_monthly(SmzUi *u, const char *label, int seed, int days_left) { ui_set_monthly_game(u, SMZ_MODE_SMZ3, label, seed, days_left); }

int ui_run_monthly(const SmzUi *u) { return u->run_mode == 4; }
int ui_run_weekly(const SmzUi *u) { return u->run_mode == 5; }

void ui_set_weekly_game(SmzUi *u, int mode, const char *label, int seed, int days_left)
{
    if (mode < 0 || mode >= SMZ_MODE_COUNT) return;
    UiGamePeriods *g = &u->periods[mode];
    g->weekly_days = days_left;
    snprintf(g->weekly_label, sizeof g->weekly_label, "%s", label ? label : "");
    g->weekly_seed = seed > 0 ? seed : 0;
    if (mode != u->game_mode) return;
    u->weekly_days = g->weekly_days;
    snprintf(u->weekly_label, sizeof u->weekly_label, "%s", g->weekly_label);
    u->weekly_seed = g->weekly_seed;
}

void ui_set_weekly(SmzUi *u, const char *label, int seed, int days_left) { ui_set_weekly_game(u, SMZ_MODE_SMZ3, label, seed, days_left); }

void ui_set_playing(SmzUi *u, int mode, int weekly, int n)
{
    if (mode < 0 || mode >= SMZ_MODE_COUNT) return;
    *(weekly ? &u->periods[mode].weekly_playing : &u->periods[mode].monthly_playing) = n > 0 ? n : 0;
}

/* The entered game's players on the seed of a run kind: online 2 monthly, 3 weekly (UiSlot.online); 0 for any other. */
static int playing_on(const SmzUi *u, int online)
{
    const UiGamePeriods *g = &u->periods[u->game_mode];
    return online == 2 ? g->monthly_playing : online == 3 ? g->weekly_playing : 0;
}

int ui_run_ladder(const SmzUi *u) { return online_game(u) && (u->run_mode >= 4 || ui_is_default(u)); }
int ui_online_now(const SmzUi *u) { return u->player_on && u->online_mode == 1 && ui_run_ladder(u); }

int ui_player_text(const SmzUi *u, char *out, size_t cap)
{
    int w = snprintf(out, cap, "%s\t%s\t%d\n", u->player, u->country, u->online_mode);
    return w < 0 || (size_t)w >= cap ? -1 : w;
}

int ui_player_dirty(SmzUi *u) { int d = u->player_dirty; u->player_dirty = 0; return d; }

static void country_open(SmzUi *u)
{
    u->country_cursor = 0;
    for (int i = 0; i < COUNTRY_COUNT; i++) if (!strcmp(COUNTRIES[i].code, u->country)) u->country_cursor = i;
    u->country_scroll = u->country_cursor < COUNTRY_ROWS ? 0 : u->country_cursor - COUNTRY_ROWS + 1;
    u->screen = UI_COUNTRY;
}

static void country_input(SmzUi *u, UiButton b)
{
    int n = COUNTRY_COUNT, *c = &u->country_cursor;
    if (b == BTN_B) { u->screen = u->player_opts ? UI_OPTIONS : UI_START; return; }
    if (b == BTN_UP) *c = (*c + n - 1) % n;
    else if (b == BTN_DOWN) *c = (*c + 1) % n;
    else if (b == BTN_L) *c = *c - COUNTRY_ROWS < 0 ? 0 : *c - COUNTRY_ROWS;
    else if (b == BTN_R) *c = *c + COUNTRY_ROWS >= n ? n - 1 : *c + COUNTRY_ROWS;
    else if (b == BTN_A) {
        snprintf(u->country, sizeof u->country, "%s", COUNTRIES[*c].code);
        u->player_dirty = 1;
        u->screen = u->player_opts ? UI_OPTIONS : UI_START;
        if (u->online_gen && !u->player_opts) { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }   /* the flag asked after Yes on Start */
        return;
    }
    if (*c < u->country_scroll) u->country_scroll = *c;
    if (*c >= u->country_scroll + COUNTRY_ROWS) u->country_scroll = *c - COUNTRY_ROWS + 1;
}

void ui_load_choice(SmzUi *u, int slot, int64_t save_ms, int save_z, int save_m, int64_t state_ms, int state_z, int state_m, int zelda_total, int metroid_total)
{
    u->lc.save_ms = save_ms; u->lc.state_ms = state_ms;
    u->lc.save_z = save_z; u->lc.save_m = save_m; u->lc.state_z = state_z; u->lc.state_m = state_m;
    u->lc.zt = zelda_total; u->lc.mt = metroid_total;
    u->lc.cursor = 1;                       /* the save state is the newest */
    u->active_slot = slot;
    u->screen = UI_LOADCHOICE;
}

static void loadchoice_input(SmzUi *u, UiButton b)
{
    if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_LEFT || b == BTN_RIGHT) u->lc.cursor = b == BTN_LEFT ? 0 : 1;
    else if (b == BTN_A) {
        u->action = u->lc.cursor ? ACT_LOAD_STATE : ACT_LOAD_SAVE;
        u->action_slot = u->active_slot;
        u->screen = UI_SLOTS_SCREEN;
    }
}

#define START_ROW 3

/* After Yes the missing name is asked, then the flag; the host starts the run when Start is pressed again. */
static void online_input(SmzUi *u, UiButton b)
{
    int *c = &u->online_cursor;
    if (b == BTN_B) { u->screen = UI_START; return; }
    if (b == BTN_UP) *c = (*c + 2) % 3;
    else if (b == BTN_DOWN) *c = (*c + 1) % 3;
    else if (b == BTN_A) {
        u->player_dirty = 1;
        if (*c == 0) {
            if (!u->player[0]) { u->name_for = 3; u->name[0] = 0; u->name_len = 0; u->key_cursor = 0; u->screen = UI_NAME; return; }
            u->online_mode = 1;
            if (u->online_gen) { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }   /* asked on Start: it starts */
            else u->screen = UI_START;
        } else {
            u->online_mode = *c == 2 ? 2 : 0;   /* not now: asked again next run */
            if (u->online_gen) { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }
            else u->screen = UI_START;
        }
    }
}

static void goto_start(SmzUi *u)
{
    u->player_opts = 0;
    u->screen = UI_START;
    u->start_cursor = START_ROW;
    u->start_col = 0;
    u->start_from = u->run_mode == 2 ? UI_CREATE : u->run_mode == 3 ? UI_SAVED : UI_MODE;
}

static void begin_run(SmzUi *u, int mode, const char *label, const char *spec)
{
    u->keep_custom = 0;
    reset_values(u);
    apply_spec(u, spec);
    u->run_mode = mode;
    snprintf(u->start_label, sizeof u->start_label, "%s", label);
    if (mode == 2) { u->screen = UI_CREATE; u->level = LEVEL_QUICK; for (int l = 0; l < LEVEL_COUNT; l++) u->cursor[l] = u->scroll[l] = 0; }
    else goto_start(u);
}

void ui_preset(SmzUi *u, int mode)
{
    if (mode == 0) begin_run(u, 0, chill(u)->name, chill(u)->spec);
    else begin_run(u, mode == 1 ? 1 : 2, mode == 1 ? original(u)->name : "Custom", mode == 1 ? original(u)->spec : "");
    u->screen = UI_SLOTS_SCREEN;
}

void ui_apply_spec(SmzUi *u, const char *spec) { apply_spec(u, spec); }

static int maprando_row(const SmzUi *u) { return sm_mode(u) && smz_sm_maprando_state() != SMZ_MR_NONE; }
/* A Link to the Past's race presets have a row of their own; Super Metroid's are all Map Rando, so its row leads to them */
static int presets_row(const SmzUi *u) { const UiLadderPreset *p; return z3_mode(u) && ui_ladder_presets(u->game_mode, &p) > 0; }
static int mode_rows(const SmzUi *u)
{
    const UiLadderPreset *p;
    if (u->mode_list) return ui_ladder_presets(u->game_mode, &p) + sm_mode(u);
    return 3 + maprando_row(u) + presets_row(u) + (u->saved_count > 0 && !single_game(u)) + (online_game(u) && u->monthly_seed > 0) + (online_game(u) && u->weekly_seed > 0);
}

#define MODE_PRESET 10   /* mode_kind of race preset k: MODE_PRESET + k */
/* 0 Cozy, 1 Original, 2 Custom, 3 Saved customs, 4 Monthly seed, 5 Weekly run, 6 Map Rando, 7 Race presets (the rows that apply,
 * in this order); in the presets list each preset, then Super Metroid's own Map Rando run */
static int mode_kind(const SmzUi *u, int row)
{
    if (u->mode_list) {
        const UiLadderPreset *p;
        int n = ui_ladder_presets(u->game_mode, &p);
        return row < n ? MODE_PRESET + row : 6;
    }
    int kinds[8] = {0, 1, 2}, n = 3;
    if (maprando_row(u)) kinds[n++] = 6;
    if (presets_row(u)) kinds[n++] = 7;
    if (u->saved_count > 0 && !single_game(u)) kinds[n++] = 3;   /* saved customs carry SMZ3 options */
    if (online_game(u) && u->monthly_seed > 0) kinds[n++] = 4;   /* the server's seeds: none for an offline game */
    if (online_game(u) && u->weekly_seed > 0) kinds[n++] = 5;
    return kinds[row < n ? row : n - 1];
}

#define KEY_CHARS_NAME "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM.'!"   /* QWERTY, 10 to a row */
#define KEY_CHARS_ADDR "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM.:/"   /* an address: : and / for the scheme and port */
#define KEY_CHARS (u->name_for == 5 ? KEY_CHARS_ADDR : KEY_CHARS_NAME)
#define KEY_COLS 10
#define KEY_ROWS 4
#define KEY_COUNT (KEY_COLS * KEY_ROWS)
#define KEY_CTRL 4        /* Space, Delete, Save, Cancel */

/* Names fit the 24-byte name fields they go to; an address takes the whole buffer. */
static int name_cap(const SmzUi *u) { return u->name_for == 5 ? (int)sizeof u->name - 1 : 23; }

static void name_input(SmzUi *u, UiButton b)
{
    int c = u->key_cursor;
    UiScreen back = u->name_for == 6 ? UI_HOME : u->name_for >= 4 ? UI_CLOUD : u->name_for == 2 ? UI_RACE_WAY : u->name_for ? u->player_opts ? UI_OPTIONS : UI_START : UI_CREATE;
    if (b == BTN_B) { u->screen = back; return; }
    if (b == BTN_START) { u->key_cursor = KEY_COUNT + 2; return; }   /* to Save */
    if (b == BTN_L) { if (u->name_len) u->name[--u->name_len] = 0; return; }
    if (b == BTN_R) { if (u->name_len < name_cap(u)) { u->name[u->name_len++] = ' '; u->name[u->name_len] = 0; } return; }
    if (b == BTN_LEFT || b == BTN_RIGHT) {
        int d = b == BTN_LEFT ? -1 : 1;
        if (c < KEY_COUNT) u->key_cursor = c / KEY_COLS * KEY_COLS + (c % KEY_COLS + d + KEY_COLS) % KEY_COLS;
        else u->key_cursor = KEY_COUNT + (c - KEY_COUNT + d + KEY_CTRL) % KEY_CTRL;
    } else if (b == BTN_UP || b == BTN_DOWN) {
        if (c < KEY_COUNT) {
            int row = c / KEY_COLS, col = c % KEY_COLS;
            if (b == BTN_UP) u->key_cursor = row ? c - KEY_COLS : KEY_COUNT + col * KEY_CTRL / KEY_COLS;
            else u->key_cursor = row < KEY_ROWS - 1 ? c + KEY_COLS : KEY_COUNT + col * KEY_CTRL / KEY_COLS;
        } else {
            int k = c - KEY_COUNT;
            u->key_cursor = b == BTN_UP ? (KEY_ROWS - 1) * KEY_COLS + k * KEY_COLS / KEY_CTRL : k * KEY_COLS / KEY_CTRL;
        }
    } else if (b == BTN_A) {
        if (c < KEY_COUNT) { if (u->name_len < name_cap(u)) { u->name[u->name_len++] = KEY_CHARS[c]; u->name[u->name_len] = 0; } }
        else switch (c - KEY_COUNT) {
        case 0: if (u->name_len < name_cap(u)) { u->name[u->name_len++] = ' '; u->name[u->name_len] = 0; } break;
        case 1: if (u->name_len) u->name[--u->name_len] = 0; break;
        case 2: {
            char spec[UI_SPEC_LEN];
            int len = u->name_len;
            while (len && u->name[len - 1] == ' ') u->name[--len] = 0;
            if (!len) break;
            if (u->name_for == 5) {
                const char *t = u->name;
                while (*t == ' ') t++;
                snprintf(u->cloud.addr, sizeof u->cloud.addr, "%s%s", strstr(t, "://") ? "" : "https://", t);
                for (char *p = u->cloud.addr; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
                size_t al = strlen(u->cloud.addr);
                while (al && u->cloud.addr[al - 1] == '/') u->cloud.addr[--al] = 0;
                u->screen = UI_CLOUD;
                u->action = ACT_CLOUD;
                u->action_slot = CLOUD_ADDRESS;
                break;
            }
            if (u->name_for == 4) {   /* a link code: its digits */
                int k = 0;
                for (int i = 0; u->name[i] && k < (int)sizeof u->cloud.link - 1; i++) if (u->name[i] >= '0' && u->name[i] <= '9') u->cloud.link[k++] = u->name[i];
                u->cloud.link[k] = 0;
                u->screen = UI_CLOUD;
                if (k != 6) { ui_cloud_msg(u, "A link code has 6 digits"); break; }
                u->action = ACT_CLOUD;
                u->action_slot = CLOUD_LINK;
                ui_cloud_msg(u, "Linking...");
                break;
            }
            if (u->name_for == 2) {   /* a room code */
                int k = 0;
                for (int i = 0; u->name[i] && k < (int)sizeof u->race.code - 1; i++) if (u->name[i] != ' ') u->race.code[k++] = u->name[i];
                u->race.code[k] = 0;
                race_reset(u);
                u->action = ACT_RACE_JOIN;
                u->screen = UI_BUSY;
                snprintf(u->status, sizeof u->status, "Joining...");
                break;
            }
            if (u->name_for) {
                snprintf(u->player, sizeof u->player, "%s", u->name);
                u->player_dirty = 1;
                if (u->name_for == 3) u->online_mode = 1;   /* the online Yes counts once there is a name */
                u->screen = back;
                if (u->name_for == 6) { u->screen = paused_row(u) ? UI_RACE_SIZE : UI_RACE_WAY; break; }
                if (u->player_opts) { country_open(u); break; }   /* from Options the country follows the name */
                if (u->online_gen && !u->country[0]) { u->country_cursor = u->country_scroll = 0; u->screen = UI_COUNTRY; }
                else if (u->online_gen && u->name_for == 3) { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }   /* Yes on Start: it starts */
                break;
            }
            make_spec(u, spec, sizeof spec);
            if (saved_add(u, u->name, spec)) { u->keep_custom = 1; snprintf(u->notice, sizeof u->notice, "Full: free a slot, then Custom"); }
            else snprintf(u->notice, sizeof u->notice, "Saved: %.20s", u->name);
            u->screen = UI_CREATE;
            break;
        }
        default: u->screen = back; break;
        }
    }
}

static void saved_input(SmzUi *u, UiButton b)
{
    int n = u->saved_count;
    if (!n) { u->screen = UI_MODE; return; }
    if (b == BTN_UP || b == BTN_DOWN) {
        u->saved_cursor = (u->saved_cursor + (b == BTN_UP ? n - 1 : 1)) % n;
        u->saved_col = u->saved_confirm = 0;
    } else if (b == BTN_LEFT) u->saved_col = u->saved_confirm = 0;
    else if (b == BTN_RIGHT) u->saved_col = 1;
    else if (b == BTN_B) { if (u->saved_confirm) u->saved_confirm = 0; else u->screen = UI_MODE; }
    else if (b == BTN_A) {
        const UiSaved *s = &u->saved[u->saved_cursor];
        if (!u->saved_col) begin_run(u, 3, s->name, s->spec);
        else if (!u->saved_confirm) u->saved_confirm = 1;
        else {
            saved_remove(u, u->saved_cursor);
            u->saved_confirm = u->saved_col = 0;
            if (u->saved_cursor >= u->saved_count) u->saved_cursor = u->saved_count ? u->saved_count - 1 : 0;
            if (!u->saved_count) u->screen = UI_MODE;
        }
    }
    if (u->saved_cursor < u->saved_scroll) u->saved_scroll = u->saved_cursor;
    if (u->saved_cursor >= u->saved_scroll + SAVED_ROWS) u->saved_scroll = u->saved_cursor - SAVED_ROWS + 1;
}

static void random_sprite(SmzUi *u, int game)
{
    int n = u->sprite_count[game];
    if (n < 2) return;
    u->rng ^= u->rng << 13; u->rng ^= u->rng >> 17; u->rng ^= u->rng << 5;
    u->value[game ? UIO_SAMUS : UIO_LINK] = 1 + (int)(u->rng % (uint32_t)(n - 1));
}

/* The options that differ from the Original rules (as ui_is_default counts them), in list order; returns how many fit in idx. */
static int changed_opts(const SmzUi *u, int *idx)
{
    int n = 0;
    for (int i = 0; i < UI_OPT_COUNT && n < UI_MAX_OPTS; i++) {
        if (ui_opt_cosmetic(i) || ui_opt_hidden(u, i) || (UI_OPTS[i].kind == OPT_SPRITE ? !u->value[i] : u->value[i] == UI_OPTS[i].def)) continue;
        idx[n++] = i;
    }
    return n;
}

static void rules_input(SmzUi *u, UiButton b)
{
    int idx[UI_MAX_OPTS], n = changed_opts(u, idx), rows = ROWS_SHOWN;
    if (b == BTN_B || b == BTN_A) u->screen = UI_START;
    else if (b == BTN_DOWN && u->rules_scroll + rows < n) u->rules_scroll++;
    else if (b == BTN_UP && u->rules_scroll > 0) u->rules_scroll--;
}

static void start_input(SmzUi *u, UiButton b)
{
    int rows = START_ROW + 1, *cur = &u->start_cursor;
    if (b == BTN_B) { u->screen = u->start_from; return; }
    if (b == BTN_L) { u->rules_scroll = 0; u->screen = UI_RULES; return; }
    if (b == BTN_UP || b == BTN_DOWN) {
        int d = b == BTN_UP ? rows - 1 : 1;
        *cur = (*cur + d) % rows;
        if (*cur == 2 && (!u->player_on || !ui_run_ladder(u))) *cur = (*cur + d) % rows;   /* a local-only run has no online row to pick */
        if (*cur == 0 && sm_mode(u)) *cur = (*cur + d) % rows;   /* Super Metroid alone: Samus only */
        if (*cur == 1 && maprando_run(u)) *cur = (*cur + d) % rows;
        if (*cur == 1 && z3_mode(u)) *cur = (*cur + d) % rows;   /* A Link to the Past alone: Link only */
        if (*cur == 2 && (!u->player_on || !ui_run_ladder(u))) *cur = (*cur + d) % rows;
    }
    else if (b == BTN_LEFT) u->start_col = u->start_col > 0 && *cur == 2 ? u->start_col - 1 : 0;
    else if (b == BTN_RIGHT) u->start_col = *cur == 2 && u->online_mode == 1 ? (u->start_col < 2 ? u->start_col + 1 : 2) : *cur < START_ROW;
    else if (b == BTN_A) {
        if (*cur == START_ROW) {
            if (u->player_on && u->online_mode == 0 && ui_run_ladder(u)) { u->online_cursor = 0; u->online_gen = 1; u->screen = UI_ONLINE; }
            else { u->action = ACT_GENERATE; u->action_slot = u->active_slot; }
        }
        else if (*cur == 2 && u->online_mode != 1) { u->online_cursor = u->online_mode == 2 ? 2 : 0; u->online_gen = 0; u->screen = UI_ONLINE; }
        else if (*cur == 2) {
            if (u->start_col == 2) { u->online_cursor = 0; u->online_gen = 0; u->screen = UI_ONLINE; }
            else if (u->start_col) { u->online_gen = 0; u->country_cursor = 0; for (int i = 0; i < COUNTRY_COUNT; i++) if (!strcmp(COUNTRIES[i].code, u->country)) u->country_cursor = i; u->country_scroll = u->country_cursor < COUNTRY_ROWS ? 0 : u->country_cursor - COUNTRY_ROWS + 1; u->screen = UI_COUNTRY; }
            else { u->online_gen = 0; u->name_for = 1; snprintf(u->name, sizeof u->name, "%s", u->player); u->name_len = (int)strlen(u->name); u->key_cursor = 0; u->screen = UI_NAME; }
        }
        else if (u->start_col) random_sprite(u, *cur);
        else {
            u->sprite_game = *cur;
            u->sprite_cursor = u->value[*cur ? UIO_SAMUS : UIO_LINK];
            u->back = UI_START; u->screen = UI_SPRITES;
        }
    }
    if (*cur == START_ROW) u->start_col = 0;
}

static void mode_input(SmzUi *u, UiButton b)
{
    int n = mode_rows(u);
    if (b == BTN_UP) u->mode_cursor = (u->mode_cursor + n - 1) % n;
    else if (b == BTN_DOWN) u->mode_cursor = (u->mode_cursor + 1) % n;
    else if (b == BTN_B && u->mode_list) { u->mode_list = 0; u->mode_cursor = 3; }   /* back on the row that opened it */
    else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_A && mode_kind(u, u->mode_cursor) >= MODE_PRESET) {
        const UiLadderPreset *p;
        ui_ladder_presets(u->game_mode, &p);
        const UiLadderPreset *k = &p[mode_kind(u, u->mode_cursor) - MODE_PRESET];
        begin_run(u, 1, k->name, k->spec);
    }
    else if (b == BTN_A && mode_kind(u, u->mode_cursor) == 7) { u->mode_list = 1; u->mode_cursor = 0; }
    else if (b == BTN_A) switch (mode_kind(u, u->mode_cursor)) {
    case 0: begin_run(u, 0, chill(u)->name, chill(u)->spec); break;
    case 1: begin_run(u, 1, original(u)->name, original(u)->spec); break;
    case 2:
        if (u->keep_custom) {   /* back after freeing a saved slot: the options that did not fit are still there */
            u->keep_custom = 0; u->run_mode = 2; u->level = LEVEL_QUICK; u->screen = UI_CREATE;
            snprintf(u->start_label, sizeof u->start_label, "Custom");
        } else begin_run(u, 2, "Custom", "");
        break;
    case 4: {
        char label[24];
        snprintf(label, sizeof label, "Monthly %s", u->monthly_label);
        begin_run(u, 4, label, ORIGINAL.spec);
        for (int i = 0; i < UI_OPT_COUNT; i++) if (!strcmp(UI_OPTS[i].key, "Seed")) u->value[i] = u->monthly_seed;
        break;
    }
    case 5:
        begin_run(u, 5, "Weekly", weekly(u)->spec);
        snprintf(u->start_label, sizeof u->start_label, "Weekly %s", u->weekly_label);
        for (int i = 0; i < UI_OPT_COUNT; i++) if (!strcmp(UI_OPTS[i].key, "Seed")) u->value[i] = u->weekly_seed;
        break;
    case 6:
        if (smz_sm_maprando_state() == SMZ_MR_OFFLINE) snprintf(u->notice, sizeof u->notice, "Map Rando needs an internet connection");
        else if (!u->mode_list) { u->mode_list = 1; u->mode_cursor = 0; }
        else begin_run(u, 2, "Map Rando", "SmGenerator=1");
        break;
    default: u->screen = UI_SAVED; u->saved_cursor = u->saved_scroll = u->saved_col = u->saved_confirm = 0; break;
    }
}

void ui_input(SmzUi *u, UiButton b)
{
    u->launching = 0;
    if (b == BTN_START && u->screen != UI_NAME) return;
    if (u->screen == UI_LOGO) return;   /* its 2 seconds can't be skipped */
    u->notice[0] = 0;
    switch (u->screen) {
    case UI_SLOTS_SCREEN:
        if (DAILY_ON(u) && (b == BTN_UP || b == BTN_DOWN)) {   /* the Daily (and World) row sits between the last slot and the first */
            int n = 1 + u->slot_count, pos = u->daily_sel ? 0 : 1 + u->slot_cursor;
            pos = (pos + (b == BTN_UP ? n - 1 : 1)) % n;
            if (!pos) u->daily_sel = 1;
            else { u->daily_sel = 0; u->slot_cursor = pos - 1; }
            slot_view(u);
        }
        else if (u->daily_sel && world_row(u) && (b == BTN_LEFT || b == BTN_RIGHT)) u->daily_sel = 3 - u->daily_sel;
        else if (b == BTN_UP || b == BTN_DOWN) { u->slot_cursor = (u->slot_cursor + (b == BTN_UP ? u->slot_count - 1 : 1)) % u->slot_count; slot_view(u); }
        else if (b == BTN_A && u->daily_sel == 2) { ladder_open(u); u->ladder_tab = 3; }
        else if (b == BTN_A && u->daily_sel) { u->screen = UI_DAILY; u->daily_info = 0; u->daily_cursor = daily_button(u) ? 0 : 2; }
        else if (b == BTN_L || b == BTN_R) {
            u->daily_sel = 0;
            int c = u->slot_cursor + (b == BTN_L ? -slots_shown(u) : slots_shown(u));
            u->slot_cursor = c < 0 ? 0 : c >= u->slot_count ? u->slot_count - 1 : c;
            slot_view(u);
        }
        else if (b == BTN_SELECT && LADDER_ON(u)) ladder_open(u);
        else if (b == BTN_B && u->home_on) u->screen = UI_HOME;
        else if (b == BTN_A) {
            u->active_slot = u->last_slot[u->game_mode] = u->slot_cursor;
            if (u->slots[u->slot_cursor].used) { u->screen = UI_SLOT_MENU; u->menu_cursor = 0; }
            else { u->screen = UI_MODE; u->mode_cursor = 0; u->mode_list = 0; }
        }
        break;
    case UI_SLOT_MENU:
        if (b == BTN_UP || b == BTN_DOWN) u->menu_cursor = !u->menu_cursor;
        else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
        else if (b == BTN_A) {
            if (u->menu_cursor) { u->screen = UI_CONFIRM; u->confirm_cursor = 0; }   /* Cancel is the default */
            else { u->action = ACT_CONTINUE; u->action_slot = u->active_slot; u->screen = UI_SLOTS_SCREEN; u->launching = 1; }
        }
        break;
    case UI_CONFIRM:
        if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT) u->confirm_cursor = !u->confirm_cursor;
        else if (b == BTN_B) u->screen = UI_SLOT_MENU;
        else if (b == BTN_A) {
            if (u->confirm_cursor) { u->action = ACT_DELETE; u->action_slot = u->active_slot; u->screen = UI_SLOTS_SCREEN; }
            else u->screen = UI_SLOT_MENU;
        }
        break;
    case UI_CREATE: {
        int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
        int *cur = &u->cursor[u->level], *scr = &u->scroll[u->level];
        int rows = n + 1 + save_row(u);                     /* then Save as custom and Next */
        if (b == BTN_UP) move(cur, scr, rows, -1);
        else if (b == BTN_DOWN) move(cur, scr, rows, 1);
        else if (b == BTN_B) u->screen = UI_MODE;
        else if (b == BTN_L || b == BTN_R) tab_step(u, b == BTN_L ? -1 : 1);
        else if (*cur == n && save_row(u)) {
            if (b == BTN_A) {
                u->name_for = 0;
                for (int k = 1, i;; k++) {   /* the first "Custom N" not saved yet */
                    snprintf(u->name, sizeof u->name, "Custom %d", k);
                    for (i = 0; i < u->saved_count && strcasecmp(u->saved[i].name, u->name); i++) {}
                    if (i == u->saved_count) break;
                }
                u->name_len = (int)strlen(u->name);
                u->key_cursor = 0;
                u->screen = UI_NAME;
            }
        } else if (*cur == n + save_row(u)) {
            if (b == BTN_A) { u->run_mode = 2; goto_start(u); }
        } else {
            const UiOptDef *d = &UI_OPTS[idx[*cur]];
            if (d->kind == OPT_SPRITE && b == BTN_A) {
                u->sprite_game = idx[*cur] == UIO_MUSIC ? 3 : idx[*cur] == UIO_PLANDO ? 2 : idx[*cur] == UIO_SAMUS ? 1 : 0;
                u->sprite_cursor = u->value[idx[*cur]];
                u->back = UI_CREATE; u->screen = UI_SPRITES;
            } else if (b == BTN_LEFT) change(u, idx[*cur], -1, 0);
            else if (b == BTN_RIGHT) change(u, idx[*cur], 1, 0);
            else if (b == BTN_A) change(u, idx[*cur], 1, d->kind == OPT_SEED);
        }
        break;
    }
    case UI_SPRITES: {
        int n = u->sprite_count[u->sprite_game];
        if (u->sprite_game >= 2) {   /* plandos and music packages: a plain list */
            if (b == BTN_UP) u->sprite_cursor = (u->sprite_cursor + n - 1) % n;
            else if (b == BTN_DOWN) u->sprite_cursor = (u->sprite_cursor + 1) % n;
        } else {                     /* sprites: a grid, the d-pad moves by cell, L/R by page */
            int step = b == BTN_UP ? -GRID_COLS : b == BTN_DOWN ? GRID_COLS : b == BTN_LEFT ? -1 : b == BTN_RIGHT ? 1
                     : b == BTN_L ? -GRID_COLS * GRID_ROWS : b == BTN_R ? GRID_COLS * GRID_ROWS : 0;
            if (step) {
                int c = u->sprite_cursor + step;
                if (c < 0) c = (b == BTN_UP || b == BTN_L) ? 0 : n - 1;
                if (c > n - 1) c = (b == BTN_DOWN || b == BTN_R) ? n - 1 : 0;
                u->sprite_cursor = c;
            }
        }
        if (b == BTN_B) u->screen = u->back;
        else if (b == BTN_A) {
            u->value[sprite_opt(u->sprite_game)] = u->sprite_cursor;
            u->screen = u->back;
        }
        break;
    }
    case UI_MODE: mode_input(u, b); break;
    case UI_SAVED: saved_input(u, b); break;
    case UI_NAME: name_input(u, b); break;
    case UI_START: start_input(u, b); break;
    case UI_COUNTRY: country_input(u, b); break;
    case UI_ONLINE: online_input(u, b); break;
    case UI_LOADCHOICE: loadchoice_input(u, b); break;
    case UI_LADDER: ladder_input(u, b); break;
    case UI_RULES: rules_input(u, b); break;
    case UI_DAILY: daily_input(u, b); break;
    case UI_HOME: home_input(u, b); break;
    case UI_GAMES: games_input(u, b); break;
    case UI_ROMS: roms_input(u, b); break;
    case UI_RACE_SIZE: case UI_RACE_WAY: case UI_RACE_FORMAT: case UI_RACE_LENGTH: case UI_RACE_PUBLIC:
    case UI_RACE_LIST: case UI_RACE_ROOM: case UI_RACE_READY: race_input(u, b); break;
    case UI_OPTIONS: options_input(u, b); break;
    case UI_CREDITS: credits_input(u, b); break;
    case UI_CLOUD: cloud_input(u, b); break;
    case UI_CLOUD_CONFLICT: cloud_conflict_input(u, b); break;
    case UI_LOGO:
    case UI_BUSY: break;
    }
}

UiCue ui_cue(const SmzUi *a, const SmzUi *z, int b)
{
    if (z->action != ACT_NONE && a->action == ACT_NONE) return z->action == ACT_DELETE || z->action == ACT_LADDER ? UI_CUE_SELECT : UI_CUE_CONFIRM;
    if (z->screen != a->screen) return b == BTN_B || z->screen == a->back || (z->screen == UI_SLOTS_SCREEN && a->screen != UI_HOME) ? UI_CUE_BACK : UI_CUE_OPEN;
    if (z->daily_info != a->daily_info) return z->daily_info ? UI_CUE_OPEN : UI_CUE_BACK;
    if (z->mode_list != a->mode_list) return z->mode_list ? UI_CUE_OPEN : UI_CUE_BACK;
    if (z->world_top != a->world_top) return z->world_top >= 0 ? UI_CUE_OPEN : UI_CUE_BACK;
    if (z->level != a->level || z->ladder_tab != a->ladder_tab || z->world_month != a->world_month || z->ladder_preset != a->ladder_preset) return UI_CUE_TAB;
    if (memcmp(z->value, a->value, sizeof z->value) || z->run_mode != a->run_mode || z->name_len != a->name_len || strcmp(z->name, a->name)) return UI_CUE_SELECT;
    if (z->game_cursor != a->game_cursor || z->slot_cursor != a->slot_cursor || z->menu_cursor != a->menu_cursor || z->confirm_cursor != a->confirm_cursor ||
        memcmp(z->cursor, a->cursor, sizeof z->cursor) || memcmp(z->scroll, a->scroll, sizeof z->scroll) ||
        z->sprite_cursor != a->sprite_cursor || z->mode_cursor != a->mode_cursor || z->saved_cursor != a->saved_cursor ||
        z->saved_col != a->saved_col || z->key_cursor != a->key_cursor || z->country_cursor != a->country_cursor ||
        z->online_cursor != a->online_cursor || z->start_cursor != a->start_cursor || z->start_col != a->start_col ||
        z->lc.cursor != a->lc.cursor || z->daily_sel != a->daily_sel || z->daily_cursor != a->daily_cursor ||
        z->rules_scroll != a->rules_scroll || z->ladder_scroll != a->ladder_scroll || z->slot_scroll != a->slot_scroll || z->world_cursor != a->world_cursor ||
        z->home_cursor != a->home_cursor || z->options_cursor != a->options_cursor || z->credits_scroll != a->credits_scroll || z->race.cursor != a->race.cursor)
        return UI_CUE_MOVE;
    return UI_CUE_NONE;
}

void ui_touch(SmzUi *u, int x, int y)
{
    u->launching = 0;
    for (int i = 0; i < u->hit_count; i++) {
        const UiHit *t = &u->hit[i];
        if (x < t->x || y < t->y || x >= t->x + t->w || y >= t->y + t->h) continue;
        switch (t->kind) {
        case H_SLOT: u->slot_cursor = t->idx; u->daily_sel = 0; ui_input(u, BTN_A); break;
        case H_DAILY: u->daily_sel = 1; ui_input(u, BTN_A); break;
        case H_WORLD: u->daily_sel = 2; ui_input(u, BTN_A); break;
        case H_DAILY_BTN: if (!u->daily_info) u->daily_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_CHEVRON:
            u->slot_scroll += t->idx;
            if (u->slot_scroll > u->slot_count - slots_shown(u)) u->slot_scroll = u->slot_count - slots_shown(u);
            if (u->slot_scroll < 0) u->slot_scroll = 0;
            if (u->slot_cursor < u->slot_scroll) u->slot_cursor = u->slot_scroll;
            if (u->slot_cursor >= u->slot_scroll + slots_shown(u)) u->slot_cursor = u->slot_scroll + slots_shown(u) - 1;
            break;
        case H_LADDER: if (t->idx) u->screen = UI_SLOTS_SCREEN; else ladder_open(u); break;
        case H_LTAB: if (t->idx == 0 && u->ladder_tab == 0) ui_input(u, BTN_A); else { u->ladder_tab = t->idx; u->ladder_scroll = 0; } break;   /* the open first tab: the next table */
        case H_WMONTH: u->world_cursor = -1; if (t->idx != u->world_month) ui_input(u, BTN_A); break;
        case H_WGOAL: u->world_cursor = t->idx; if (u->world_month) ui_input(u, BTN_A); break;
        case H_CONFIRM: u->confirm_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_MENU: u->menu_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_TAB: u->level = t->idx; break;
        case H_ROW: u->cursor[u->level] = t->idx; ui_input(u, BTN_A); break;
        case H_SCROLL: {
            int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
            for (int k = 0; k < 3; k++) move(&u->cursor[u->level], &u->scroll[u->level], n + 1 + save_row(u), t->idx);
            break;
        }
        case H_CREATE: u->run_mode = 2; goto_start(u); break;
        case H_MODE: u->mode_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_SAVED: u->saved_cursor = t->idx; u->saved_col = 0; u->saved_confirm = 0; ui_input(u, BTN_A); break;
        case H_SAVED_DEL: u->saved_cursor = t->idx; u->saved_col = 1; ui_input(u, BTN_A); break;
        case H_KEY: u->key_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_LOADCHOICE: u->lc.cursor = t->idx; ui_input(u, BTN_A); break;
        case H_ONLINE: u->online_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_COUNTRY: u->country_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_START: {   /* 0, 1 sprite rows, 2, 3 their Random chips, 4 Start, 5 player name, 6 country */
            u->start_cursor = t->idx == 4 ? START_ROW : t->idx >= 5 ? 2 : t->idx & 1;
            u->start_col = t->idx == 7 ? 2 : t->idx == 2 || t->idx == 3 || t->idx == 6;
            ui_input(u, BTN_A);
            break;
        }
        case H_SPRITE: u->sprite_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_HOME: u->home_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_ROMS: u->roms_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_GAME: if (t->idx == u->game_cursor) ui_input(u, BTN_A); else u->game_cursor = t->idx; break;   /* a side card turns to the front first */
        case H_OPTION: u->options_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_CREDITS: ui_input(u, t->idx < 0 ? BTN_L : t->idx > 0 ? BTN_R : BTN_B); break;
        case H_CLOUD: if (t->idx < 0) ui_input(u, BTN_B); else { u->cloud.cursor = t->idx; ui_input(u, BTN_A); } break;
        case H_RACE: u->race.cursor = t->idx; ui_input(u, BTN_A); break;
        }
        return;
    }
    if (u->screen == UI_LOGO || u->screen == UI_GAMES || u->screen == UI_BUSY || (u->screen == UI_SLOTS_SCREEN && !u->home_on)) return;
    if (y < 28 && x < 100) ui_input(u, BTN_B);
}

static int pad_x;   /* while a screen laid out for 320 is drawn centred on a wider canvas: how far it is moved right */

static void title(SmzUi *u, const char *t)
{
    rect(u, -pad_x, 0, UI_W + 2 * pad_x, 28, C_PANEL);   /* the bar always spans the canvas */
    if (strncmp(t, "SMZ3", 4)) { text(u, 8 - pad_x, 8, C_ACCENT, 0, t); return; }
    put(u, 2, 6 - pad_x, 3, 22, 22, C_TEXT, 0, "logo");   /* the app logo beside the name */
    text(u, 32 - pad_x, 8, C_ACCENT, 0, t);
}

static void fmt_value(const SmzUi *u, int opt, int v, char *out, size_t n)
{
    const UiOptDef *d = &UI_OPTS[opt];
    switch (d->kind) {
    case OPT_BOOL: snprintf(out, n, "%s", v ? "On" : "Off"); break;
    case OPT_INT: snprintf(out, n, "%d", v); break;
    case OPT_SEED: if (v) snprintf(out, n, "%d", v); else snprintf(out, n, "Random"); break;
    case OPT_ENUM: snprintf(out, n, "%s", v >= d->min && v <= d->max ? d->names[v - d->min] : "?"); break;
    case OPT_SPRITE: { int g = opt == UIO_MUSIC ? 3 : opt == UIO_PLANDO ? 2 : opt == UIO_SAMUS; snprintf(out, n, "%s", v < u->sprite_count[g] ? u->sprites[g][v] : u->sprites[g][0]); break; }
    }
}

void ui_value_text(const SmzUi *u, int opt, char *out, size_t n) { fmt_value(u, opt, u->value[opt], out, n); }


/* ---- the run list rows: a big percent, Link and Samus counters, name and time */

static const uint16_t DIGIT[10] = {   /* 3x5 pixels, one bit per pixel, row-major from the top-left */
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};

static void big_digit(SmzUi *u, int x, int y, int d, int scale, int color)
{
    for (int r = 0; r < 5; r++) {
        int bits = (DIGIT[d] >> (12 - r * 3)) & 7;
        for (int c = 0; c < 3;) {
            if (!(bits & (4 >> c))) { c++; continue; }
            int e = c;
            while (e < 3 && (bits & (4 >> e))) e++;
            rect(u, x + c * scale, y + r * scale, (e - c) * scale, scale, color);
            c = e;
        }
    }
}

static void bar(SmzUi *u, int x, int y, int w, int num, int den, int color)
{
    int fill = den > 0 && num > 0 ? (int)((long long)w * (num > den ? den : num) / den) : 0;
    rect(u, x, y, w, 3, C_BG);
    if (num > 0 && fill < 1) fill = 1;
    if (fill) rect(u, x, y, fill, 3, color);
}

/* a crystal about 9x9 pixels; collected ones are blue (red for the two dark ones) with a highlight, missing ones dark */
static void gem(SmzUi *u, int x, int y, int have, int red)
{
    int c = !have ? C_BG : red ? C_WARN : C_CYAN;
    rect(u, x + 3, y, 3, 1, c);
    rect(u, x + 1, y + 1, 7, 1, c);
    rect(u, x, y + 2, 9, 3, c);
    rect(u, x + 1, y + 5, 7, 1, c);
    rect(u, x + 2, y + 6, 5, 1, c);
    rect(u, x + 3, y + 7, 3, 1, c);
    rect(u, x + 4, y + 8, 1, 1, c);
    if (have) rect(u, x + 2, y + 2, 2, 1, C_TEXT);
}

static void slot_rich(SmzUi *u, int i, int y, int w)
{
    const UiSlot *t = &u->slots[i];
    if (t->online) {
        put(u, 3, 13, y, w - 5, 44, t->online == 2 ? C_VIOLET : t->online == 3 ? C_NAVY : C_TEAL, t->online == 2 ? C_GOLD : t->online == 3 ? C_TEAL : C_NAVY, NULL);
    }
    int total = t->zelda_total + t->metroid_total, done = t->zelda_cleared + t->metroid_cleared;
    int pct = total > 0 ? (int)((long long)done * 100 / total) : 0;
    char s[48];
    int digits = pct >= 100 ? 3 : pct >= 10 ? 2 : 1, x = 22;
    int color = pct >= 100 ? C_GREEN : C_ACCENT;
    if (u->rings) {   /* the percent inside a ring that fills with it */
        put(u, 4, 22, y + 2, 40, 40, color, (uint8_t)pct, NULL);
        snprintf(s, sizeof s, "%d%%", pct);
        text(u, 42, y + 16, C_TEXT, 1, s);
    } else {
        for (int d = 0, div = digits == 3 ? 100 : digits == 2 ? 10 : 1; d < digits; d++, div /= 10) { big_digit(u, x, y + 9, pct / div % 10, 5, color); x += 18; }
        text(u, x, y + 24, C_DIM, 0, "%");
    }
    int fin = t->game == 3, live = fin ? 0 : playing_on(u, t->online);
    snprintf(s, sizeof s, fin || live ? "%.11s" : "%.20s", t->name);
    text(u, 88, y + 2, C_TEXT, 0, s);
    long long sec = t->play_ms / 1000;
    snprintf(s, sizeof s, "%lld:%02lld:%02lld", sec / 3600, sec / 60 % 60, sec % 60);
    text(u, 8 + w - 8, y + 2, fin ? C_GOLD : C_DIM, 2, s);
    if (fin) {   /* the final time, under a gold tag */
        int tx = 8 + w - 8 - (int)strlen(s) * 7 - 70;
        rect(u, tx, y + 1, 62, 11, C_GOLD);
        text(u, tx + 31, y + 2, C_BG, 1, "FINISHED");
    }
    if (live) {   /* others on the same seed now, under a green tag */
        char tag[16];
        snprintf(tag, sizeof tag, "%d PLAYING", live);
        int tw = (int)strlen(tag) * 7 + 6, tx = 8 + w - 8 - (int)strlen(s) * 7 - tw - 8;
        rect(u, tx, y + 1, tw, 11, C_GREEN);
        text(u, tx + tw / 2, y + 2, C_BG, 1, tag);
    }
    /* Link with the crystals the run asks for, Samus with its bosses; what is missing is dark (the pictures are dimmed).
     * All seven crystals go by number; fewer are slots filled by the count, the bosses beaten first. */
    char sk[56];   /* the run's own sprites; the games' standing ones (tools/gen_original_icons.py) for the originals */
    int cn = t->crystals_need ? t->crystals_need : 7, bn = t->bosses_need ? t->bosses_need : 4, got = 0, order[4], k = 0;
    if (!sm_mode(u)) {   /* Super Metroid alone: Samus and her bosses only */
        snprintf(sk, sizeof sk, "link/%s", t->link_sprite[0] ? t->link_sprite : "Original");
        put(u, 2, 88, y + 15, 10, 14, C_TEXT, 0, sk);
        for (int n = 0; n < 7; n++) got += t->crystal_mask >> n & 1;
        for (int n = 1; n <= cn; n++) gem(u, 106 + (n - 1) * 14, y + 17, cn == 7 ? t->crystal_mask >> (n - 1) & 1 : n <= got, cn == 7 && (n == 5 || n == 6));
    }
    if (z3_mode(u)) {   /* A Link to the Past alone: its pendants under the crystals (the run's boss mask holds them) */
        static const char *const PENDANT[3] = {"reward/green_pendant", "reward/blue_pendant", "reward/red_pendant"};
        for (int n = 0; n < 3; n++) put(u, 2, 106 + n * 20, y + 30, 13, 13, t->boss_mask >> n & 1 ? C_TEXT : C_DIM, 0, PENDANT[n]);
    } else {
        snprintf(sk, sizeof sk, "samus/%s", t->samus_sprite[0] ? t->samus_sprite : "Original");
        put(u, 2, 88, y + 29, 10, 14, C_TEXT, 0, sk);
        static const char *const BOSS[4] = {"reward/kraid", "reward/phantoon", "reward/draygon", "reward/ridley"};
        for (int b = 0; b < 4; b++) if (t->boss_mask >> b & 1) order[k++] = b;
        for (int b = 0; b < 4; b++) if (!(t->boss_mask >> b & 1)) order[k++] = b;
        for (int n = 0; n < (bn == 4 ? 4 : bn); n++) {
            int b = bn == 4 ? n : order[n];
            put(u, 2, 106 + n * 20, y + 30, 13, 13, t->boss_mask >> b & 1 ? C_TEXT : C_DIM, 0, BOSS[b]);
        }
    }
    if (t->online) {
        char who[24];
        int wc = t->online == 2 ? C_ACCENT : t->online == 3 ? C_GREEN : C_CYAN, fx = 8 + w - 8 - (u->flags ? 12 : 14);
        snprintf(who, sizeof who, "%.12s", u->player[0] ? u->player : "Online");
        flag(u, fx, y + 15, wc, u->country);
        text(u, u->country[0] ? fx - 4 : 8 + w - 8, y + 14, wc, 2, who);
    }
    if (!sm_mode(u)) bar(u, 214, y + 27, w - 214, t->zelda_cleared, t->zelda_total, C_GREEN);
    if (!z3_mode(u)) bar(u, 214, y + 36, w - 214, t->metroid_cleared, t->metroid_total, C_ACCENT);
}

/* ---- the daily challenge: its row on top of the run list, the token counter and its own screen */

static int64_t now_ms(void) { return (int64_t)time(NULL) * 1000; }

/* The currency, a rupeergy: a rupee drawn as Super Metroid energy, 10x13, blinking between two shades. */
static void rupeergy(SmzUi *u, int x, int y)
{
    static const char *const ROWS[13] = {
        "....kk....", "...krrk...", "..krwhrk..", ".krwhhmrk.", ".krhhmmrk.", ".krhmmmrk.", ".krhmmmrk.",
        ".krhmmsrk.", ".krmmssrk.", ".krmsssrk.", "..krssrk..", "...krrk...", "....kk....",
    };
    int b = (int)(now_ms() / 1000 % 2);   /* the clock here has whole seconds: one shade a second */
    for (int r = 0; r < 13; r++)
        for (int i = 0; ROWS[r][i]; ) {
            char ch = ROWS[r][i];
            int j = i;
            while (ROWS[r][j] == ch) j++;
            int c = ch == 'k' ? C_BG : ch == 'r' ? (b ? C_RG_R2 : C_RG_R) : ch == 'w' ? (b ? C_TEXT : C_RG_W) : ch == 'h' ? (b ? C_RG_H2 : C_RG_H)
                  : ch == 'm' ? (b ? C_RG_M2 : C_RG_M) : ch == 's' ? (b ? C_RG_S2 : C_RG_S) : -1;
            if (c >= 0) rect(u, x + i, y + r, j - i, 1, c);
            i = j;
        }
}

static void token_chip(SmzUi *u, int x, int y)
{
    char s[16];
    rect(u, x, y, 48, 20, C_PANEL);
    rupeergy(u, x + 4, y + 3);
    snprintf(s, sizeof s, "%d", u->daily.tokens);
    text(u, x + 44, y + 4, C_TEXT, 2, s);
}

/* Lines of at most n characters, broken at spaces, from y down; returns the y after the last one. */
static int wrap(SmzUi *u, int x, int y, int color, int n, int line_h, int max_lines, const char *t)
{
    char line[64];
    if (n > 63) n = 63;
    for (int l = 0; *t && l < max_lines; l++, y += line_h) {
        int len = (int)strlen(t);
        if (len > n) { len = n; while (len > 0 && t[len] != ' ') len--; if (!len) len = n; }
        snprintf(line, sizeof line, "%.*s", len, t);
        if (l == max_lines - 1 && (int)strlen(t) > len && len > 3) memcpy(line + len - 3, "...", 4);
        text(u, x, y, color, 0, line);
        t += len;
        while (*t == ' ') t++;
    }
    return y;
}

static void daily_status(const SmzUi *u, char *out, size_t cap)
{
    const SmzDailyInfo *d = &u->daily;
    char t[24];
    switch (d->state) {
    case DAILY_STARTED: smz_daily_span(now_ms() - (d->began_at > d->started_at ? d->began_at : d->started_at), t, sizeof t);   /* as smz_daily_clock_from */ snprintf(out, cap, "Started  -  %s on the clock", t); break;
    case DAILY_DONE: { long long s = d->time_ms / 1000; snprintf(out, cap, "Finished in %lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60); break; }
    case DAILY_LATE: snprintf(out, cap, "Finished after the window closed"); break;
    case DAILY_FAILED: snprintf(out, cap, "Time is up: not finished in the window"); break;
    default: snprintf(out, cap, "%s  -  about %d min", d->difficulty[0] ? d->difficulty : "Open", d->est_min); break;
    }
}

/* A 9x11 pixel padlock, for a challenge that is not open. */
static void padlock(SmzUi *u, int x, int y, int c)
{
    rect(u, x + 2, y, 5, 2, c);
    rect(u, x + 1, y + 1, 2, 4, c);
    rect(u, x + 6, y + 1, 2, 4, c);
    rect(u, x, y + 4, 9, 7, c);
    rect(u, x + 4, y + 6, 1, 3, C_BG);
}

/* The window as a line along the card's foot: what is left of it while open, how close the next one is while closed. */
static void window_line(SmzUi *u, int x, int y, int w, int64_t done, int64_t span, int color)
{
    int full = w - 10;
    int fill = span > 0 ? (int)(full * (done < 0 ? 0 : done > span ? span : done) / span) : 0;
    rect(u, x + 5, y + 42, full, 2, C_BG);
    if (fill > 0) rect(u, x + 5, y + 42, fill, 2, color);
}

/* The run list's Daily challenge card, x to x + w (half the row when the World goals share it). */
static void daily_row(SmzUi *u, int x, int y, int w)
{
    const SmzDailyInfo *d = &u->daily;
    char s[64], t[24];
    int64_t now = now_ms();
    int narrow = w < 280, tx = x + 12;
    rect(u, x, y, w, 44, u->daily_sel == 1 ? C_SEL : C_PANEL);
    if (u->daily_sel != 1) put(u, 3, x + 5, y, w - 5, 44, C_NAVY, C_VIOLET, NULL);
    rect(u, x, y, 5, 44, C_GOLD);
    hit(u, H_DAILY, 0, x, y, w, 44);
    text(u, tx, y + 3, C_ACCENT, 0, narrow ? "DAILY" : "DAILY CHALLENGE");
    if (d->loaded == 0) { text(u, tx, y + 23, C_DIM, 0, "Connecting..."); return; }
    if (d->loaded < 0) { text(u, tx, y + 23, C_DIM, 0, narrow ? "Offline" : "Offline: the challenge needs the server"); return; }
    if (!d->window[0]) {
        smz_daily_span(d->next_opens_at - now, t, sizeof t);
        padlock(u, tx, y + 23, C_TEXT);
        if (w < 170) {   /* half of a 4:3 screen: the countdown takes the second line */
            snprintf(s, sizeof s, "Opens in %s", t);
            text(u, tx + 13, y + 22, C_TEXT, 0, s);
            window_line(u, x, y, w, 4 * 3600000LL - (d->next_opens_at - now), 4 * 3600000LL, C_DIM);
            return;
        }
        text(u, tx + 13, y + 22, C_TEXT, 0, "[CLOSED]");
        snprintf(s, sizeof s, "Opens in %s", t);
        text(u, x + w - 6, y + 3, C_TEXT, 2, s);
        time_t at = (time_t)(d->next_opens_at / 1000);
        struct tm *lt = localtime(&at);
        if (lt) { strftime(t, sizeof t, "%H:%M", lt); snprintf(s, sizeof s, narrow ? "At %s" : "Next one at %s", t); text(u, x + w - 6, y + 22, C_DIM, 2, s); }
        window_line(u, x, y, w, 4 * 3600000LL - (d->next_opens_at - now), 4 * 3600000LL, C_DIM);   /* the gap between windows is 4 hours */
        return;
    }
    int64_t left = d->closes_at - now;
    int hurry = left > 0 && left < 15 * 60000, dot = (now / 1000) % 2 ? C_GREEN : C_TEAL;   /* the dot beats once a second */
    int lx = narrow ? tx : x + 125, ly = narrow ? y + 28 : y + 3;   /* the live mark: on the status line when the card is narrow */
    if (left > 0) {
        rect(u, lx + 1, ly + 3, 4, 6, dot);
        rect(u, lx, ly + 4, 6, 4, dot);
        text(u, lx + 10, ly, C_GREEN, 0, narrow ? "LIVE" : "NOW LIVE");
    } else text(u, lx, ly, C_WARN, 0, "[CLOSED]");
    smz_daily_span(left, t, sizeof t);
    snprintf(s, sizeof s, left > 0 ? "%s left" : "", t);
    text(u, x + w - 6, y + 3, hurry ? C_WARN : C_TEXT, 2, s);
    const char *icon;
    wrap(u, tx, y + 16, C_TEXT, (w - 18) / 7, 13, 1, d->goal);
    if (smz_goal_boss(d->goal, NULL, &icon, NULL) && icon[0] && tx + (int)strlen(d->goal) * 7 + 18 <= x + w) {
        snprintf(s, sizeof s, "goal/%s", icon);
        put(u, 2, tx + (int)strlen(d->goal) * 7 + 3, y + 14, 13, 13, C_TEXT, 0, s);
    }
    daily_status(u, s, sizeof s);
    int sx = narrow ? tx + (left > 0 ? 44 : 70) : tx;
    int room = (x + w - 6 - sx) / 7;   /* cut to the card */
    if ((int)strlen(s) > room && d->state != DAILY_STARTED && d->state != DAILY_DONE && d->state != DAILY_LATE && d->state != DAILY_FAILED) {
        snprintf(s, sizeof s, "%s, ~%d min", d->difficulty[0] ? d->difficulty : "Open", d->est_min);   /* the estimate stays whole on a narrow card */
        if ((int)strlen(s) > room) snprintf(s, sizeof s, "~%d min", d->est_min);
    }
    if (room >= 0 && room < (int)sizeof s) s[room] = 0;
    text(u, sx, y + 28, d->state == DAILY_DONE ? C_GREEN : d->state == DAILY_FAILED || d->state == DAILY_LATE ? C_WARN : C_DIM, 0, s);
    if (!narrow && left > 0 && d->players_now >= 0 && sx + (int)strlen(s) * 7 + 80 < x + w) {
        snprintf(s, sizeof s, "%d playing", d->players_now);
        text(u, x + w - 6, y + 28, C_GREEN, 2, s);
    }
    window_line(u, x, y, w, left, d->closes_at - d->opens_at, hurry ? C_WARN : C_GREEN);
}

/* The run list's World goals card: an icon and a bar per goal of the month. */
static void world_list_row(SmzUi *u, int x, int y, int w)
{
    char s[48];
    int tx = x + 12;
    rect(u, x, y, w, 44, u->daily_sel == 2 ? C_SEL : C_PANEL);
    rect(u, x, y, 5, 44, C_CYAN);
    hit(u, H_WORLD, 0, x, y, w, 44);
    int tiny = w < 170;   /* half of a 4:3 screen: a bar under each icon instead of the percentages */
    text(u, tx, y + 3, C_CYAN, 0, tiny ? "WORLD" : "WORLD GOALS");
    if (u->world.loaded == 0) { text(u, tx, y + 23, C_DIM, 0, "Connecting..."); return; }
    if (u->world.loaded < 0) { text(u, tx, y + 23, C_DIM, 0, w < 280 ? "Offline" : "Offline: the goals need the server"); return; }
    int64_t left = u->world.ends_at - now_ms();
    int days = left > 0 ? (int)((left + 86399999) / 86400000) : 0;
    snprintf(s, sizeof s, left > 0 ? "%d day%s" : "Ended", days, days == 1 ? "" : "s");
    if (left > 0 && w >= 280) strcat(s, " left");
    text(u, x + w - 6, y + 3, C_TEXT, 2, s);
    int col = (w - 16) / 4;
    for (int i = 0; i < u->world.n && i < 4; i++) {
        const typeof(u->world.g[0]) *g = &u->world.g[i];
        int gx = tx + i * col, met = g->target > 0 && g->total >= g->target, bw = col - 22;
        rect(u, gx, y + 20, 16, 16, C_BG);
        if (g->sprite[0]) { snprintf(s, sizeof s, "goal/%s", g->sprite); put(u, 2, gx, y + 20, 16, 16, C_TEXT, 0, s); }
        if (tiny) {
            int fill = g->target > 0 ? (int)(16 * (g->total > g->target ? g->target : g->total) / g->target) : 0;
            rect(u, gx, y + 38, 16, 3, C_BG);
            if (fill) rect(u, gx, y + 38, fill, 3, met ? C_ACCENT : C_GREEN);
            continue;
        }
        rect(u, gx + 19, y + 22, bw, 4, C_BG);
        int fill = g->target > 0 ? (int)(bw * (g->total > g->target ? g->target : g->total) / g->target) : 0;
        if (fill) rect(u, gx + 19, y + 22, fill, 4, met ? C_ACCENT : C_GREEN);
        snprintf(s, sizeof s, "%d%%", g->target > 0 ? (int)(100 * (g->total > g->target ? g->target : g->total) / g->target) : 0);
        text(u, gx + 19, y + 28, met ? C_ACCENT : C_DIM, 0, met ? "Met" : s);
    }
}

/* The rules and the details, behind the Info button. */
static void daily_info_page(SmzUi *u)
{
    const SmzDailyInfo *d = &u->daily;
    char s[160];
    title(u, "Daily challenge  -  Info");
    int y = 34;
    if (d->loaded == 1 && d->window[0]) {
        s[0] = 0;
        for (int i = 0; i < d->n_start; i++) { size_t n = strlen(s); snprintf(s + n, sizeof s - n, "%s%s", i ? ", " : "", d->start[i]); }
        text(u, 16, y, C_DIM, 0, "You start with");
        y = wrap(u, 16, y + 14, C_TEXT, 40, 13, 2, d->n_start ? s : "nothing extra") + 8;
    }
    /* NULL leaves a gap between the groups */
    static const char *const RULES[] = {
        "Opens at 06:00, 14:00 and 22:00 UTC", "for four hours. Same seed for all.", NULL,
        "One try. The clock starts in game", "and keeps running if you close it.", NULL,
        "Fast forward and save states are off.", "The first hint arrives at 10 minutes.", NULL,
        "Finish in time to earn rupeergies.",
    };
    for (int i = 0; i < (int)(sizeof RULES / sizeof RULES[0]); i++) {
        if (!RULES[i]) { y += 6; continue; }
        text(u, 16, y, C_TEXT, 0, RULES[i]);
        y += 13;
    }
    rect(u, 8, 198, UI_W - 16, 24, C_SEL);
    text(u, UI_W / 2, 204, C_TEXT, 1, "Back");
    hit(u, H_DAILY_BTN, 2, 8, 198, UI_W - 16, 24);
}

static void daily_screen(SmzUi *u)
{
    if (u->daily_info) { daily_info_page(u); return; }
    const SmzDailyInfo *d = &u->daily;
    char s[96], t[24];
    snprintf(s, sizeof s, "Daily challenge  %s", d->window);
    title(u, s);
    if (d->loaded == 1) token_chip(u, UI_W - 56, 4);
    int y = 40;
    if (d->loaded != 1 || !d->window[0]) {
        text(u, 16, y + 8, C_TEXT, 0, d->loaded != 1 ? "The challenge needs the server." : "No challenge is open now.");
        if (d->loaded == 1) { smz_daily_span(d->next_opens_at - now_ms(), t, sizeof t); snprintf(s, sizeof s, "The next one opens in %s.", t); text(u, 16, y + 26, C_DIM, 0, s); }
    } else {
        rect(u, 8, y, UI_W - 16, 60, C_PANEL);
        text(u, 16, y + 4, C_DIM, 0, "Goal");
        const char *icon, *place;
        if (smz_goal_boss(d->goal, NULL, &icon, &place)) {   /* "Defeat Arrghus [icon]", then where it waits */
            text(u, 16, y + 22, C_ACCENT, 0, d->goal);
            if (icon[0]) { snprintf(s, sizeof s, "goal/%s", icon); put(u, 2, 16 + (int)strlen(d->goal) * 7 + 4, y + 16, 22, 22, C_TEXT, 0, s); }
            snprintf(s, sizeof s, "in %s", place);
            text(u, 16, y + 40, C_TEXT, 0, s);
        } else wrap(u, 16, y + 20, C_ACCENT, 40, 14, 2, d->goal);
        y += 72;
        int64_t left = d->closes_at - now_ms();
        smz_daily_span(left, t, sizeof t);
        snprintf(s, sizeof s, left > 0 ? "%s left" : "Window closed", t);
        text(u, 16, y, left > 0 && left < 15 * 60000 ? C_WARN : C_TEXT, 0, s);
        daily_status(u, s, sizeof s);
        text(u, 16, y + 16, d->state == DAILY_DONE ? C_GREEN : d->state == DAILY_FAILED || d->state == DAILY_LATE ? C_WARN : C_DIM, 0, s);
        if (d->state == DAILY_OPEN && left > 0 && left < 3600000) text(u, 16, y + 34, C_WARN, 0, "Under 1 hour left");
        if (d->players_now >= 0 && left > 0) {   /* who is on it right now, beside the time left */
            snprintf(s, sizeof s, "%d playing now, %d finished", d->players_now, d->players_done);
            text(u, UI_W - 16, y, C_GREEN, 2, s);
        }
    }
    int go = daily_button(u);
    if (go == 1)   /* the skins the run is made with */
        for (int k = 0; k < 2; k++) {
            int w = (UI_W - 22) / 2, x = 8 + k * (w + 6), y = 164;
            rect(u, x, y, w, 28, u->daily_cursor == 3 + k ? C_SEL : C_PANEL);
            hit(u, H_DAILY_BTN, 3 + k, x, y, w, 28);
            text(u, x + 6, y + 4, C_DIM, 0, k ? "Your Samus" : "Your Link");
            char v[44];
            ui_value_text(u, sprite_opt(k), v, sizeof v);
            text(u, x + 6, y + 15, C_TEXT, 0, v);
        }
    /* Start (when there is something to start), Info, Back */
    int by = 198, n = go ? 3 : 2, w = (UI_W - 16 - (n - 1) * 6) / n, x = 8;
    static const char *const NAME[3] = {NULL, "Info", "Back"};
    for (int id = go ? 0 : 1; id < 3; id++, x += w + 6) {
        rect(u, x, by, w, 24, u->daily_cursor == id ? C_SEL : C_PANEL);
        text(u, x + w / 2, by + 6, id == 0 ? C_ACCENT : C_TEXT, 1, id == 0 ? (go == 1 ? "Start" : "Continue") : NAME[id]);
        hit(u, H_DAILY_BTN, id, x, by, w, 24);
    }
}

/* A row of the race screens: a name on the left, a note on the right. */
static void race_row_h(SmzUi *u, int r, int y, int h, const char *name, const char *note, int color)
{
    rect(u, 8, y, UI_W - 16, h, r == u->race.cursor ? C_SEL : C_PANEL);
    hit(u, H_RACE, r, 8, y, UI_W - 16, h);
    text(u, 18, y + (h - 12) / 2, color, 0, name);
    if (note && note[0]) text(u, UI_W - 18, y + (h - 12) / 2, C_DIM, 2, note);
}
static void race_row(SmzUi *u, int r, int y, const char *name, const char *note, int color) { race_row_h(u, r, y, 28, name, note, color); }

static void race_draw(SmzUi *u)
{
    char buf[64], note[48];
    SmzRace *r = &u->race.room;
    switch (u->screen) {
    case UI_RACE_SIZE:
        title(u, u->race.format == RACE_DRAFT ? "Online draft" : "Championship");
        text(u, 10, 34, C_DIM, 0, u->race.format == RACE_DRAFT ? "One goal, the same start for everyone. 5 to 15 minutes." : "A normal run from the start. First to the goal wins.");
    {
        int p = paused_row(u);
        if (p) {
            char when[24];
            smz_race_when(u->race.paused_resume, when, sizeof when);
            snprintf(buf, sizeof buf, "Paused race %s", u->race.paused_code);
            snprintf(note, sizeof note, "goes on %s", when);
            race_row(u, 0, 52, buf, note, C_GOLD);
        }
        race_row(u, p, 52 + 34 * p, "New race", "2 to 8 players", C_TEXT);
    }
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_WAY:
        title(u, u->race.format == RACE_DRAFT ? "Online draft" : "Championship");
        text(u, 10, 144, C_DIM, 0, "2 to 8 players: start once the ones you want are in");
        race_row(u, 0, 36, "Create a room", "", C_TEXT);
        race_row(u, 1, 70, "Join with a code", "", C_TEXT);
        race_row(u, 2, 104, "Find a race", "public rooms", C_TEXT);
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_FORMAT:
        title(u, "Championship");
        race_row(u, 0, 36, "Full game", sm_mode(u) ? "Mother Brain" : z3_mode(u) ? "Ganon" : "Ganon and Mother Brain", C_TEXT);
        race_row(u, 1, 70, "Partial game", "pick a length", C_TEXT);
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_LENGTH:
        title(u, "Partial game");
        race_row(u, 0, 36, "Sprint", z3_mode(u) ? "Fast Ganon, 2 crystals" : "2 crystals, 1 boss", C_TEXT);
        race_row(u, 1, 70, "Half", z3_mode(u) ? "Fast Ganon, 4 crystals" : "4 crystals, 2 bosses", C_TEXT);
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_PUBLIC:
        title(u, "New room");
        race_row(u, 0, 36, "Private room", "only with its code", C_TEXT);
        race_row(u, 1, 70, "Public room", "listed in Find a race", C_TEXT);
        legend(u, "A Make it  B Back");
        break;
    case UI_RACE_LIST:
        title(u, "Find a race");
        if (!u->race.room_count)
            text(u, UI_W / 2, 100, C_DIM, 1, u->race.rooms_loaded == 0 ? "Looking..." : u->race.rooms_loaded < 0 ? "No connection" : "No open rooms right now");
        for (int k = 0, first = u->race.cursor < 6 ? 0 : u->race.cursor - 5; k < 6 && first + k < u->race.room_count; k++) {   /* 6 rows that follow the cursor */
            const SmzRaceRoom *w = &u->race.rooms[first + k];
            snprintf(buf, sizeof buf, "%s  %s", w->creator, smz_race_format_name(w->format));
            snprintf(note, sizeof note, "%d of %d", w->players, w->size);
            race_row(u, first + k, 36 + k * 32, buf, note, C_TEXT);
        }
        legend(u, u->race.room_count ? "A Join  B Back" : "B Back");
        break;
    case UI_RACE_ROOM: {
        int creator = !strcmp(r->you, r->creator);
        title(u, "Room");
        if (!strcmp(r->format, "draft")) snprintf(buf, sizeof buf, "Online draft");
        else snprintf(buf, sizeof buf, "Championship, %s", smz_race_format_name(r->format));
        text(u, 10, 34, C_DIM, 0, buf);
        snprintf(buf, sizeof buf, "Code  %s", r->code);
        text(u, UI_W - 10, 34, C_ACCENT, 2, buf);
        snprintf(buf, sizeof buf, "%d of %d players here%s", r->n, r->size, r->is_public ? ", public" : "");
        text(u, 10, 48, C_TEXT, 0, buf);
        int step = r->n > 6 ? 16 : 18, end = 62 + r->n * step;   /* 7 or 8 players: tighter rows, so the notice and legend stay clear */
        for (int i = 0; i < r->n; i++) {
            int y = 62 + i * step, mine = !strcmp(r->p[i].pid, r->you);
            rect(u, 8, y, UI_W - 16, step - 2, i == u->race.cursor && creator ? C_SEL : C_PANEL);
            hit(u, H_RACE, i, 8, y, UI_W - 16, step - 2);
            text(u, 14, y + step - 14, mine ? C_ACCENT : C_TEXT, 0, r->p[i].name);
            text(u, UI_W - 14, y + step - 14, C_DIM, 2, !strcmp(r->p[i].pid, r->creator) ? "creator" : creator && !mine ? "A remove" : "");
        }
        if (creator) race_row_h(u, r->n, end + (r->n > 6 ? 0 : 4), r->n > 6 ? 16 : 28, "Start race", r->n < 2 ? "needs 2 players" : "skins and the ready room", r->n < 2 ? C_DIM : C_TEXT);
        else text(u, UI_W / 2, end + 6, C_DIM, 1, "Give this code to the other players");
        legend(u, creator ? "A Choose  B Leave" : "B Leave");
        break;
    }
    case UI_RACE_READY: {
        int ready = 0;
        for (int i = 0; i < r->n; i++) ready += r->p[i].ready;
        snprintf(buf, sizeof buf, "Ready room  -  %d of %d ready", ready, r->n);
        title(u, buf);
        for (int i = 0; i < r->n; i++) {
            int y = 34 + i * 16;
            char nm[16];
            snprintf(nm, sizeof nm, "%.12s", r->p[i].name);   /* clear of "waiting" */
            text(u, 12, y, !strcmp(r->p[i].pid, r->you) ? C_ACCENT : C_TEXT, 0, nm);
            text(u, 150, y, r->p[i].ready ? C_GREEN : C_DIM, 2, r->p[i].ready ? "READY" : "waiting");
        }
        const SmzRacePlayer *me = smz_race_me(r);
        int done = me && me->ready, x = 160, w = UI_W - 168;
        for (int k = 0; k < 2; k++) {
            int y = 34 + k * 34;
            rect(u, x, y, w, 28, k == u->race.cursor && !done ? C_SEL : C_PANEL);
            hit(u, H_RACE, k, x, y, w, 28);
            text(u, x + 6, y + 4, C_DIM, 0, k ? "Your Samus" : "Your Link");
            char v[44];
            ui_value_text(u, sprite_opt(k), v, sizeof v);
            text(u, x + 6, y + 15, C_TEXT, 0, v);
        }
        rect(u, x, 104, w, 28, u->race.cursor == 2 && !done ? C_SEL : C_PANEL);
        hit(u, H_RACE, 2, x, 104, w, 28);
        text(u, x + w / 2, 113, done ? C_GREEN : u->race.getting_ready ? C_DIM : C_ACCENT, 1, done ? "Ready" : u->race.getting_ready ? "Getting ready..." : "I'm ready");
        text(u, x, 140, C_DIM, 0, "No save states, fast");
        text(u, x, 152, C_DIM, 0, "forward or rewind.");
        legend(u, done ? "B Leave" : "A Choose  B Leave");
        break;
    }
    default: break;
    }
    if (u->notice[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->notice);
}

void ui_draw(SmzUi *u)
{
    char buf[64], val[44];
    int listed = u->screen;
    if (u->launching && u->screen == UI_SLOTS_SCREEN) u->screen = UI_SLOT_MENU;
    u->cmd_count = 0;
    u->hit_count = 0;
    rect(u, 0, 0, UI_W, UI_H, C_BG);
    if (u->screen != UI_LOADCHOICE) u->orb_on = 0;
    /* the screens with a fixed 320 layout (the sprite grid, the start and load choices, the keyboard) are drawn at 320 and centred */
    int full = UI_W, full_h = UI_H, c0 = u->cmd_count, h0 = u->hit_count, fixed = 0;   /* fixed: the screen keeps its 240 layout */
    if (u->screen == UI_SPRITES || u->screen == UI_START || u->screen == UI_LOADCHOICE || u->screen == UI_NAME) { pad_x = (full - 320) / 2; smz_ui_w = 320; }
    /* on a tall canvas only the lists grow; the other screens keep their 240 layout at the top, with the legend at the bottom */
    int tall = u->screen == UI_SLOTS_SCREEN || u->screen == UI_CREATE || u->screen == UI_SPRITES || u->screen == UI_SAVED
            || u->screen == UI_RULES || u->screen == UI_COUNTRY || u->screen == UI_LADDER;
    if (!tall && full_h > 240) { fixed = 1; smz_ui_h = 240; }
    switch (u->screen) {
    case UI_SLOTS_SCREEN: {
        int more = u->slot_count > slots_shown(u);
        /* with the token counter beside Ladder the title is shorter, so it fits in the hosts' wider fonts too */
        const char *g = GAME_SHORT[u->game_mode];
        if (u->daily_sel == 1) snprintf(buf, sizeof buf, "%s  -  Special run", g);
        else if (u->daily_sel == 2) snprintf(buf, sizeof buf, "%s  -  World goals", g);
        else if (more) snprintf(buf, sizeof buf, DAILY_ON(u) ? "%s  -  Runs  (%d/%d)" : "%s  -  Select a run  (%d/%d)", g, u->slot_cursor + 1, u->slot_count);
        else snprintf(buf, sizeof buf, DAILY_ON(u) ? "%s  -  Runs" : "%s  -  Select a run", g);
        title(u, buf);
        if (LADDER_ON(u)) {
            rect(u, UI_W - 70, 4, 64, 20, C_SEL);
            text(u, UI_W - 38, 8, C_TEXT, 1, "Ladder >");
            hit(u, H_LADDER, 0, UI_W - 70, 4, 64, 20);
        }
        if (DAILY_ON(u) && u->daily.loaded == 1) token_chip(u, UI_W - 70 - 52, 4);
        int w = UI_W - 16 - (more ? 26 : 0), top = 32, shown = slots_shown(u);
        if (world_row(u)) { int half = (w - 6) / 2; daily_row(u, 8, top, half); world_list_row(u, 8 + half + 6, top, w - half - 6); top += 47; }
        else if (DAILY_ON(u)) { daily_row(u, 8, top, w); top += 47; }
        for (int r = 0; r < shown && u->slot_scroll + r < u->slot_count; r++) {
            int i = u->slot_scroll + r, y = top + r * 47, used = u->slots[i].used;
            rect(u, 8, y, w, 44, i == u->slot_cursor && !u->daily_sel ? C_SEL : C_PANEL);
            if (used) rect(u, 8, y, 5, 44, C_ACCENT);   /* a run with data has an orange bar on the left */
            hit(u, H_SLOT, i, 8, y, w, 44);
            if (used && u->slots[i].stats) slot_rich(u, i, y, w);
            else if (used) {
                snprintf(buf, sizeof buf, "Slot %d  %s", i + 1, u->slots[i].name);
                text(u, 20, y + 7, C_TEXT, 0, buf);
                text(u, 20, y + 25, C_DIM, 0, u->slots[i].summary);
            } else {   /* an empty slot: a plus where the percent goes and an invitation */
                rect(u, 8, y, 5, 44, i == u->slot_cursor && !u->daily_sel ? C_ACCENT : C_DIM);
                rect(u, 24, y + 20, 26, 5, C_DIM);
                rect(u, 34, y + 10, 5, 25, C_DIM);
                text(u, 88, y + 16, C_ACCENT, 0, "New run");
            }
        }
        if (more) {
            int x = UI_W - 26;
            if (u->slot_scroll > 0) { rect(u, x, top, 20, 44, C_PANEL); text(u, x + 10, top + 14, C_ACCENT, 1, "^"); hit(u, H_CHEVRON, -1, x, top, 20, 44); }
            int by = top + (shown - 1) * 47;
            if (u->slot_scroll + shown < u->slot_count) { rect(u, x, by, 20, 44, C_PANEL); text(u, x + 10, by + 14, C_ACCENT, 1, "v"); hit(u, H_CHEVRON, 1, x, by, 20, 44); }
        }
        legend(u, LADDER_ON(u) ? u->home_on ? "A Open  Select Ladder  B Back" : "A Open  Select Ladder" : u->home_on ? "A Open  B Back" : "A Open");
        if (u->version[0]) text(u, UI_W - 6, UI_H - 12, C_DIM, 2, u->version);
        break;
    }
    case UI_CONFIRM:
        snprintf(buf, sizeof buf, "Delete Slot %d?", u->active_slot + 1);
        title(u, buf);
        text(u, 16, 40, C_TEXT, 0, u->slots[u->active_slot].name);
        text(u, 16, 58, C_DIM, 0, u->slots[u->active_slot].summary);
        text(u, 16, 84, C_WARN, 0, "This erases the run and its save.");
        hit(u, H_CONFIRM, 0, 8, 110, UI_W - 16, 30);
        hit(u, H_CONFIRM, 1, 8, 146, UI_W - 16, 30);
        rect(u, 8, 110, UI_W - 16, 30, u->confirm_cursor == 0 ? C_SEL : C_PANEL);
        text(u, 16, 118, C_TEXT, 0, "Cancel");
        rect(u, 8, 146, UI_W - 16, 30, u->confirm_cursor == 1 ? C_SEL : C_PANEL);
        text(u, 16, 154, C_WARN, 0, "Yes, delete it");
        legend(u, "B Back");
        break;
    case UI_SLOT_MENU:
        snprintf(buf, sizeof buf, "Slot %d  %s", u->active_slot + 1, u->slots[u->active_slot].name);
        title(u, buf);
        text(u, 16, 40, C_DIM, 0, u->slots[u->active_slot].summary);
        if (playing_on(u, u->slots[u->active_slot].online)) {
            int n = playing_on(u, u->slots[u->active_slot].online);
            snprintf(buf, sizeof buf, "%d player%s on this seed right now", n, n == 1 ? "" : "s");
            text(u, 16, 54, C_GREEN, 0, buf);
        }
        hit(u, H_MENU, 0, 8, 70, UI_W - 16, 30);
        hit(u, H_MENU, 1, 8, 106, UI_W - 16, 30);
        rect(u, 8, 70, UI_W - 16, 30, u->menu_cursor == 0 ? C_SEL : C_PANEL);
        text(u, 16, 78, C_TEXT, 0, "Continue");
        rect(u, 8, 106, UI_W - 16, 30, u->menu_cursor == 1 ? C_SEL : C_PANEL);
        text(u, 16, 114, C_WARN, 0, "Delete");
        legend(u, "B Back");
        break;
    case UI_CREATE: {
        int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
        int cur = u->cursor[u->level], scr = u->scroll[u->level];
        title(u, "");   /* six tabs take the whole bar */
        {
            static const char *const TABS[LEVEL_COUNT] = { "Quick", "Run", "Zelda", "Metroid", "Play", "Items" };
            for (int t = 0, k = 0; t < LEVEL_COUNT; t++) {
                if (single_game(u) && !tab_shown(u, t)) continue;
                int x = 4 + k++ * 52;
                hit(u, H_TAB, t, x, 4, 50, 20);
                rect(u, x, 4, 50, 20, u->level == t ? C_SEL : C_BG);
                text(u, x + 25, 8, C_TEXT, 1, TABS[t]);
            }
            if (u->level == LEVEL_QUICK) text(u, 6, 30, C_DIM, 0, "The options most players change");
        }
        for (int r = 0; r < ROWS_SHOWN && scr + r <= n + save_row(u); r++) {
            int row = scr + r, y = LIST_Y + r * ROW_H;
            rect(u, 4, y, UI_W - 40, ROW_H - 2, row == cur ? C_SEL : C_PANEL);
            hit(u, H_ROW, row, 4, y, UI_W - 40, ROW_H - 2);
            if (row == n && save_row(u)) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Save as custom..."); continue; }
            if (row == n + save_row(u)) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Next: sprites and start"); continue; }
            ui_value_text(u, idx[row], val, sizeof val);
            text(u, 10, y + 4, C_TEXT, 0, UI_OPTS[idx[row]].label);
            int pic = (idx[row] == UIO_LINK || idx[row] == UIO_SAMUS) && u->value[idx[row]] > 0 && u->value[idx[row]] < u->sprite_count[idx[row] == UIO_SAMUS];
            int fit = (UI_W - 46 - (150 + ROW_H + 2)) / 7;   /* chars of about 7 px between the preview and the edge */
            if (pic && (int)strlen(val) > fit) { val[fit > 2 ? fit - 2 : 0] = 0; strcat(val, ".."); }
            text(u, UI_W - 46, y + 4, C_ACCENT, 2, val);
            if (pic) {
                /* the chosen sprite beside its row, not only in the picker */
                char key[64];
                int g = idx[row] == UIO_SAMUS;
                snprintf(key, sizeof key, "%s/%s", g ? "samus" : "link", u->sprites[g][u->value[idx[row]]]);
                put(u, 2, 150, y, ROW_H - 2, ROW_H - 2, C_TEXT, 0, key);
            }
        }
        rect(u, UI_W - 32, LIST_Y, 28, 66, C_PANEL);
        text(u, UI_W - 18, LIST_Y + 24, C_ACCENT, 1, "^");
        hit(u, H_SCROLL, -1, UI_W - 32, LIST_Y, 28, 66);
        rect(u, UI_W - 32, LIST_Y + 74, 28, 66, C_PANEL);
        text(u, UI_W - 18, LIST_Y + 98, C_ACCENT, 1, "v");
        hit(u, H_SCROLL, 1, UI_W - 32, LIST_Y + 74, 28, 66);
        rect(u, UI_W - 92, UI_H - 54, 88, 32, C_SEL);
        text(u, UI_W - 48, UI_H - 44, C_ACCENT, 1, "Next");
        hit(u, H_CREATE, 0, UI_W - 92, UI_H - 54, 88, 32);
        {
            const char *tip = u->notice[0] ? u->notice : cur < n ? UI_OPTS[idx[cur]].tip : cur == n && save_row(u) ? "Saves these options under a name." : "Choose the sprites, then start.";
            char line[44];
            size_t len = strlen(tip), cut = len;
            if (len > 28) { cut = 28; while (cut > 0 && tip[cut] != 0x20) cut--; if (!cut) cut = 28; }
            snprintf(line, sizeof line, "%.*s", (int)cut, tip);
            text(u, 6, UI_H - 52, C_DIM, 0, line);
            if (cut < len) {
                const char *rest = tip + cut + (tip[cut] == ' ');
                size_t rl = strlen(rest), c2 = rl > 28 ? 28 : rl;
                if (rl > 28) { while (c2 > 0 && rest[c2] != ' ') c2--; if (!c2) c2 = 28; }
                char l2[44];
                snprintf(l2, sizeof l2, "%.*s", (int)c2, rest);
                text(u, 6, UI_H - 40, C_DIM, 0, l2);
                if (c2 < rl) text(u, 6, UI_H - 28, C_DIM, 0, rest + c2 + (rest[c2] == ' '));
            }
        }
        legend(u, "L R Tabs  B Back");
        break;
    }
    case UI_SPRITES: {
        int g = u->sprite_game, n = u->sprite_count[g];
        title(u, g == 3 ? "Music package" : g == 2 ? "Plando" : g ? "Samus sprite" : "Link sprite");
        if (g >= 2) {
            int first = u->sprite_cursor - ROWS_SHOWN / 2;
            if (first > n - ROWS_SHOWN) first = n - ROWS_SHOWN;
            if (first < 0) first = 0;
            for (int r = 0; r < ROWS_SHOWN && first + r < n; r++) {
                int y = LIST_Y + r * ROW_H;
                rect(u, 4, y, UI_W - 8, ROW_H - 2, first + r == u->sprite_cursor ? C_SEL : C_PANEL);
                hit(u, H_SPRITE, first + r, 4, y, UI_W - 8, ROW_H - 2);
                text(u, 10, y + 4, C_TEXT, 0, u->sprites[g][first + r]);
            }
            legend(u, "A Pick  B Back");
            break;
        }
        int rows = (n + GRID_COLS - 1) / GRID_COLS, row = u->sprite_cursor / GRID_COLS;
        int first = row - GRID_ROWS / 2;
        if (first > rows - GRID_ROWS) first = rows - GRID_ROWS;
        if (first < 0) first = 0;
        int chosen = u->value[g ? UIO_SAMUS : UIO_LINK];
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                int i = (first + r) * GRID_COLS + c;
                if (i >= n) break;
                int x = GRID_X + c * GRID_CELL, y = GRID_Y + r * GRID_CELL;
                rect(u, x, y, GRID_CELL - 2, GRID_CELL - 2, i == u->sprite_cursor ? C_SEL : C_PANEL);
                if (i == chosen) rect(u, x, y, GRID_CELL - 2, 3, C_ACCENT);
                hit(u, H_SPRITE, i, x, y, GRID_CELL - 2, GRID_CELL - 2);
                {
                    char key[64];
                    snprintf(key, sizeof key, "%s/%s", g ? "samus" : "link", u->sprites[g][i]);
                    put(u, 2, x + 2, y + 4, GRID_CELL - 6, GRID_CELL - 8, C_TEXT, 0, key);
                }
            }
        }
        char info[48];
        snprintf(info, sizeof info, "%d/%d  %s", u->sprite_cursor + 1, n, u->sprites[g][u->sprite_cursor]);
        text(u, 6, UI_H - 18, C_TEXT, 0, info);
        legend(u, "A Pick  L R Page  B Back");
        break;
    }
    case UI_LOGO:
        put(u, 2, UI_W / 2 - 32, 56, 64, 64, C_TEXT, 0, "logo");
        text(u, UI_W / 2, 132, C_ACCENT, 1, "SMZ3");
        text(u, UI_W / 2, 150, C_DIM, 1, "A Link to the Past + Super Metroid");
        if (u->version[0]) text(u, UI_W / 2, 166, C_DIM, 1, u->version);
        break;
    case UI_ROMS: {
        static const char *const NAME[2] = {"Super Metroid", "A Link to the Past"}, *const NEEDS[2] = {"Super Metroid (JU)", "ALttP, Japan v1.0"};
        title(u, "Your ROMs");
        text(u, UI_W / 2, 36, C_DIM, 1, "SMZ3 builds its games from your own ROM files");
        int all = 1;
        for (int i = 0; i < 2; i++) {
            int y = 54 + i * 42, ok = u->game_state[i ? SMZ_MODE_Z3 : SMZ_MODE_SM] != UI_GAME_NO_ROM;
            all &= ok;
            rect(u, 24, y, UI_W - 48, 36, C_PANEL);
            rect(u, 24, y, 5, 36, ok ? C_GREEN : C_WARN);
            text(u, 38, y + 6, C_TEXT, 0, NAME[i]);
            text(u, 38, y + 20, C_DIM, 0, NEEDS[i]);
            text(u, UI_W - 32, y + 13, ok ? C_GREEN : C_WARN, 2, ok ? "Found" : "Missing");
        }
        text(u, UI_W / 2, 144, all ? C_GREEN : C_DIM, 1, all ? "All good! Start playing!" : u->roms_hint);
        int bw = (UI_W - 60) / 2;
        for (int i = 0; i < 2; i++) {
            int x = 24 + i * (bw + 12);
            int play = i && all;   /* both ROMs in: Play lights up green */
            rect(u, x, 164, bw, 28, play ? C_GREEN : i == u->roms_cursor ? C_SEL : C_PANEL);
            if (play && i == u->roms_cursor) { rect(u, x, 164, bw, 2, C_TEXT); rect(u, x, 190, bw, 2, C_TEXT); }   /* its focus, as green hides C_SEL */
            hit(u, H_ROMS, i, x, 164, bw, 28);
            text(u, x + bw / 2, 172, play ? C_BG : C_TEXT, 1, i ? (all ? "Play" : "Later") : "Add ROMs");
        }
        if (u->notice[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->notice);
        legend(u, all ? "A Choose  Left Right Move" : "A Choose  Left Right Move  B Later");
        break;
    }
    case UI_GAMES: {
        /* three cards on a ring seen from the front: the far ones smaller and higher, drawn first so the near one covers them */
        int order[SMZ_MODE_COUNT], n = 0;
        float depth[SMZ_MODE_COUNT];
        for (int g = 0; g < SMZ_MODE_COUNT; g++) {
            depth[g] = cosf((g * 1000 - u->game_spin) * 6.2831853f / 3000);
            int at = n++;
            while (at > 0 && depth[order[at - 1]] > depth[g]) { order[at] = order[at - 1]; at--; }
            order[at] = g;
        }
        for (int i = 0; i < n; i++) {
            int g = order[i], dim = u->game_state[g] != UI_GAME_READY;
            float a = (g * 1000 - u->game_spin) * 6.2831853f / 3000, k = 0.55f + 0.45f * (depth[g] + 0.5f) / 1.5f;
            int w = (int)(150 * k), h = (int)(170 * k), x = UI_W / 2 + (int)(sinf(a) * (UI_W * 0.34f)) - w / 2, y = 34 + (int)((1 - k) * 60);
            int front = g == u->game_cursor && depth[g] > 0.9f;
            rect(u, x - 2, y - 2, w + 4, h + 4, front ? C_ACCENT : C_PANEL);
            rect(u, x, y, w, h, front ? C_SEL : C_BG);
            put(u, 2, x + w / 4, y + h / 10, w / 2, h / 2, dim ? C_DIM : C_TEXT, 0, GAME_PICS[g]);
            hit(u, H_GAME, g, x, y, w, h);
            if (k < 0.8f) continue;   /* only the cards turned to the front carry their names */
            text(u, x + w / 2, y + h * 2 / 3, dim ? C_DIM : C_ACCENT, 1, GAME_NAMES[g]);
            text(u, x + w / 2, y + h * 2 / 3 + 16, C_DIM, 1, u->game_state[g] == UI_GAME_SOON ? "Coming soon" : u->game_state[g] == UI_GAME_NO_ROM ? "ROM missing" : GAME_LINES[g]);
        }
        if (u->notice[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->notice);
        else if (u->update[0]) { snprintf(buf, sizeof buf, "%s is out: Options > Update", u->update); text(u, UI_W / 2, UI_H - 30, C_GOLD, 1, buf); }
        legend(u, "Left Right Turn  A Choose");
        break;
    }
    case UI_HOME:
        put(u, 2, UI_W / 2 - 16, 10, 32, 32, C_TEXT, 0, GAME_PICS[u->game_mode]);
        text(u, UI_W / 2, 48, C_ACCENT, 1, GAME_NAMES[u->game_mode]);
        for (int r = 0, n = home_rows(u), pitch = n > 4 ? 28 : 34, h = n > 4 ? 24 : 28; r < n; r++) {   /* five rows: closer, above the notice line */
            int y = (n > 4 ? 64 : 70) + r * pitch, off = (r == 1 || r == 2) && (!u->race.on || !online_game(u));   /* the online rows, without a connection */
            rect(u, 24, y, UI_W - 48, h, r == u->home_cursor ? C_SEL : C_PANEL);
            rect(u, 24, y, 5, h, off ? C_DIM : C_ACCENT);
            hit(u, H_HOME, r, 24, y, UI_W - 48, h);
            text(u, 38, y + h / 2 - 6, off ? C_DIM : C_TEXT, 0, HOME_NAMES[r]);
            if (off) text(u, UI_W - 32, y + h / 2 - 6, C_DIM, 2, "Offline");
        }
        if (u->notice[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->notice);
        else if (u->update[0]) { snprintf(buf, sizeof buf, "%s is out: Options > Update", u->update); text(u, UI_W / 2, UI_H - 30, C_GOLD, 1, buf); }
        legend(u, "A Choose  B Games");
        break;
    case UI_CREDITS: {
        title(u, "Credits");
        int shown = CREDITS_SHOWN, top = u->credits_scroll, more = CREDITS_COUNT > shown;
        for (int i = 0; i < shown && top + i < CREDITS_COUNT; i++) {
            const char *l = CREDITS_LINES[top + i];
            text(u, 14, 34 + i * CREDITS_PITCH, *l == '#' ? C_GOLD : C_TEXT, 0, l + (*l == '#'));
        }
        if (more) {   /* page arrows on the right; a tap anywhere else goes back */
            int h = (UI_H - 64) / 2;
            rect(u, UI_W - 24, 32, 22, h, C_PANEL);
            text(u, UI_W - 13, 32 + h / 2 - 6, top > 0 ? C_ACCENT : C_DIM, 1, "^");
            hit(u, H_CREDITS, -1, UI_W - 24, 32, 22, h);
            rect(u, UI_W - 24, 40 + h, 22, h, C_PANEL);
            text(u, UI_W - 13, 40 + h + h / 2 - 6, top + shown < CREDITS_COUNT ? C_ACCENT : C_DIM, 1, "v");
            hit(u, H_CREDITS, 1, UI_W - 24, 40 + h, 22, h);
        }
        hit(u, H_CREDITS, 0, 0, 28, UI_W, UI_H - 28);
        legend(u, more ? "Up Down Scroll  L R Page  B Back" : "B Back");
        break;
    }
    case UI_CLOUD: {
        static const char *const NAMES[] = {"", "Sign in to RomM", "Cancel", "Sync now", "Link another device", "Enter a link code", "Sign out", "", "", "RomM address"};
        static const char *const DESCS[] = {"", "with a code, on its page", "", "runs, saves and states", "show a code for it", "from another device", "", "", "", ""};
        title(u, "Cloud sync");
        if (u->cloud.state == UI_CLOUD_IN) {
            snprintf(buf, sizeof buf, "Signed in as %s", u->cloud.user);
            text(u, UI_W / 2, 36, C_TEXT, 1, buf);
            text(u, UI_W / 2, 52, C_DIM, 1, u->cloud.server);
        } else if (u->cloud.state == UI_CLOUD_WAITING) {
            text(u, UI_W / 2, 32, C_DIM, 1, "On your phone or PC, open");
            text(u, UI_W / 2, 46, C_CYAN, 1, u->cloud.url);
            text(u, UI_W / 2, 60, C_DIM, 1, "sign in and enter this code:");
        } else text(u, UI_W / 2, 44, C_DIM, 1, "Your runs, saves and states on your own RomM");
        if (u->cloud.state == UI_CLOUD_WAITING) { rect(u, UI_W / 2 - 70, 76, 140, 30, C_PANEL); text(u, UI_W / 2, 84, C_GOLD, 1, u->cloud.code); }
        int row[6], n = cloud_rows(u, row), y0 = u->cloud.state == UI_CLOUD_WAITING ? 116 : 72;
        for (int r = 0; r < n; r++) {
            int y = y0 + r * 30;
            rect(u, 8, y, UI_W - 16, 26, r == u->cloud.cursor ? C_SEL : C_PANEL);
            hit(u, H_CLOUD, r, 8, y, UI_W - 16, 26);
            text(u, 18, y + 5, C_TEXT, 0, NAMES[row[r]]);
            const char *d = DESCS[row[r]];
            if (row[r] == CLOUD_ADDRESS) d = u->cloud.server[0] ? u->cloud.server : "not set";
            text(u, UI_W - 18, y + 5, C_DIM, 2, d);
        }
        if (u->cloud.msg[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->cloud.msg);
        legend(u, "A Choose  B Back");
        break;
    }
    case UI_CLOUD_CONFLICT: {
        title(u, "Which one to keep?");
        text(u, UI_W / 2, 36, C_TEXT, 1, "This run changed on this device");
        snprintf(buf, sizeof buf, "and on %.30s", u->cloud.dev);
        text(u, UI_W / 2, 52, C_TEXT, 1, buf);
        if (u->cloud.when[0]) text(u, UI_W / 2, 68, C_DIM, 1, u->cloud.when);
        for (int r = 0; r < 2; r++) {
            int y = 96 + r * 34;
            rect(u, 8, y, UI_W - 16, 28, r == u->cloud.cursor ? C_SEL : C_PANEL);
            hit(u, H_CLOUD, r, 8, y, UI_W - 16, 28);
            if (r) snprintf(buf, sizeof buf, "Keep %.28s's", u->cloud.dev);
            text(u, 18, y + 6, C_TEXT, 0, r ? buf : "Keep this device's");
        }
        text(u, UI_W / 2, 170, C_DIM, 1, "The other one is replaced");
        hit(u, H_CLOUD, -1, 0, UI_H - 24, UI_W, 24);
        legend(u, "A Keep  B Not now");
        break;
    }
    case UI_OPTIONS: {
        title(u, "Options");
        int kind[OPTION_KINDS], n = option_rows(u, kind), pitch = n > 7 ? (UI_H - 54) / n : n > 6 ? 28 : n > 5 ? 30 : 34;
        for (int r = 0; r < n; r++) {
            int y = 36 + r * pitch, bit = OPTION_ROWS[kind[r]].bit;
            rect(u, 8, y, UI_W - 16, pitch - 6, r == u->options_cursor ? C_SEL : C_PANEL);
            hit(u, H_OPTION, r, 8, y, UI_W - 16, pitch - 6);
            text(u, 18, y + (pitch - 18) / 2, bit == -1 ? C_GOLD : C_TEXT, 0, OPTION_ROWS[kind[r]].name);
            const char *d = OPTION_ROWS[kind[r]].desc;
            if (!bit && u->player[0]) { snprintf(buf, sizeof buf, "%s %s", u->player, u->country); d = buf; }
            if (bit == -1) { snprintf(buf, sizeof buf, u->update_install ? "install %s" : "%s is out", u->update); d = buf; }
            if (bit == UI_OPT_SOUND) d = u->menu_music ? "on" : "off";
            if (bit == UI_OPT_SEED) d = u->seed_code ? "shown" : "hidden";
            if (bit == -3) d = u->cloud.state == UI_CLOUD_IN ? u->cloud.user : "off";
            text(u, UI_W - 18, y + (pitch - 18) / 2, C_DIM, 2, d);
        }
        if (u->notice[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->notice);
        legend(u, "A Choose  B Back");
        break;
    }
    case UI_RACE_SIZE: case UI_RACE_WAY: case UI_RACE_FORMAT: case UI_RACE_LENGTH: case UI_RACE_PUBLIC:
    case UI_RACE_LIST: case UI_RACE_ROOM: case UI_RACE_READY: race_draw(u); break;
    case UI_MODE: {
        snprintf(buf, sizeof buf, "%s  -  Slot %d", !u->mode_list ? "New run" : sm_mode(u) ? "Map Rando" : "Race presets", u->active_slot + 1);
        title(u, buf);
        const UiLadderPreset *pre;
        ui_ladder_presets(u->game_mode, &pre);
        int rows = mode_rows(u), pitch = rows > 5 ? 31 : rows > 4 ? 40 : 50, rh = pitch - (rows > 5 ? 3 : 6);
        int gap = rows > 5 ? 12 : 14, dy = (rh - gap - 14) / 2;   /* the name and its line centred in the row */
        for (int r = 0; r < rows; r++) {
            int y = 34 + r * pitch, k = mode_kind(u, r);
            const char *name = k >= MODE_PRESET ? pre[k - MODE_PRESET].name : k == 0 ? chill(u)->name : k == 1 ? original(u)->name : k == 2 ? "Custom" : k == 3 ? "Saved customs"
                : k == 4 ? "Monthly run" : k == 6 ? (u->mode_list ? "Custom Map Rando" : "Map Rando") : k == 7 ? "Race presets" : weekly(u)->name;
            const char *wgoal = sm_mode(u) ? "Mother Brain" : z3_mode(u) ? "Fast Ganon, 2 crystals" : "2 crystals, 1 boss";
            char desc[48];
            if (k >= MODE_PRESET) snprintf(desc, sizeof desc, "%s", pre[k - MODE_PRESET].desc);
            else if (k == 7) snprintf(desc, sizeof desc, "Casual Boots, Standard, AD Keys: own tables");
            else if (k == 6 && u->mode_list) snprintf(desc, sizeof desc, "Pick the preset and skill yourself");
            else if (k == 0) snprintf(desc, sizeof desc, "%s", chill(u)->desc);
            else if (k == 1) snprintf(desc, sizeof desc, "%s", original(u)->desc);
            else if (k == 2) snprintf(desc, sizeof desc, "Pick every option yourself");
            else if (k == 3) snprintf(desc, sizeof desc, "%d saved", u->saved_count);
            else if (k == 6) snprintf(desc, sizeof desc, "%s", smz_sm_maprando_state() == SMZ_MR_OFFLINE ? "Needs an internet connection" : "A rebuilt map, as SM races play it");
            else if (k == 5 && u->weekly_days <= 0) snprintf(desc, sizeof desc, "%s: ends today, %s", u->weekly_label, wgoal);
            else if (k == 5) snprintf(desc, sizeof desc, "%s: %dd left, %s", u->weekly_label, u->weekly_days, wgoal);
            else if (u->monthly_days <= 0) snprintf(desc, sizeof desc, "%s: ends today", u->monthly_label);
            else snprintf(desc, sizeof desc, "%s: %d day%s left", u->monthly_label, u->monthly_days, u->monthly_days == 1 ? "" : "s");
            rect(u, 8, y, UI_W - 16, rh, r == u->mode_cursor ? C_SEL : C_PANEL);
            rect(u, 8, y, 5, rh, k == 4 ? C_GREEN : k == 5 ? C_CYAN : k >= MODE_PRESET ? C_GOLD : C_ACCENT);
            hit(u, H_MODE, r, 8, y, UI_W - 16, rh);
            text(u, 20, y + dy, C_TEXT, 0, name);
            text(u, 20, y + dy + gap, C_DIM, 0, desc);
            int live = k == 4 ? playing_on(u, 2) : k == 5 ? playing_on(u, 3) : 0;
            if (live) { snprintf(buf, sizeof buf, "Playing: %d", live); text(u, UI_W - 18, y + dy, C_GREEN, 2, buf); }
        }
        legend(u, "A Choose  B Back");
        break;
    }
    case UI_SAVED: {
        snprintf(buf, sizeof buf, "Saved customs  (%d/%d)", u->saved_cursor + 1, u->saved_count);
        title(u, buf);
        for (int r = 0; r < SAVED_ROWS && u->saved_scroll + r < u->saved_count; r++) {
            int i = u->saved_scroll + r, y = 34 + r * 38;
            char sum[48];
            spec_summary(u, u->saved[i].spec, sum, 33);
            rect(u, 6, y, UI_W - 70, 34, i == u->saved_cursor && !u->saved_col ? C_SEL : C_PANEL);
            hit(u, H_SAVED, i, 6, y, UI_W - 70, 34);
            rect(u, UI_W - 60, y, 54, 34, i == u->saved_cursor && u->saved_col ? C_SEL : C_PANEL);
            hit(u, H_SAVED_DEL, i, UI_W - 60, y, 54, 34);
            if (i == u->saved_cursor && u->saved_confirm) {
                text(u, 12, y + 4, C_WARN, 0, "Delete this custom?");
                text(u, 12, y + 19, C_DIM, 0, "A yes   B no");
                text(u, UI_W - 33, y + 11, C_WARN, 1, "Sure?");
            } else {
                text(u, 12, y + 4, C_TEXT, 0, u->saved[i].name);
                text(u, 12, y + 19, C_DIM, 0, sum);
                text(u, UI_W - 33, y + 11, C_WARN, 1, "Delete");
            }
        }
        legend(u, "A Choose  Right Delete  B Back");
        break;
    }
    case UI_NAME: {
        title(u, u->name_for == 5 ? "RomM address" : u->name_for == 4 ? "Link code" : u->name_for == 2 ? "Room code" : u->name_for ? "Player name" : "Save custom as");
        char field[72];
        snprintf(field, sizeof field, "%s_", u->name_len > 44 ? u->name + u->name_len - 44 : u->name);   /* a long address: its end */
        rect(u, 10, 32, UI_W - 20, 24, C_PANEL);
        text(u, 16, 38, C_TEXT, 0, field);
        for (int k = 0; k < KEY_COUNT; k++) {
            int x = 10 + (k % KEY_COLS) * 30, y = 62 + (k / KEY_COLS) * 30;
            char ch[2] = {KEY_CHARS[k], 0};
            rect(u, x, y, 28, 28, k == u->key_cursor ? C_SEL : C_PANEL);
            hit(u, H_KEY, k, x, y, 28, 28);
            text(u, x + 14, y + 8, C_TEXT, 1, ch);
        }
        const char *const CTRL[KEY_CTRL] = {"Space", "Delete", u->name_for == 2 || u->name_for == 4 ? "Join" : "Save", "Cancel"};
        for (int k = 0; k < KEY_CTRL; k++) {
            int x = 10 + k * 75;
            rect(u, x, 186, 73, 26, KEY_COUNT + k == u->key_cursor ? C_SEL : C_PANEL);
            hit(u, H_KEY, KEY_COUNT + k, x, 186, 73, 26);
            text(u, x + 36, 192, k == 2 ? C_ACCENT : C_TEXT, 1, CTRL[k]);
        }
        legend(u, "A Type  L Delete  R Space  B Back");
        break;
    }
    case UI_RULES: {
        int idx[UI_MAX_OPTS], n = changed_opts(u, idx);
        char val[48], head[48];
        title(u, "Run rules");
        const UiLadderPreset *pre;
        int pk = ui_run_preset(u), np = ui_ladder_presets(u->game_mode, &pre);
        if (pk > 0 && pk <= np) snprintf(head, sizeof head, "%s table", pre[pk - 1].name);
        else if (!n) snprintf(head, sizeof head, "Original run - no changes");
        else snprintf(head, sizeof head, "Original run + %d change%s", n, n == 1 ? "" : "s");
        text(u, UI_W - 8, 8, C_DIM, 2, head);
        for (int r = 0; r < ROWS_SHOWN && u->rules_scroll + r < n; r++) {
            int o = idx[u->rules_scroll + r], y = LIST_Y + r * ROW_H;
            rect(u, 4, y, UI_W - 8, ROW_H - 2, C_PANEL);
            ui_value_text(u, o, val, sizeof val);
            text(u, 10, y + 4, C_TEXT, 0, UI_OPTS[o].label);
            text(u, UI_W - 10, y + 4, C_ACCENT, 2, val);
        }
        if (!n) text(u, UI_W / 2, 110, C_DIM, 1, "The default randomizer rules");
        legend(u, n > ROWS_SHOWN ? "Up Down Scroll  B Back" : "B Back");
        break;
    }
    case UI_START: {
        snprintf(buf, sizeof buf, "%s  -  %s", u->start_label, maprando_run(u) ? "Start" : "Sprites");
        title(u, buf);
        int rh = u->player_on ? 46 : 56, step = rh + 6, g0 = sm_mode(u), g1 = z3_mode(u) ? 1 : maprando_run(u) ? 1 : 2;   /* one game alone: its own row */
        for (int g = g0; g < g1; g++) {
            int y = 34 + (g - g0) * step, v = u->value[g ? UIO_SAMUS : UIO_LINK];
            if (v >= u->sprite_count[g]) v = 0;
            rect(u, 8, y, 218, rh, u->start_cursor == g && !u->start_col ? C_SEL : C_PANEL);
            hit(u, H_START, g, 8, y, 218, rh);
            rect(u, 232, y, 80, rh, u->start_cursor == g && u->start_col ? C_SEL : C_PANEL);
            hit(u, H_START, 2 + g, 232, y, 80, rh);
            {
                char key[64];
                snprintf(key, sizeof key, "%s/%s", g ? "samus" : "link", u->sprites[g][v]);
                put(u, 2, 14, y + 4, rh - 8, rh - 8, C_TEXT, 0, key);
            }
            text(u, 72, y + rh / 2 - 16, C_DIM, 0, g ? "Samus" : "Link");
            text(u, 72, y + rh / 2 + 4, C_ACCENT, 0, u->sprites[g][v]);
            text(u, 272, y + rh / 2 - 6, C_ACCENT, 1, "Random");
        }
        if (u->player_on && online_game(u)) {
            int y = 34 + 2 * step, sel = u->start_cursor == 2;
            if (!ui_run_ladder(u)) {   /* changed rules: the time stays on this device */
                rect(u, 8, y, UI_W - 16, 36, C_PANEL);
                text(u, 16, y + 4, C_DIM, 0, "Online ladder");
                text(u, 16, y + 19, C_DIM, 0, "Local only: these rules don't go online");
            } else if (u->online_mode == 1) {
                rect(u, 8, y, 162, 36, sel && u->start_col == 0 ? C_SEL : C_PANEL);
                hit(u, H_START, 5, 8, y, 162, 36);
                rect(u, 176, y, 62, 36, sel && u->start_col == 1 ? C_SEL : C_PANEL);
                hit(u, H_START, 6, 176, y, 62, 36);
                rect(u, 244, y, 68, 36, sel && u->start_col == 2 ? C_SEL : C_PANEL);
                hit(u, H_START, 7, 244, y, 68, 36);
                text(u, 16, y + 4, C_DIM, 0, "Online player");
                text(u, 16, y + 19, u->player[0] ? C_ACCENT : C_DIM, 0, u->player[0] ? u->player : "Set your name");
                if (u->country[0] && u->flags) { snprintf(val, sizeof val, "flag/%s", u->country); put(u, 2, 195, y + 9, 24, 18, C_ACCENT, 0, val); }
                else text(u, 207, y + 12, u->country[0] ? C_ACCENT : C_DIM, 1, u->country[0] ? u->country : "Flag");
                text(u, 278, y + 12, C_GREEN, 1, "On");
            } else {
                rect(u, 8, y, UI_W - 16, 36, sel ? C_SEL : C_PANEL);
                hit(u, H_START, 7, 8, y, UI_W - 16, 36);
                text(u, 16, y + 4, C_DIM, 0, "Online ladder");
                text(u, 16, y + 19, u->online_mode == 2 ? C_DIM : C_ACCENT, 0, u->online_mode == 2 ? "Off" : "Ask before each run");
            }
        }
        int sy = u->player_on ? 188 : 164, sh = u->player_on ? 34 : 44;
        rect(u, 8, sy, UI_W - 16, sh, u->start_cursor == START_ROW ? C_SEL : C_PANEL);
        hit(u, H_START, 4, 8, sy, UI_W - 16, sh);
        text(u, UI_W / 2, sy + sh / 2 - 6, C_ACCENT, 1, "Start");
        legend(u, "A Choose  Right Random  L Rules  B Back");
        break;
    }
    case UI_LOADCHOICE: {
        snprintf(buf, sizeof buf, "Slot %d  -  Which to load?", u->active_slot + 1);
        title(u, buf);
        int64_t smax = u->lc.state_ms > u->lc.save_ms ? u->lc.state_ms : u->lc.save_ms;
        int x0 = 44, x1 = 252, y = 66;
        int xs = x0, xt = x1;
        if (smax > 0) {
            xs = x0 + (int)((x1 - x0) * u->lc.save_ms / smax * 85 / 100);
            xt = x0 + (int)((x1 - x0) * u->lc.state_ms / smax * 85 / 100) + (x1 - x0) * 15 / 100;
            if (xt - xs < 84) xt = xs + 84;
            if (xt > x1) { xt = x1; xs = x1 - 84; }
        }
        rect(u, 20, y - 1, 280, 3, C_DIM);                           /* the time line */
        rect(u, 292, y - 4, 8, 9, C_DIM);
        rect(u, xs, y - 4, xt - xs, 9, C_PANEL);                      /* the stretch the save state is ahead */
        rect(u, xs - 1, y - 12, 3, 25, C_ACCENT);
        rect(u, xs - 5, y - 5, 11, 11, C_ACCENT);
        rect(u, xt - 1, y - 12, 3, 25, C_GREEN);
        rect(u, xt - 6, y - 3, 12, 7, C_GREEN);
        rect(u, xt - 3, y - 6, 6, 13, C_GREEN);
        {   /* the orb on the line marks what A loads; drawn once a frame, it eases toward the choice */
            float target = (float)(u->lc.cursor ? xt : xs);
            if (!u->orb_on) { u->orb_on = 1; u->orb_x = target; u->orb_k = -0.9f; }   /* below 0: waiting out the screen change */
            u->orb_x += (target - u->orb_x) * 0.16f;
            if (u->orb_k < 1) u->orb_k += u->orb_k < 0 ? 0.03f : (1.05f - u->orb_k) * 0.12f;
            float k = u->orb_k < 0 ? 0 : u->orb_k > 1 ? 1 : u->orb_k;
            int d = (int)(16 * k + 0.5f), ox = (int)(u->orb_x + 0.5f);
            if (d > 1) {
                if (u->orbs) put(u, 5, ox - 12, y - 12, 24, 24, u->lc.cursor ? C_GREEN : C_ACCENT, (uint8_t)(k * 100), NULL);
                else rect(u, ox - d / 4, y - d / 4, d / 2, d / 2, C_CYAN);
            }
        }
        text(u, xs, y - 26, C_ACCENT, 1, "GAME SAVE");
        text(u, xt, y - 26, C_GREEN, 1, "SAVE STATE");
        char t[32];
        long long a = u->lc.save_ms / 1000, d = u->lc.state_ms / 1000;
        snprintf(t, sizeof t, "%lld:%02lld:%02lld", a / 3600, a / 60 % 60, a % 60); text(u, xs, y + 16, C_TEXT, 1, t);
        snprintf(t, sizeof t, "%lld:%02lld:%02lld", d / 3600, d / 60 % 60, d % 60); text(u, xt, y + 16, C_TEXT, 1, t);
        long long gap = (u->lc.state_ms - u->lc.save_ms) / 1000;
        snprintf(t, sizeof t, "+%lld:%02lld further on", gap / 60, gap % 60);
        if (gap > 0) text(u, (xs + xt) / 2, y + 32, C_CYAN, 1, t);   /* a finished run's clock is frozen: no gap to tell */
        for (int k = 0; k < 2; k++) {
            int x = 8 + k * 156, cy = 112, z = k ? u->lc.state_z : u->lc.save_z, m = k ? u->lc.state_m : u->lc.save_m;
            int total = u->lc.zt + u->lc.mt, pct = total > 0 ? (z + m) * 100 / total : 0;
            rect(u, x, cy, 148, 98, k == u->lc.cursor ? C_SEL : C_PANEL);
            rect(u, x, cy, 148, 3, k ? C_GREEN : C_ACCENT);
            hit(u, H_LOADCHOICE, k, x, cy, 148, 98);
            text(u, x + 8, cy + 8, k ? C_GREEN : C_ACCENT, 0, k ? "Save state" : "Game save");
            text(u, x + 8, cy + 22, C_DIM, 0, k ? "Where you left off" : "Where it last saved");
            snprintf(t, sizeof t, "%d%%", pct); text(u, x + 8, cy + 40, C_TEXT, 2 - 2, t);
            rect(u, x + 8, cy + 56, 132, 4, C_BG);
            if (total > 0) rect(u, x + 8, cy + 56, 132 * (z + m) / total, 4, C_GREEN);
            snprintf(t, sizeof t, "Zelda %d/%d", z, u->lc.zt);
            text(u, x + 8, cy + 66, C_DIM, 0, t);
            snprintf(t, sizeof t, "Metroid %d/%d", m, u->lc.mt);
            text(u, x + 8, cy + 80, C_DIM, 0, t);
            if (k && u->lc.state_z > u->lc.save_z) { snprintf(t, sizeof t, "+%d", u->lc.state_z - u->lc.save_z); text(u, x + 140, cy + 66, C_CYAN, 2, t); }
            if (k && u->lc.state_m > u->lc.save_m) { snprintf(t, sizeof t, "+%d", u->lc.state_m - u->lc.save_m); text(u, x + 140, cy + 80, C_CYAN, 2, t); }
        }
        text(u, UI_W / 2, 213, C_DIM, 1, "The other is replaced when you quit");
        legend(u, "Left Right Choose  A Load  B Back");
        break;
    }
    case UI_ONLINE: {
        title(u, "Online ladder");
        text(u, 12, 32, C_TEXT, 0, "Send this run's time to the");
        text(u, 12, 48, C_TEXT, 0, "online ladder?");
        text(u, 12, 64, C_DIM, 0, "No save states while it counts.");
        static const char *const ROW[3] = {"Yes", "Not now", "No, don't ask again"};
        static const char *const SUB[3] = {"Pick a name and flag", "Offline now, ask next run", "Change it in Online ladder"};
        char as[48];
        snprintf(as, sizeof as, u->player[0] ? "Send it as %s" : u->country[0] ? "Pick a name" : "Pick a name and flag", u->player);
        for (int r = 0; r < 3; r++) {
            int y = 86 + r * 46;
            rect(u, 8, y, UI_W - 16, 42, r == u->online_cursor ? C_SEL : C_PANEL);
            rect(u, 8, y, 5, 42, r == 0 ? C_GREEN : C_ACCENT);
            hit(u, H_ONLINE, r, 8, y, UI_W - 16, 42);
            text(u, 22, y + 6, C_TEXT, 0, ROW[r]);
            text(u, 22, y + 24, C_DIM, 0, r == 0 ? as : SUB[r]);
        }
        legend(u, "A Choose  B Back");
        break;
    }
    case UI_COUNTRY: {
        title(u, "Flag");
        for (int r = 0; r < COUNTRY_ROWS && u->country_scroll + r < COUNTRY_COUNT; r++) {
            int i = u->country_scroll + r, y = 32 + r * 25;
            rect(u, 6, y, UI_W - 12, 23, i == u->country_cursor ? C_SEL : C_PANEL);
            hit(u, H_COUNTRY, i, 6, y, UI_W - 12, 23);
            if (COUNTRIES[i].code[0] && u->flags) flag(u, 14, y + 7, C_ACCENT, COUNTRIES[i].code);
            else text(u, 14, y + 6, C_ACCENT, 0, COUNTRIES[i].code[0] ? COUNTRIES[i].code : "--");
            text(u, 54, y + 6, C_TEXT, 0, COUNTRIES[i].name);
        }
        legend(u, "A Choose  L R Page  B Back");
        break;
    }
    case UI_LADDER: {
        const UiLadderPreset *pre;
        int presets = ui_ladder_presets(u->game_mode, &pre), lp = u->ladder_preset > 0 && u->ladder_preset <= presets ? u->ladder_preset : 0;
        const char *const TABS[4] = {lp ? pre[lp - 1].tab : "Original", "Monthly", "Weekly", "World"};
        const typeof(u->ladder[0]) *l = &u->ladder[u->ladder_tab < 3 ? u->ladder_tab : 0];
        if (u->ladder_tab == 3) snprintf(buf, sizeof buf, "World goals  %s", month_name(u->world_month && u->world_prev.n ? u->world_prev.month : u->world.month));
        else if (l->label[0] && u->ladder_tab) snprintf(buf, sizeof buf, "%s  %s", sm_mode(u) ? "Super Metroid ladder" : z3_mode(u) ? "ALttP ladder" : "Online ladder", l->label);
        else if (lp) snprintf(buf, sizeof buf, "%s", pre[lp - 1].name);
        else snprintf(buf, sizeof buf, "%s", sm_mode(u) ? "Super Metroid ladder" : z3_mode(u) ? "ALttP ladder" : "Online ladder");
        title(u, buf);
        rect(u, UI_W - 84, 4, 78, 20, C_SEL);
        text(u, UI_W - 45, 8, C_TEXT, 1, "< Save list");
        hit(u, H_LADDER, 1, UI_W - 84, 4, 78, 20);
        if (u->ladder_tab == 3 && u->world_top >= 0) { world_top10(u); legend(u, "B Back"); break; }
        for (int t = 0, tw = (UI_W - 12) / 4; t < 4; t++) {
            int x = 6 + t * tw;
            rect(u, x, 32, tw - 2, 22, t == u->ladder_tab ? C_SEL : C_PANEL);
            hit(u, H_LTAB, t, x, 32, tw - 2, 22);
            text(u, x + tw / 2 - 1, 38, t == u->ladder_tab ? C_TEXT : C_DIM, 1, TABS[t]);
        }
        if (u->ladder_tab == 3) {
            world_tab(u);
            legend(u, u->world_cursor < 0 && u->world_prev.n ? (u->world_month ? "A This month  Left Right Tab" : "A Last month  Left Right Tab")
                      : u->world_month ? "A Top 10  Left Right Tab" : "Up Down Goal  Left Right Tab");
            break;
        }
        if (l->loaded == 0) text(u, UI_W / 2, 120, C_DIM, 1, "Loading ...");
        else if (l->loaded < 0) text(u, UI_W / 2, 120, C_WARN, 1, "No connection");
        else if (!l->n) text(u, UI_W / 2, 120, C_DIM, 1, "No finished runs yet");
        for (int r = 0; r < UI_LADDER_SHOWN && u->ladder_scroll + r < l->n; r++) {
            int i = u->ladder_scroll + r, y = 58 + r * 20;
            rect(u, 6, y, UI_W - 12, 18, i % 2 ? C_BG : C_PANEL);
            snprintf(buf, sizeof buf, "%d", i + 1);
            text(u, 12, y + 4, i == 0 ? C_GOLD : C_DIM, 0, buf);
            text(u, 40, y + 4, C_TEXT, 0, l->row[i].name);
            flag(u, 196 + (UI_W - 320) / 2, y + 5, C_ACCENT, l->row[i].cc);
            int64_t s = l->row[i].ms / 1000;
            snprintf(buf, sizeof buf, "%d:%02d:%02d.%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60), (int)(l->row[i].ms % 1000 / 10));
            text(u, UI_W - 10, y + 4, C_TEXT, 2, buf);
        }
        if (u->ladder_tab == 0 && presets) {   /* which of the game's tables: Original or a race preset, A for the next */
            for (int k = 0, cx = 6 + (UI_W - 12) / 8 - 1; k <= presets; k++) rect(u, cx - presets * 4 + k * 8 - 2, 52, 5, 2, k == lp ? C_GOLD : C_DIM);
            legend(u, "A Next table  Left Right Tab  Up Down Scroll");
        } else legend(u, "Left Right Tab  Up Down Scroll  Select Save list");
        break;
    }
    case UI_DAILY: daily_screen(u); break;
    case UI_BUSY:
        title(u, u->action == ACT_RACE_CREATE || u->action == ACT_RACE_JOIN || !strncmp(u->status, "Making the room", 15) || !strncmp(u->status, "Joining", 7) ? "Race" : "Generating");
        text(u, UI_W / 2, UI_H / 2 - 6, C_TEXT, 1, u->status);
        break;
    }
    if (pad_x || fixed) {
        for (int i = c0; i < u->cmd_count; i++) { u->cmd[i].x += pad_x; if (fixed && u->cmd[i].y >= 240 - 16) u->cmd[i].y += full_h - 240; }   /* the legend goes to the bottom */
        for (int i = h0; i < u->hit_count; i++) u->hit[i].x += pad_x;
        pad_x = 0;
    }
    smz_ui_w = full;
    smz_ui_h = full_h;
    if (u->staging && u->screen != UI_LOGO) {   /* a test server: never mistaken for the real ladder */
        rect(u, UI_W / 2 - 29, 0, 58, 9, C_WARN);   /* top middle: the top right holds Ladder and the rupees, the bottom left the legends */
        text(u, UI_W / 2, 0, C_BG, 1, "STAGING");
    }
    u->screen = listed;
}
