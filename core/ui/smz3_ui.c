#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include "smz3_ui.h"

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

void ui_init(SmzUi *u)
{
    memset(u, 0, sizeof *u);
    u->world_cursor = u->world_top = -1;
    for (int i = 0; i < UI_OPT_COUNT; i++) u->value[i] = UI_OPTS[i].def;
    for (int g = 0; g < 4; g++) { strcpy(u->sprites[g][0], g == 2 ? "None" : "Original"); u->sprite_count[g] = 1; }
    strcpy(u->sprites[3][1], "Random"); u->sprite_count[3] = 2;
    u->slot_count = 4;
    u->screen = UI_SLOTS_SCREEN;
    u->rng = (uint32_t)time(NULL) * 2654435761u | 1;
    ui_saved_load(u, NULL);
}

void ui_set_slot_count(SmzUi *u, int n)
{
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
#define HOME_ROWS 4
static const char *const HOME_NAMES[HOME_ROWS] = {"Start / Load game", "Online draft", "Championship", "Options"};
static const struct { int bit; const char *name, *desc; } OPTION_ROWS[] = {
    {UI_OPT_BUTTONS, "Buttons and keys", ""}, {UI_OPT_VIDEO, "Video", "resolution, screen"}, {UI_OPT_SOUND, "Sound", "music, chime, rival notes"},
    {0, "Player", "name, country"}, {UI_OPT_QUIT, "Quit", ""}};
#define OPTION_KINDS (int)(sizeof OPTION_ROWS / sizeof OPTION_ROWS[0])

void ui_home_enable(SmzUi *u, int mask) { u->home_on = 1; u->options_mask = mask; }
void ui_home_open(SmzUi *u) { u->screen = UI_LOGO; u->logo_ms = LOGO_MS; u->home_cursor = 0; }
void ui_tick(SmzUi *u, int ms) { if (u->screen == UI_LOGO && (u->logo_ms -= ms) <= 0) u->screen = UI_HOME; }

/* The Options rows this host has, as indices into OPTION_ROWS. */
static int option_rows(const SmzUi *u, int *kind)
{
    int n = 0;
    for (int i = 0; i < OPTION_KINDS; i++)
        if (OPTION_ROWS[i].bit ? u->options_mask & OPTION_ROWS[i].bit : u->player_on) kind[n++] = i;
    return n;
}

static void home_input(SmzUi *u, UiButton b)
{
    if (b == BTN_UP || b == BTN_DOWN) u->home_cursor = (u->home_cursor + (b == BTN_UP ? HOME_ROWS - 1 : 1)) % HOME_ROWS;
    else if (b != BTN_A) return;
    else if (u->home_cursor == 0) u->screen = UI_SLOTS_SCREEN;
    else if (u->home_cursor == 3) { u->screen = UI_OPTIONS; u->options_cursor = 0; }
    else if (u->home_cursor == 2 && u->race.on) { u->screen = UI_RACE_SIZE; u->race.cursor = 0; }
    else snprintf(u->notice, sizeof u->notice, u->home_cursor == 2 ? "Online races need a connection" : "Online draft isn't ready yet");
}

static void options_input(SmzUi *u, UiButton b)
{
    int kind[OPTION_KINDS], n = option_rows(u, kind), *c = &u->options_cursor;
    if (b == BTN_B) { u->screen = UI_HOME; return; }
    if (!n) return;
    if (b == BTN_UP || b == BTN_DOWN) *c = (*c + (b == BTN_UP ? n - 1 : 1)) % n;
    else if (b == BTN_A && *c < n) {
        int bit = OPTION_ROWS[kind[*c]].bit;
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
void ui_race_enable(SmzUi *u, int on) { u->race.on = on; }
const char *ui_race_format_key(const SmzUi *u) { return u->race.format == 1 ? "sprint" : u->race.format == 2 ? "half" : "full"; }

static void race_reset(SmzUi *u) { memset(&u->race.room, 0, sizeof u->race.room); u->race.getting_ready = u->race.started = 0; }

void ui_race_end(SmzUi *u) { race_reset(u); u->screen = UI_HOME; }

void ui_race_fail(SmzUi *u, const char *msg)
{
    snprintf(u->notice, sizeof u->notice, "%.31s", msg);
    u->race.getting_ready = 0;
    if (u->screen == UI_BUSY || u->screen == UI_RACE_PUBLIC || u->screen == UI_RACE_LIST) { u->screen = UI_RACE_WAY; u->race.cursor = 0; }
}

void ui_race_rooms(SmzUi *u, const char *json)
{
    u->race.room_count = smz_race_rooms(json, u->race.rooms, RACE_ROOMS);
    u->race.rooms_loaded = json ? 1 : -1;
    if (u->race.cursor >= u->race.room_count) u->race.cursor = 0;
}

/* A server answer: the room's state picks the screen while the player is on the race screens. */
void ui_race_update(SmzUi *u, const char *json, int64_t local_now)
{
    SmzRace *r = &u->race.room;
    int was = r->state;
    if (smz_race_read(r, json, local_now)) return;
    if (r->state != was) u->notice[0] = 0;   /* an error from the step before no longer applies */
    const SmzRacePlayer *me = smz_race_me(r);
    if (me && me->ready) u->race.getting_ready = 0;
    int on_race = u->screen == UI_BUSY || (u->screen >= UI_RACE_SIZE && u->screen <= UI_RACE_READY) || (u->screen == UI_SPRITES && u->back == UI_RACE_READY);
    if (!on_race) return;
    if (r->state == RACE_OPEN && u->screen != UI_RACE_ROOM) { u->screen = UI_RACE_ROOM; u->race.cursor = 0; }
    else if (r->state == RACE_CLOSED && u->screen != UI_RACE_READY && u->screen != UI_SPRITES) { u->screen = UI_RACE_READY; u->race.cursor = was == RACE_CLOSED ? u->race.cursor : 0; }
    else if (r->state == RACE_RUNNING && !u->race.started) { u->race.started = 1; u->action = ACT_RACE_START; }
    else if (r->state == RACE_CANCELLED) { race_reset(u); u->screen = UI_RACE_WAY; snprintf(u->notice, sizeof u->notice, "The room closed"); }
}

static int race_rows(const SmzUi *u)
{
    switch (u->screen) {
    case UI_RACE_SIZE: case UI_RACE_FORMAT: case UI_RACE_LENGTH: case UI_RACE_PUBLIC: return 2;
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
        case UI_RACE_WAY: u->screen = UI_RACE_SIZE; *c = u->race.size == 2 ? 0 : 1; break;
        case UI_RACE_FORMAT: u->screen = UI_RACE_WAY; *c = 0; break;
        case UI_RACE_LENGTH: u->screen = UI_RACE_FORMAT; *c = 1; break;
        case UI_RACE_PUBLIC: u->screen = u->race.format ? UI_RACE_LENGTH : UI_RACE_FORMAT; *c = u->race.format ? u->race.format - 1 : 0; break;
        case UI_RACE_LIST: u->screen = UI_RACE_WAY; *c = 2; break;
        case UI_RACE_ROOM: case UI_RACE_READY: u->action = ACT_RACE_LEAVE; race_reset(u); u->screen = UI_RACE_WAY; *c = 0; break;
        default: break;
        }
        return;
    }
    if (b != BTN_A) return;
    switch (u->screen) {
    case UI_RACE_SIZE: u->race.size = *c ? 8 : 2; u->screen = UI_RACE_WAY; *c = 0; break;
    case UI_RACE_WAY:
        if (*c == 0) { u->screen = UI_RACE_FORMAT; *c = 0; }
        else if (*c == 1) { u->name_for = 2; u->name[0] = 0; u->name_len = 0; u->key_cursor = 0; u->screen = UI_NAME; }
        else { u->race.rooms_loaded = 0; u->race.room_count = 0; u->screen = UI_RACE_LIST; *c = 0; u->action = ACT_RACE_FIND; }
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
        if (*c == r->n) u->action = ACT_RACE_CLOSE;
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
            const char *key = k < 24 ? UI_TABS[level][k] : NULL;
            if (!key) break;
            for (i = 0; i < UI_OPT_COUNT && strcmp(UI_OPTS[i].key, key); i++) {}
            if (i == UI_OPT_COUNT) continue;
        } else if (UI_OPTS[i].level != LEVEL_ITEMS) continue;
        if (i == UIO_HELPS && u->value[UIO_HELP] != 1) continue;   /* the count only matters in the limited mode */
        if (!strncmp(UI_OPTS[i].key, "Spazers", 7) && u->value[UIO_MODE] != 2) continue;   /* Spazer Hunt only */
        if (i == UIO_MUSIC && u->sprite_count[3] <= 2) continue;   /* no music package installed: nothing to pick (the 3DS has none) */
        idx[n++] = i;
    }
    return n;
}

static void change(SmzUi *u, int opt, int dir, int big)
{
    const UiOptDef *d = &UI_OPTS[opt];
    int *v = &u->value[opt];
    switch (d->kind) {
    case OPT_BOOL: *v = !*v; break;
    case OPT_SEED: { long n = (long)*v + dir * (big ? 1000 : 1); if (n < 0) n = 0; if (n > d->max) n = d->max; *v = (int)n; break; }
    case OPT_SPRITE: break;
    default: { int n = *v + dir; if (n < d->min) n = d->max; if (n > d->max) n = d->min; *v = n; }
    }
}

/* slot rows on screen: the Daily row takes the first place */
static int world_row(const SmzUi *u) { return u->ladder_on && u->daily_on; }   /* World goals beside the Daily challenge, in its row */
static int slots_shown(const SmzUi *u) { return UI_SLOTS_SHOWN + (UI_H - 240) / 47 - (u->daily_on != 0); }

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
static const UiPreset WEEKLY = {"Weekly run", "Short run: 2 crystals, 1 boss", "GanonCrystals=2,GtCrystals=2,TourianBosses=1"};
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

static int opt_by_key(const char *key, size_t len)
{
    for (int i = 0; i < UI_OPT_COUNT; i++) if (!strncmp(UI_OPTS[i].key, key, len) && !UI_OPTS[i].key[len]) return i;
    return -1;
}

/* the choices a saved custom does not carry: they depend on the files of this device */
static int spec_skips(int i) { return i == 0 || i == UIO_LINK || i == UIO_SAMUS || i == UIO_PLANDO || i == UIO_MUSIC; }

void ui_expected_values(const SmzUi *u, int *want)
{
    for (int i = 0; i < UI_OPT_COUNT; i++) want[i] = UI_OPTS[i].def;
    if (u->run_mode == 5) spec_values(want, WEEKLY.spec);   /* the weekly run's own rules */
}

static void reset_values(SmzUi *u) { for (int i = 0; i < UI_OPT_COUNT; i++) u->value[i] = UI_OPTS[i].def; }

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
}

static void make_spec(const SmzUi *u, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < UI_OPT_COUNT; i++) {
        if (spec_skips(i) || u->value[i] == UI_OPTS[i].def) continue;
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

static void saved_add(SmzUi *u, const char *name, const char *spec)
{
    int i;
    for (i = 0; i < u->saved_count && strcasecmp(u->saved[i].name, name); i++) {}
    if (i == u->saved_count) {
        if (i >= UI_MAX_SAVED) return;
        u->saved_count++;
    }
    snprintf(u->saved[i].name, sizeof u->saved[i].name, "%s", name);
    snprintf(u->saved[i].spec, sizeof u->saved[i].spec, "%s", spec);
    u->saved_dirty = 1;
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
    u->daily = *d; u->daily_has_run = has_run;
    if (u->screen == UI_DAILY && !had && daily_button(u)) u->daily_cursor = 0;   /* the answer came while the screen was open */
}

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
    if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_LEFT || b == BTN_UP) { if (u->daily_cursor > (go ? 0 : 1)) u->daily_cursor--; }
    else if (b == BTN_RIGHT || b == BTN_DOWN) { if (u->daily_cursor < 2) u->daily_cursor++; }
    else if (b == BTN_A) {
        if (go && u->daily_cursor == 0) { u->action = ACT_DAILY; u->screen = UI_SLOTS_SCREEN; }
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
    char label[16] = "";
    const char *runs = strstr(json, "\"runs\":");
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
    if (b == BTN_B || b == BTN_SELECT) u->screen = UI_SLOTS_SCREEN;
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

void ui_set_monthly(SmzUi *u, const char *label, int seed, int days_left)
{
    u->monthly_days = days_left;
    snprintf(u->monthly_label, sizeof u->monthly_label, "%s", label ? label : "");
    u->monthly_seed = seed > 0 ? seed : 0;
}

int ui_run_monthly(const SmzUi *u) { return u->run_mode == 4; }
int ui_run_weekly(const SmzUi *u) { return u->run_mode == 5; }

void ui_set_weekly(SmzUi *u, const char *label, int seed, int days_left)
{
    u->weekly_days = days_left;
    snprintf(u->weekly_label, sizeof u->weekly_label, "%s", label ? label : "");
    u->weekly_seed = seed > 0 ? seed : 0;
}

int ui_run_ladder(const SmzUi *u) { return u->run_mode >= 4 || ui_is_default(u); }
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
            u->online_mode = 1;
            if (!u->player[0]) { u->name_for = 1; u->name[0] = 0; u->name_len = 0; u->key_cursor = 0; u->screen = UI_NAME; return; }
            u->screen = UI_START;
        } else {
            u->online_mode = *c == 2 ? 2 : u->online_mode == 1 ? 0 : u->online_mode;
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
    reset_values(u);
    apply_spec(u, spec);
    u->run_mode = mode;
    snprintf(u->start_label, sizeof u->start_label, "%s", label);
    if (mode == 2) { u->screen = UI_CREATE; u->level = LEVEL_QUICK; for (int l = 0; l < LEVEL_COUNT; l++) u->cursor[l] = u->scroll[l] = 0; }
    else goto_start(u);
}

void ui_preset(SmzUi *u, int mode)
{
    if (mode == 0) begin_run(u, 0, CHILL.name, CHILL.spec);
    else begin_run(u, mode == 1 ? 1 : 2, mode == 1 ? ORIGINAL.name : "Custom", mode == 1 ? ORIGINAL.spec : "");
    u->screen = UI_SLOTS_SCREEN;
}

void ui_apply_spec(SmzUi *u, const char *spec) { apply_spec(u, spec); }

static int mode_rows(const SmzUi *u) { return 3 + (u->saved_count > 0) + (u->monthly_seed > 0) + (u->weekly_seed > 0); }

/* 0 Cozy, 1 Original, 2 Custom, 3 Saved customs, 4 Monthly seed, 5 Weekly run (the rows that apply, in this order) */
static int mode_kind(const SmzUi *u, int row)
{
    int kinds[6] = {0, 1, 2}, n = 3;
    if (u->saved_count > 0) kinds[n++] = 3;
    if (u->monthly_seed > 0) kinds[n++] = 4;
    if (u->weekly_seed > 0) kinds[n++] = 5;
    return kinds[row < n ? row : n - 1];
}

#define KEY_CHARS "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM.'!"   /* QWERTY, 10 to a row */
#define KEY_COLS 10
#define KEY_ROWS 4
#define KEY_COUNT (KEY_COLS * KEY_ROWS)
#define KEY_CTRL 4        /* Space, Delete, Save, Cancel */

static void name_input(SmzUi *u, UiButton b)
{
    int c = u->key_cursor;
    UiScreen back = u->name_for == 2 ? UI_RACE_WAY : u->name_for ? u->player_opts ? UI_OPTIONS : UI_START : UI_CREATE;
    if (b == BTN_B) { u->screen = back; return; }
    if (b == BTN_START) { u->key_cursor = KEY_COUNT + 2; return; }   /* to Save */
    if (b == BTN_L) { if (u->name_len) u->name[--u->name_len] = 0; return; }
    if (b == BTN_R) { if (u->name_len < (int)sizeof u->name - 1) { u->name[u->name_len++] = ' '; u->name[u->name_len] = 0; } return; }
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
        if (c < KEY_COUNT) { if (u->name_len < (int)sizeof u->name - 1) { u->name[u->name_len++] = KEY_CHARS[c]; u->name[u->name_len] = 0; } }
        else switch (c - KEY_COUNT) {
        case 0: if (u->name_len < (int)sizeof u->name - 1) { u->name[u->name_len++] = ' '; u->name[u->name_len] = 0; } break;
        case 1: if (u->name_len) u->name[--u->name_len] = 0; break;
        case 2: {
            char spec[UI_SPEC_LEN];
            int len = u->name_len;
            while (len && u->name[len - 1] == ' ') u->name[--len] = 0;
            if (!len) break;
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
                u->screen = back;
                if (u->player_opts) { country_open(u); break; }   /* from Options the country follows the name */
                if (u->online_gen && !u->country[0]) { u->country_cursor = u->country_scroll = 0; u->screen = UI_COUNTRY; }
                break;
            }
            make_spec(u, spec, sizeof spec);
            saved_add(u, u->name, spec);
            snprintf(u->notice, sizeof u->notice, "Saved: %.20s", u->name);
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

/* The options that differ from the Original rules, in list order; returns how many fit in idx. */
static int changed_opts(const SmzUi *u, int *idx)
{
    int n = 0;
    for (int i = 0; i < UI_OPT_COUNT && n < UI_MAX_OPTS; i++) {
        OptKind k = UI_OPTS[i].kind;
        if (k == OPT_SPRITE || k == OPT_SEED || u->value[i] == UI_OPTS[i].def) continue;
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
            else if (u->start_col) { u->country_cursor = 0; for (int i = 0; i < COUNTRY_COUNT; i++) if (!strcmp(COUNTRIES[i].code, u->country)) u->country_cursor = i; u->country_scroll = u->country_cursor < COUNTRY_ROWS ? 0 : u->country_cursor - COUNTRY_ROWS + 1; u->screen = UI_COUNTRY; }
            else { u->name_for = 1; snprintf(u->name, sizeof u->name, "%s", u->player); u->name_len = (int)strlen(u->name); u->key_cursor = 0; u->screen = UI_NAME; }
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
    else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
    else if (b == BTN_A) switch (mode_kind(u, u->mode_cursor)) {
    case 0: begin_run(u, 0, CHILL.name, CHILL.spec); break;
    case 1: begin_run(u, 1, ORIGINAL.name, ORIGINAL.spec); break;
    case 2: begin_run(u, 2, "Custom", ""); break;
    case 4: {
        char label[24];
        snprintf(label, sizeof label, "Monthly %s", u->monthly_label);
        begin_run(u, 4, label, ORIGINAL.spec);
        for (int i = 0; i < UI_OPT_COUNT; i++) if (!strcmp(UI_OPTS[i].key, "Seed")) u->value[i] = u->monthly_seed;
        break;
    }
    case 5:
        begin_run(u, 5, "Weekly", WEEKLY.spec);
        snprintf(u->start_label, sizeof u->start_label, "Weekly %s", u->weekly_label);
        for (int i = 0; i < UI_OPT_COUNT; i++) if (!strcmp(UI_OPTS[i].key, "Seed")) u->value[i] = u->weekly_seed;
        break;
    default: u->screen = UI_SAVED; u->saved_cursor = u->saved_scroll = u->saved_col = u->saved_confirm = 0; break;
    }
}

void ui_input(SmzUi *u, UiButton b)
{
    if (b == BTN_START && u->screen != UI_NAME) return;
    if (u->screen == UI_LOGO) return;   /* its 2 seconds can't be skipped */
    u->notice[0] = 0;
    switch (u->screen) {
    case UI_SLOTS_SCREEN:
        if (u->daily_on && (b == BTN_UP || b == BTN_DOWN)) {   /* the Daily (and World) row sits between the last slot and the first */
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
        else if (b == BTN_SELECT && u->ladder_on) ladder_open(u);
        else if (b == BTN_B && u->home_on) u->screen = UI_HOME;
        else if (b == BTN_A) {
            u->active_slot = u->slot_cursor;
            if (u->slots[u->slot_cursor].used) { u->screen = UI_SLOT_MENU; u->menu_cursor = 0; }
            else { u->screen = UI_MODE; u->mode_cursor = 0; }
        }
        break;
    case UI_SLOT_MENU:
        if (b == BTN_UP || b == BTN_DOWN) u->menu_cursor = !u->menu_cursor;
        else if (b == BTN_B) u->screen = UI_SLOTS_SCREEN;
        else if (b == BTN_A) {
            if (u->menu_cursor) { u->screen = UI_CONFIRM; u->confirm_cursor = 0; }   /* Cancel is the default */
            else { u->action = ACT_CONTINUE; u->action_slot = u->active_slot; u->screen = UI_SLOTS_SCREEN; }
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
        int rows = n + 2;                                   /* then Save as custom and Next */
        if (b == BTN_UP) move(cur, scr, rows, -1);
        else if (b == BTN_DOWN) move(cur, scr, rows, 1);
        else if (b == BTN_B) u->screen = UI_MODE;
        else if (b == BTN_L) { if (u->level > LEVEL_QUICK) u->level--; }
        else if (b == BTN_R) { if (u->level < LEVEL_ITEMS) u->level++; }
        else if (*cur == n) {
            if (b == BTN_A) {
                u->name_for = 0;
                snprintf(u->name, sizeof u->name, "Custom %d", u->saved_count + 1);
                u->name_len = (int)strlen(u->name);
                u->key_cursor = 0;
                u->screen = UI_NAME;
            }
        } else if (*cur == n + 1) {
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
    case UI_RACE_SIZE: case UI_RACE_WAY: case UI_RACE_FORMAT: case UI_RACE_LENGTH: case UI_RACE_PUBLIC:
    case UI_RACE_LIST: case UI_RACE_ROOM: case UI_RACE_READY: race_input(u, b); break;
    case UI_OPTIONS: options_input(u, b); break;
    case UI_LOGO:
    case UI_BUSY: break;
    }
}

UiCue ui_cue(const SmzUi *a, const SmzUi *z, int b)
{
    if (z->action != ACT_NONE && a->action == ACT_NONE) return z->action == ACT_DELETE || z->action == ACT_LADDER ? UI_CUE_SELECT : UI_CUE_CONFIRM;
    if (z->screen != a->screen) return b == BTN_B || z->screen == a->back || (z->screen == UI_SLOTS_SCREEN && a->screen != UI_HOME) ? UI_CUE_BACK : UI_CUE_OPEN;
    if (z->daily_info != a->daily_info) return z->daily_info ? UI_CUE_OPEN : UI_CUE_BACK;
    if (z->world_top != a->world_top) return z->world_top >= 0 ? UI_CUE_OPEN : UI_CUE_BACK;
    if (z->level != a->level || z->ladder_tab != a->ladder_tab || z->world_month != a->world_month) return UI_CUE_TAB;
    if (memcmp(z->value, a->value, sizeof z->value) || z->run_mode != a->run_mode || z->name_len != a->name_len || strcmp(z->name, a->name)) return UI_CUE_SELECT;
    if (z->slot_cursor != a->slot_cursor || z->menu_cursor != a->menu_cursor || z->confirm_cursor != a->confirm_cursor ||
        memcmp(z->cursor, a->cursor, sizeof z->cursor) || memcmp(z->scroll, a->scroll, sizeof z->scroll) ||
        z->sprite_cursor != a->sprite_cursor || z->mode_cursor != a->mode_cursor || z->saved_cursor != a->saved_cursor ||
        z->saved_col != a->saved_col || z->key_cursor != a->key_cursor || z->country_cursor != a->country_cursor ||
        z->online_cursor != a->online_cursor || z->start_cursor != a->start_cursor || z->start_col != a->start_col ||
        z->lc.cursor != a->lc.cursor || z->daily_sel != a->daily_sel || z->daily_cursor != a->daily_cursor ||
        z->rules_scroll != a->rules_scroll || z->ladder_scroll != a->ladder_scroll || z->slot_scroll != a->slot_scroll || z->world_cursor != a->world_cursor ||
        z->home_cursor != a->home_cursor || z->options_cursor != a->options_cursor || z->race.cursor != a->race.cursor)
        return UI_CUE_MOVE;
    return UI_CUE_NONE;
}

void ui_touch(SmzUi *u, int x, int y)
{
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
        case H_LTAB: u->ladder_tab = t->idx; u->ladder_scroll = 0; break;
        case H_WMONTH: u->world_cursor = -1; if (t->idx != u->world_month) ui_input(u, BTN_A); break;
        case H_WGOAL: u->world_cursor = t->idx; if (u->world_month) ui_input(u, BTN_A); break;
        case H_CONFIRM: u->confirm_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_MENU: u->menu_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_TAB: u->level = t->idx; break;
        case H_ROW: u->cursor[u->level] = t->idx; ui_input(u, BTN_A); break;
        case H_SCROLL: {
            int idx[UI_MAX_OPTS], n = visible(u, u->level, idx);
            for (int k = 0; k < 3; k++) move(&u->cursor[u->level], &u->scroll[u->level], n + 2, t->idx);
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
        case H_OPTION: u->options_cursor = t->idx; ui_input(u, BTN_A); break;
        case H_RACE: u->race.cursor = t->idx; ui_input(u, BTN_A); break;
        }
        return;
    }
    if (u->screen == UI_LOGO || u->screen == UI_HOME || u->screen == UI_BUSY || (u->screen == UI_SLOTS_SCREEN && !u->home_on)) return;
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
    int fin = t->game == 3;
    snprintf(s, sizeof s, fin ? "%.11s" : "%.20s", t->name);
    text(u, 88, y + 2, C_TEXT, 0, s);
    long long sec = t->play_ms / 1000;
    snprintf(s, sizeof s, "%lld:%02lld:%02lld", sec / 3600, sec / 60 % 60, sec % 60);
    text(u, 8 + w - 8, y + 2, fin ? C_GOLD : C_DIM, 2, s);
    if (fin) {   /* the final time, under a gold tag */
        int tx = 8 + w - 8 - (int)strlen(s) * 7 - 70;
        rect(u, tx, y + 1, 62, 11, C_GOLD);
        text(u, tx + 31, y + 2, C_BG, 1, "FINISHED");
    }
    /* Link with the crystals the run asks for, Samus with its bosses; what is missing is dark (the pictures are dimmed).
     * All seven crystals go by number; fewer are slots filled by the count, the bosses beaten first. */
    char sk[56];   /* the run's own sprites; the games' standing ones (tools/gen_original_icons.py) for the originals */
    snprintf(sk, sizeof sk, "link/%s", t->link_sprite[0] ? t->link_sprite : "Original");
    put(u, 2, 88, y + 15, 10, 14, C_TEXT, 0, sk);
    int cn = t->crystals_need ? t->crystals_need : 7, bn = t->bosses_need ? t->bosses_need : 4, got = 0, order[4], k = 0;
    for (int n = 0; n < 7; n++) got += t->crystal_mask >> n & 1;
    for (int n = 1; n <= cn; n++) gem(u, 106 + (n - 1) * 14, y + 17, cn == 7 ? t->crystal_mask >> (n - 1) & 1 : n <= got, cn == 7 && (n == 5 || n == 6));
    snprintf(sk, sizeof sk, "samus/%s", t->samus_sprite[0] ? t->samus_sprite : "Original");
    put(u, 2, 88, y + 29, 10, 14, C_TEXT, 0, sk);
    static const char *const BOSS[4] = {"reward/kraid", "reward/phantoon", "reward/draygon", "reward/ridley"};
    for (int b = 0; b < 4; b++) if (t->boss_mask >> b & 1) order[k++] = b;
    for (int b = 0; b < 4; b++) if (!(t->boss_mask >> b & 1)) order[k++] = b;
    for (int n = 0; n < (bn == 4 ? 4 : bn); n++) {
        int b = bn == 4 ? n : order[n];
        put(u, 2, 106 + n * 20, y + 30, 13, 13, t->boss_mask >> b & 1 ? C_TEXT : C_DIM, 0, BOSS[b]);
    }
    if (t->online) {
        char who[24];
        int wc = t->online == 2 ? C_ACCENT : t->online == 3 ? C_GREEN : C_CYAN, fx = 8 + w - 8 - (u->flags ? 12 : 14);
        snprintf(who, sizeof who, "%.12s", u->player[0] ? u->player : "Online");
        flag(u, fx, y + 15, wc, u->country);
        text(u, u->country[0] ? fx - 4 : 8 + w - 8, y + 14, wc, 2, who);
    }
    bar(u, 214, y + 27, w - 214, t->zelda_cleared, t->zelda_total, C_GREEN);
    bar(u, 214, y + 36, w - 214, t->metroid_cleared, t->metroid_total, C_ACCENT);
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
    case DAILY_STARTED: smz_daily_span(now_ms() - d->started_at, t, sizeof t); snprintf(out, cap, "Started  -  %s on the clock", t); break;
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
    wrap(u, tx, y + 16, C_TEXT, (w - 18) / 7, 13, 1, d->goal);
    daily_status(u, s, sizeof s);
    int sx = narrow ? tx + (left > 0 ? 44 : 70) : tx;
    int room = (x + w - 6 - sx) / 7;   /* cut to the card */
    if (room >= 0 && room < (int)sizeof s) s[room] = 0;
    text(u, sx, y + 28, d->state == DAILY_DONE ? C_GREEN : d->state == DAILY_FAILED || d->state == DAILY_LATE ? C_WARN : C_DIM, 0, s);
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
        wrap(u, 16, y + 20, C_ACCENT, 40, 14, 2, d->goal);
        y += 72;
        int64_t left = d->closes_at - now_ms();
        smz_daily_span(left, t, sizeof t);
        snprintf(s, sizeof s, left > 0 ? "%s left" : "Window closed", t);
        text(u, 16, y, left > 0 && left < 15 * 60000 ? C_WARN : C_TEXT, 0, s);
        daily_status(u, s, sizeof s);
        text(u, 16, y + 16, d->state == DAILY_DONE ? C_GREEN : d->state == DAILY_FAILED || d->state == DAILY_LATE ? C_WARN : C_DIM, 0, s);
        if (d->state == DAILY_OPEN && left > 0 && left < 3600000) text(u, 16, y + 34, C_WARN, 0, "Under 1 hour left");
    }
    /* Start (when there is something to start), Info, Back */
    int go = daily_button(u), by = 198, n = go ? 3 : 2, w = (UI_W - 16 - (n - 1) * 6) / n, x = 8;
    static const char *const NAME[3] = {NULL, "Info", "Back"};
    for (int id = go ? 0 : 1; id < 3; id++, x += w + 6) {
        rect(u, x, by, w, 24, u->daily_cursor == id ? C_SEL : C_PANEL);
        text(u, x + w / 2, by + 6, id == 0 ? C_ACCENT : C_TEXT, 1, id == 0 ? (go == 1 ? "Start" : "Continue") : NAME[id]);
        hit(u, H_DAILY_BTN, id, x, by, w, 24);
    }
}

/* A row of the race screens: a name on the left, a note on the right. */
static void race_row(SmzUi *u, int r, int y, const char *name, const char *note, int color)
{
    rect(u, 8, y, UI_W - 16, 28, r == u->race.cursor ? C_SEL : C_PANEL);
    hit(u, H_RACE, r, 8, y, UI_W - 16, 28);
    text(u, 18, y + 8, color, 0, name);
    if (note && note[0]) text(u, UI_W - 18, y + 8, C_DIM, 2, note);
}

static void race_draw(SmzUi *u)
{
    char buf[64], note[48];
    SmzRace *r = &u->race.room;
    switch (u->screen) {
    case UI_RACE_SIZE:
        title(u, "Championship");
        text(u, 10, 34, C_DIM, 0, "A normal run from the start. First to the goal wins.");
        race_row(u, 0, 52, "1 vs 1 race", "2 players", C_TEXT);
        race_row(u, 1, 86, "Multi race", "up to 8 players", C_TEXT);
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_WAY:
        title(u, u->race.size == 2 ? "1 vs 1 race" : "Multi race");
        race_row(u, 0, 36, "Create a room", "", C_TEXT);
        race_row(u, 1, 70, "Join with a code", "", C_TEXT);
        race_row(u, 2, 104, "Find a race", "public rooms", C_TEXT);
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_FORMAT:
        title(u, "Championship");
        race_row(u, 0, 36, "Full game", "Ganon and Mother Brain", C_TEXT);
        race_row(u, 1, 70, "Partial game", "pick a length", C_TEXT);
        legend(u, "A Choose  B Back");
        break;
    case UI_RACE_LENGTH:
        title(u, "Partial game");
        race_row(u, 0, 36, "Sprint", "2 crystals, 1 boss", C_TEXT);
        race_row(u, 1, 70, "Half", "4 crystals, 2 bosses", C_TEXT);
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
        for (int i = 0; i < u->race.room_count && i < 6; i++) {
            const SmzRaceRoom *w = &u->race.rooms[i];
            snprintf(buf, sizeof buf, "%s  %s", w->creator, smz_race_format_name(w->format));
            snprintf(note, sizeof note, "%d of %d", w->players, w->size);
            race_row(u, i, 36 + i * 32, buf, note, C_TEXT);
        }
        legend(u, "A Join  B Back");
        break;
    case UI_RACE_ROOM: {
        int creator = !strcmp(r->you, r->creator);
        title(u, "Room");
        snprintf(buf, sizeof buf, "Championship, %s", smz_race_format_name(r->format));
        text(u, 10, 34, C_DIM, 0, buf);
        snprintf(buf, sizeof buf, "Code  %s", r->code);
        text(u, UI_W - 10, 34, C_ACCENT, 2, buf);
        snprintf(buf, sizeof buf, "%d of %d players here%s", r->n, r->size, r->is_public ? ", public" : "");
        text(u, 10, 48, C_TEXT, 0, buf);
        for (int i = 0; i < r->n; i++) {
            int y = 62 + i * 18, mine = !strcmp(r->p[i].pid, r->you);
            rect(u, 8, y, UI_W - 16, 16, i == u->race.cursor && creator ? C_SEL : C_PANEL);
            hit(u, H_RACE, i, 8, y, UI_W - 16, 16);
            text(u, 14, y + 4, mine ? C_ACCENT : C_TEXT, 0, r->p[i].name);
            text(u, UI_W - 14, y + 4, C_DIM, 2, !strcmp(r->p[i].pid, r->creator) ? "creator" : creator && !mine ? "A remove" : "");
        }
        if (creator) race_row(u, r->n, 66 + r->n * 18, "Close the room", r->n < 2 ? "needs 2 players" : "skins and the ready room", r->n < 2 ? C_DIM : C_TEXT);
        else text(u, UI_W / 2, 200, C_DIM, 1, "Give this code to the other players");
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
            text(u, 12, y, !strcmp(r->p[i].pid, r->you) ? C_ACCENT : C_TEXT, 0, r->p[i].name);
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
        if (u->daily_sel == 1) snprintf(buf, sizeof buf, "SMZ3  -  Special run");
        else if (u->daily_sel == 2) snprintf(buf, sizeof buf, "SMZ3  -  World goals");
        else snprintf(buf, sizeof buf, more ? (u->daily_on ? "SMZ3  -  Runs  (%d/%d)" : "SMZ3  -  Select a run  (%d/%d)") : u->daily_on ? "SMZ3  -  Runs" : "SMZ3  -  Select a run", u->slot_cursor + 1, u->slot_count);
        title(u, buf);
        if (u->ladder_on) {
            rect(u, UI_W - 70, 4, 64, 20, C_SEL);
            text(u, UI_W - 38, 8, C_TEXT, 1, "Ladder >");
            hit(u, H_LADDER, 0, UI_W - 70, 4, 64, 20);
        }
        if (u->daily_on && u->daily.loaded == 1) token_chip(u, UI_W - 70 - 52, 4);
        int w = UI_W - 16 - (more ? 26 : 0), top = 32, shown = slots_shown(u);
        if (world_row(u)) { int half = (w - 6) / 2; daily_row(u, 8, top, half); world_list_row(u, 8 + half + 6, top, w - half - 6); top += 47; }
        else if (u->daily_on) { daily_row(u, 8, top, w); top += 47; }
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
        legend(u, u->ladder_on ? u->home_on ? "A Open  Select Ladder  B Back" : "A Open  Select Ladder" : u->home_on ? "A Open  B Back" : "A Open");
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
            for (int t = 0; t < LEVEL_COUNT; t++) {
                int x = 4 + t * 52;
                hit(u, H_TAB, t, x, 4, 50, 20);
                rect(u, x, 4, 50, 20, u->level == t ? C_SEL : C_BG);
                text(u, x + 25, 8, C_TEXT, 1, TABS[t]);
            }
            if (u->level == LEVEL_QUICK) text(u, 6, 30, C_DIM, 0, "The options most players change");
        }
        for (int r = 0; r < ROWS_SHOWN && scr + r <= n + 1; r++) {
            int row = scr + r, y = LIST_Y + r * ROW_H;
            rect(u, 4, y, UI_W - 40, ROW_H - 2, row == cur ? C_SEL : C_PANEL);
            hit(u, H_ROW, row, 4, y, UI_W - 40, ROW_H - 2);
            if (row == n) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Save as custom..."); continue; }
            if (row == n + 1) { text(u, (UI_W - 40) / 2, y + 4, C_ACCENT, 1, "Next: sprites and start"); continue; }
            ui_value_text(u, idx[row], val, sizeof val);
            text(u, 10, y + 4, C_TEXT, 0, UI_OPTS[idx[row]].label);
            text(u, UI_W - 46, y + 4, C_ACCENT, 2, val);
            if ((idx[row] == UIO_LINK || idx[row] == UIO_SAMUS) && u->value[idx[row]] > 0 && u->value[idx[row]] < u->sprite_count[idx[row] == UIO_SAMUS]) {
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
            const char *tip = u->notice[0] ? u->notice : cur < n ? UI_OPTS[idx[cur]].tip : cur == n ? "Saves these options under a name." : "Choose the sprites, then start.";
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
    case UI_HOME:
        put(u, 2, UI_W / 2 - 16, 10, 32, 32, C_TEXT, 0, "logo");
        text(u, UI_W / 2, 48, C_ACCENT, 1, "SMZ3");
        for (int r = 0; r < HOME_ROWS; r++) {
            int y = 70 + r * 34, online = r == 1 || (r == 2 && !u->race.on);
            rect(u, 24, y, UI_W - 48, 28, r == u->home_cursor ? C_SEL : C_PANEL);
            rect(u, 24, y, 5, 28, online ? C_DIM : C_ACCENT);
            hit(u, H_HOME, r, 24, y, UI_W - 48, 28);
            text(u, 38, y + 8, online ? C_DIM : C_TEXT, 0, HOME_NAMES[r]);
            if (online) text(u, UI_W - 32, y + 8, C_DIM, 2, "Soon");
        }
        if (u->notice[0]) text(u, UI_W / 2, UI_H - 30, C_WARN, 1, u->notice);
        legend(u, "A Choose");
        break;
    case UI_OPTIONS: {
        title(u, "Options");
        int kind[OPTION_KINDS], n = option_rows(u, kind);
        for (int r = 0; r < n; r++) {
            int y = 36 + r * 34;
            rect(u, 8, y, UI_W - 16, 28, r == u->options_cursor ? C_SEL : C_PANEL);
            hit(u, H_OPTION, r, 8, y, UI_W - 16, 28);
            text(u, 18, y + 8, C_TEXT, 0, OPTION_ROWS[kind[r]].name);
            const char *d = OPTION_ROWS[kind[r]].desc;
            if (!OPTION_ROWS[kind[r]].bit && u->player[0]) { snprintf(buf, sizeof buf, "%s %s", u->player, u->country); d = buf; }
            text(u, UI_W - 18, y + 8, C_DIM, 2, d);
        }
        legend(u, "A Choose  B Back");
        break;
    }
    case UI_RACE_SIZE: case UI_RACE_WAY: case UI_RACE_FORMAT: case UI_RACE_LENGTH: case UI_RACE_PUBLIC:
    case UI_RACE_LIST: case UI_RACE_ROOM: case UI_RACE_READY: race_draw(u); break;
    case UI_MODE: {
        snprintf(buf, sizeof buf, "New run  -  Slot %d", u->active_slot + 1);
        title(u, buf);
        int rows = mode_rows(u), pitch = rows > 5 ? 31 : rows > 4 ? 40 : 50, rh = pitch - (rows > 5 ? 3 : 6);
        int gap = rows > 5 ? 12 : 14, dy = (rh - gap - 14) / 2;   /* the name and its line centred in the row */
        for (int r = 0; r < rows; r++) {
            int y = 34 + r * pitch, k = mode_kind(u, r);
            const char *name = k == 0 ? CHILL.name : k == 1 ? ORIGINAL.name : k == 2 ? "Custom" : k == 3 ? "Saved customs" : k == 4 ? "Monthly run" : WEEKLY.name;
            char desc[48];
            if (k == 0) snprintf(desc, sizeof desc, "%s", CHILL.desc);
            else if (k == 1) snprintf(desc, sizeof desc, "%s", ORIGINAL.desc);
            else if (k == 2) snprintf(desc, sizeof desc, "Pick every option yourself");
            else if (k == 3) snprintf(desc, sizeof desc, "%d saved", u->saved_count);
            else if (k == 5 && u->weekly_days <= 0) snprintf(desc, sizeof desc, "%s: ends today, 2 crystals, 1 boss", u->weekly_label);
            else if (k == 5) snprintf(desc, sizeof desc, "%s: %dd left, 2 crystals, 1 boss", u->weekly_label, u->weekly_days);
            else if (u->monthly_days <= 0) snprintf(desc, sizeof desc, "%s: ends today", u->monthly_label);
            else snprintf(desc, sizeof desc, "%s: %d day%s left", u->monthly_label, u->monthly_days, u->monthly_days == 1 ? "" : "s");
            rect(u, 8, y, UI_W - 16, rh, r == u->mode_cursor ? C_SEL : C_PANEL);
            rect(u, 8, y, 5, rh, k == 4 ? C_GREEN : k == 5 ? C_CYAN : C_ACCENT);
            hit(u, H_MODE, r, 8, y, UI_W - 16, rh);
            text(u, 20, y + dy, C_TEXT, 0, name);
            text(u, 20, y + dy + gap, C_DIM, 0, desc);
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
        title(u, u->name_for ? "Player name" : "Save custom as");
        char field[40];
        snprintf(field, sizeof field, "%s_", u->name);
        rect(u, 10, 32, UI_W - 20, 24, C_PANEL);
        text(u, 16, 38, C_TEXT, 0, field);
        for (int k = 0; k < KEY_COUNT; k++) {
            int x = 10 + (k % KEY_COLS) * 30, y = 62 + (k / KEY_COLS) * 30;
            char ch[2] = {KEY_CHARS[k], 0};
            rect(u, x, y, 28, 28, k == u->key_cursor ? C_SEL : C_PANEL);
            hit(u, H_KEY, k, x, y, 28, 28);
            text(u, x + 14, y + 8, C_TEXT, 1, ch);
        }
        static const char *const CTRL[KEY_CTRL] = {"Space", "Delete", "Save", "Cancel"};
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
        if (!n) snprintf(head, sizeof head, "Original run - no changes");
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
        snprintf(buf, sizeof buf, "%s  -  Sprites", u->start_label);
        title(u, buf);
        int rh = u->player_on ? 46 : 56, step = rh + 6;
        for (int g = 0; g < 2; g++) {
            int y = 34 + g * step, v = u->value[g ? UIO_SAMUS : UIO_LINK];
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
        if (u->player_on) {
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
        static const char *const SUB[3] = {"Pick a name and flag", "Offline now, ask next run", "Change it in the Player row"};
        for (int r = 0; r < 3; r++) {
            int y = 86 + r * 46;
            rect(u, 8, y, UI_W - 16, 42, r == u->online_cursor ? C_SEL : C_PANEL);
            rect(u, 8, y, 5, 42, r == 0 ? C_GREEN : C_ACCENT);
            hit(u, H_ONLINE, r, 8, y, UI_W - 16, 42);
            text(u, 22, y + 6, C_TEXT, 0, ROW[r]);
            text(u, 22, y + 24, C_DIM, 0, SUB[r]);
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
        static const char *const TABS[4] = {"Original", "Monthly", "Weekly", "World"};
        const typeof(u->ladder[0]) *l = &u->ladder[u->ladder_tab < 3 ? u->ladder_tab : 0];
        if (u->ladder_tab == 3) snprintf(buf, sizeof buf, "World goals  %s", month_name(u->world_month && u->world_prev.n ? u->world_prev.month : u->world.month));
        else if (l->label[0] && u->ladder_tab) snprintf(buf, sizeof buf, "Online ladder  %s", l->label);
        else snprintf(buf, sizeof buf, "Online ladder");
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
            legend(u, u->world_cursor < 0 ? (u->world_month ? "A This month  Left Right Tab" : "A Last month  Left Right Tab")
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
        legend(u, "Left Right Tab  Up Down Scroll  Select Save list");
        break;
    }
    case UI_DAILY: daily_screen(u); break;
    case UI_BUSY:
        title(u, "Generating");
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
}
