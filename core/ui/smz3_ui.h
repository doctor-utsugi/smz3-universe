/* The start menu, the Create run options and the sprite pickers as a platform-free state machine. A frontend feeds it
 * button events and draws the rectangles and texts it produces on a 320x240 canvas. */
#ifndef SMZ3_UI_H
#define SMZ3_UI_H

#include <stdint.h>
#include <stddef.h>
#include "smz3_daily.h"

#define UI_LADDER_ROWS 30
#define UI_LADDER_SHOWN (8 + (UI_H - 240) / 20)
#define UI_WORLD_GOALS 6
/* The canvas is as wide as the host's screen allows (320 for 4:3 up to 432; ui_set_width) and 240 high, taller on a phone held
 * upright (ui_set_height): the lists use the extra height, the other screens stay a 240 block in the middle. */
extern int smz_ui_w, smz_ui_h;
#define UI_W smz_ui_w
#define UI_H smz_ui_h
#define UI_SLOTS 32           /* most slots a frontend can show; each one sets how many it keeps with ui_set_slot_count */
#define UI_SLOTS_SHOWN 4
#define UI_MAX_SPRITES 640
#define UI_MAX_OPTS 128
#define UI_MAX_CMDS 512
#define UI_MAX_HITS 64
#define UI_MAX_SAVED 16
#define UI_SPEC_LEN 512

typedef enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_L, BTN_R, BTN_SELECT, BTN_START } UiButton;   /* START: only the keyboard uses it (to Save) */
typedef enum { UI_SLOTS_SCREEN, UI_SLOT_MENU, UI_CREATE, UI_SPRITES, UI_BUSY, UI_CONFIRM, UI_MODE, UI_SAVED, UI_NAME, UI_START, UI_COUNTRY, UI_ONLINE, UI_LOADCHOICE, UI_LADDER, UI_RULES, UI_DAILY, UI_LOGO, UI_HOME, UI_OPTIONS } UiScreen;
typedef enum { OPT_ENUM, OPT_INT, OPT_BOOL, OPT_SEED, OPT_SPRITE } OptKind;
/* Tabs of the Create run screen; Quick repeats the options most players change.  Only LEVEL_ITEMS is read from UiOptDef.level; the other tabs list their keys in UI_TABS. */
typedef enum { LEVEL_QUICK, LEVEL_RUN, LEVEL_ZELDA, LEVEL_METROID, LEVEL_PLAY, LEVEL_ITEMS, LEVEL_COUNT } OptLevel;
typedef enum { ACT_NONE, ACT_CONTINUE, ACT_DELETE, ACT_GENERATE, ACT_LOAD_SAVE, ACT_LOAD_STATE, ACT_LADDER, ACT_DAILY, ACT_OPTIONS } UiAction;

typedef struct {
    const char *key;             /* C# property name for logic options */
    const char *label;
    const char *tip;
    OptKind kind;
    OptLevel level;
    int min, max, def;
    const char *const *names;    /* OPT_ENUM labels, max-min+1 entries */
} UiOptDef;

typedef struct {
    int used; char name[24]; char summary[48];   /* "Zelda 120/216 - Metroid 80/100 - 22:29:03" is 41 */
    int stats, zelda_cleared, zelda_total, metroid_cleared, metroid_total, game, boss_mask, crystal_mask;
    int crystals_need, bosses_need;   /* what the run's rules ask for (7 and 4 unless ui_set_slot_rules says otherwise) */
    char link_sprite[40], samus_sprite[40];   /* the sprites the run was made with ("" = the originals) */   /* stats: the numbers below are set; game 0 new, 1 Zelda, 2 Metroid, 3 finished */
    int64_t play_ms;
    int online;                  /* 0 offline run, 1 original-rules run on the online ladder, 2 monthly seed run, 3 weekly run */
} UiSlot;
/* A saved custom: a name and its options that differ from the defaults as "Key=value,Key=value" (option keys, so the list survives new options). */
typedef struct { char name[24]; char spec[UI_SPEC_LEN]; } UiSaved;

/* kind 0 rectangle, 1 text, 3 gradient to the colour in align, 4 progress ring (align: percent; only hosts that set rings get it),
 * 5 glowing orb of diameter w centred in its box (only hosts that set orbs get it),
 * 2 sprite preview: draw the picture of text ("link/<name>" or "samus/<name>", or "goal/<sprite>" for a world goal, file <name>.pv beside the
 * .rdc: u8 width, u8 height, then width*height RGBA bytes) centred and scaled to fit the x, y, w, h box. */
typedef struct { uint8_t kind; int16_t x, y, w, h; uint8_t color; uint8_t align; char text[64]; } UiCmd;
enum { C_BG, C_PANEL, C_SEL, C_TEXT, C_DIM, C_ACCENT, C_WARN, C_GREEN, C_CYAN, C_TEAL, C_NAVY, C_VIOLET, C_GOLD,
       C_RG_W, C_RG_H, C_RG_M, C_RG_S, C_RG_R, C_RG_H2, C_RG_M2, C_RG_S2, C_RG_R2 };   /* C_RG_*: the rupeergy (the currency), its two blink frames */
#define UI_COLORS 22

typedef struct { int16_t x, y, w, h; uint8_t kind; int16_t idx; } UiHit;
enum { H_SLOT, H_MENU, H_TAB, H_ROW, H_SPRITE, H_SCROLL, H_CREATE, H_BACK, H_CHEVRON, H_CONFIRM, H_MODE, H_SAVED, H_SAVED_DEL, H_KEY, H_START, H_COUNTRY, H_ONLINE, H_LOADCHOICE, H_LADDER, H_LTAB, H_DAILY, H_DAILY_BTN, H_WORLD, H_WMONTH, H_WGOAL, H_HOME, H_OPTION };

typedef struct {
    UiScreen screen, back;
    UiSlot slots[UI_SLOTS];
    int slot_cursor, menu_cursor, active_slot, slot_count, slot_scroll, confirm_cursor;
    int level;                   /* LEVEL_* shown in the options screen */
    int cursor[LEVEL_COUNT], scroll[LEVEL_COUNT];
    int value[UI_MAX_OPTS];
    char sprites[4][UI_MAX_SPRITES][32];  /* 0 Link, 1 Samus, 2 plando files, 3 music packages; index 0 is "Original" ("None" for plandos, "Original" then "Random" for music) */
    int sprite_count[4], sprite_game, sprite_cursor;
    int mode_cursor, run_mode;   /* run_mode: 0 Chill run, 1 Original, 2 Custom, 3 Saved custom */
    UiSaved saved[UI_MAX_SAVED];
    int saved_count, saved_cursor, saved_scroll, saved_col, saved_confirm, saved_dirty;
    char name[24];               /* the name being typed for a new saved custom or for the player */
    int name_len, key_cursor;
    int name_for;                /* 0 saved custom, 1 player name */
    int player_on, player_dirty; /* the host shows the Player row (it has an online ladder) / the profile changed */
    char version[16];   /* the release shown in the run list's corner; empty where another screen shows it */
    char player[24], country[3]; /* the profile sent with finished runs; country is an ISO 3166 code or empty */
    int country_cursor, country_scroll;
    char monthly_label[16]; int monthly_seed;  /* the month's fixed seed (0 until the host knows it) and its label, e.g. 2026-10 */
    int monthly_days;                   /* days left until the month's seed changes */
    char weekly_label[16]; int weekly_seed, weekly_days;   /* the week's fixed seed, its label (e.g. 2026-W44) and the days left */
    struct { int64_t save_ms, state_ms; int save_z, save_m, state_z, state_m, zt, mt, cursor; } lc;   /* the load choice shown by UI_LOADCHOICE */
    int online_mode, online_cursor, online_gen; /* 0 ask before each run, 1 upload, 2 never; the prompt's row; 1 when the prompt came from Start */
    int rings;                   /* the host draws progress rings (kind 4): the run list shows its percent inside one */
    int orbs;                    /* the host draws orbs (kind 5): the load choice's marker on its time line */
    float orb_x, orb_k;          /* that marker as drawn: it eases to the choice and grows in when the screen opens */
    int orb_on;
    int flags;                   /* the host draws "flag/<CC>" pictures; without them countries show as their code */
    int ladder_on, ladder_tab, ladder_scroll;   /* the host can show the online tables / the tab shown (0 original, 1 monthly, 2 weekly) / first row shown */
    struct { int loaded; char label[16]; int n; struct { char name[24]; char cc[3]; int64_t ms; } row[UI_LADDER_ROWS]; } ladder[3];   /* loaded: 0 not yet, 1 ok, -1 failed */
    struct { int loaded; char month[8]; int64_t ends_at; int counted, gems_if_met, n;
             struct { char key[16], title[40], sprite[32]; int64_t target, total, min, mine; } g[UI_WORLD_GOALS]; } world;   /* the ladder's World tab (tab 3) */
    struct { char month[8]; int n, counted, gems;
             struct { char key[16], title[40], sprite[32]; int64_t target, total, mine; int rank, players, nt;
                      struct { char name[25], cc[3]; int64_t n; } top[10]; } g[UI_WORLD_GOALS]; } world_prev;   /* the month before, with each goal's top 10 */
    int world_month, world_cursor, world_top;   /* the World tab shows last month / its cursor (-1 the month switch) / the goal whose top 10 is open (-1 none) */
    int daily_on, daily_sel, daily_cursor, daily_has_run, daily_info;   /* the run list's Daily row: shown / has the cursor (2: the World goals row under it); its screen's button (0 Start, 1 Info, 2 Back); the host keeps an attempt; the Info page is open */
    SmzDailyInfo daily;
    char start_label[24], notice[32];
    int home_on, home_cursor, logo_ms;   /* the start menu is on / its row / what is left of the logo's fixed time */
    int options_mask, options_cursor, player_opts;   /* UI_OPT_* rows the host can open / the row / the Player row was opened from Options */
    int rules_scroll;            /* first row shown on the rules screen */
    int start_cursor, start_col;  /* sprite screen: row (Link, Samus, Start) and column (sprite, Random) */
    UiScreen start_from;
    uint32_t rng;
    UiAction action;             /* set by an input event, read and cleared by the host */
    int action_slot;
    char status[44];
    UiCmd cmd[UI_MAX_CMDS];
    int cmd_count;
    UiHit hit[UI_MAX_HITS];
    int hit_count;
} SmzUi;

/* Option indices the host reads from u->value (see ui_settings for the rest). */
enum { UIO_LINK = 6, UIO_SAMUS = 7, UIO_PLANDO = 18, UIO_JINGLE = 19, UIO_MAP = 8, UIO_MODE = 17, UIO_DARK = 9, UIO_FILTER = 10, UIO_SHAPE = 11, UIO_HELP = 12, UIO_HELPS = 13, UIO_NERF = 22, UIO_SAVES = 23, UIO_MORPH = 24, UIO_MUSIC = 25 };

extern const UiOptDef UI_OPTS[];
extern const int UI_OPT_COUNT;
extern const char *const UI_TABS[LEVEL_ITEMS][24];   /* option keys per tab, in display order, NULL ended */

void ui_init(SmzUi *u);
void ui_set_slot(SmzUi *u, int i, int used, const char *name, const char *summary);
void ui_set_slot_count(SmzUi *u, int n);
/* The numbers a used slot shows (percent, counters, time); without them the summary text is drawn. */
void ui_set_slot_stats(SmzUi *u, int i, int zelda_cleared, int zelda_total, int metroid_cleared, int metroid_total, int64_t play_ms, int game, int boss_mask, int crystal_mask);
/* The crystals and bosses the run asks for, read from its settings JSON (ui_settings_json); call after ui_set_slot_stats. */
void ui_set_slot_rules(SmzUi *u, int i, const char *settings_json);
/* Marks a used slot as an online run (see UiSlot.online): it is drawn on a gradient with the player's name. */
void ui_set_slot_online(SmzUi *u, int i, int kind);
/* Text of an option's current value ("On", "Medium", a sprite name ...). */
void ui_value_text(const SmzUi *u, int opt, char *out, size_t n);
/* game 0 Link sprites, 1 Samus sprites, 2 plando files, 3 music packages */
void ui_set_sprites(SmzUi *u, int game, const char *const *names, int n);
void ui_input(SmzUi *u, UiButton b);
/* Touch at canvas coordinates (as drawn by the last ui_draw): selects and activates the row under the point. */
void ui_touch(SmzUi *u, int x, int y);
typedef enum { UI_CUE_NONE, UI_CUE_MOVE, UI_CUE_TAB, UI_CUE_SELECT, UI_CUE_OPEN, UI_CUE_BACK, UI_CUE_CONFIRM } UiCue;
/* The sound an input should make, from the menu before and after it; b is the button pressed, or -1 for a touch. */
UiCue ui_cue(const SmzUi *before, const SmzUi *after, int b);
/* Saved customs: text is the file the host stored ("name<TAB>spec" lines); NULL (no file yet) installs the built-in ones.
 * ui_saved_dirty returns 1 once after the list changed, then the host writes ui_saved_text again. */
void ui_saved_load(SmzUi *u, const char *text);
int ui_saved_text(const SmzUi *u, char *out, size_t cap);
int ui_saved_dirty(SmzUi *u);
/* Player profile: the Player row appears in the start screen once the host enables it. text is "name<TAB>CC<TAB>mode" (NULL: empty). */
void ui_player_enable(SmzUi *u, int on);
/* Online tables: the slot list gets a Ladder button (and Select opens it) and the host answers ACT_LADDER by filling the three
 * tables: ui_ladder_begin(tab, label, ok) starts one (ok 0 marks it as failed) and ui_ladder_add appends its rows, best first. */
void ui_ladder_enable(SmzUi *u, int on);
void ui_ladder_begin(SmzUi *u, int tab, const char *label, int ok);
void ui_ladder_add(SmzUi *u, int tab, const char *name, const char *country, int64_t ms);
/* Fills a table from the server's /api/ladder answer ({"month"|"week":"...","runs":[{"player","country","timeMs"}]}); NULL marks it failed. */
void ui_ladder_load_json(SmzUi *u, int tab, const char *json);
/* Sets the canvas width (clamped to 320..432); the layouts follow it. */
void ui_set_width(int w);
void ui_set_height(int h);
/* The world goals of the month from the server's /api/goals answer (NULL: no connection). */
void ui_world_load_json(SmzUi *u, const char *json);
/* The world goals are on screen (the ladder's World tab, or the run list's row under the Daily one): the host refreshes them now and then. */
int ui_world_showing(const SmzUi *u);
void ui_player_load(SmzUi *u, const char *text);
int ui_player_text(const SmzUi *u, char *out, size_t cap);
int ui_player_dirty(SmzUi *u);
/* 1 when the run being started uploads to the online ladder (read when the host starts generating). */
int ui_online_now(const SmzUi *u);
/* The run's rules are the randomizer's own (looks and tracker aids aside): only such a run, or a monthly or weekly one, goes to the online ladder. */
int ui_is_default(const SmzUi *u);
int ui_run_ladder(const SmzUi *u);
/* The daily challenge: once enabled, the run list has a Daily row on top and a token counter by the Ladder button. The host
 * hands in each /api/daily answer (ui_daily_set) and says whether it keeps an attempt of the open window (Continue instead
 * of Start); Start or Continue comes back as ACT_DAILY. */
void ui_daily_enable(SmzUi *u, int on);
void ui_daily_set(SmzUi *u, const SmzDailyInfo *d, int has_run);
/* Sets the options from a run's settings JSON (seed, rules, logic, start items), to build the run the server made. */
void ui_apply_run_settings(SmzUi *u, const char *settings_json);
/* The month's fixed seed; once set, the run mode list offers "Monthly seed" (Original rules with that seed). */
void ui_set_monthly(SmzUi *u, const char *label, int seed, int days_left);
/* Asks which to load for a slot whose save state is ahead of its game save (times in play-time ms, counts of checks done);
 * the answer comes back as ACT_LOAD_SAVE or ACT_LOAD_STATE with the slot in action_slot. */
void ui_load_choice(SmzUi *u, int slot, int64_t save_ms, int save_z, int save_m, int64_t state_ms, int state_z, int state_m, int zelda_total, int metroid_total);
/* Sets every option as the given way of starting a run does (0 Cozy, 1 Original, 2 Custom defaults), without opening any screen. */
void ui_preset(SmzUi *u, int mode);
/* Applies "Key=value,Key=value" on top of the current options (the format of saved customs; Seed is set through value[0]). */
void ui_apply_spec(SmzUi *u, const char *spec);
/* The week's fixed seed; once set, the run mode list offers the Weekly run (limited goals, own ladder). */
void ui_set_weekly(SmzUi *u, const char *label, int seed, int days_left);
int ui_run_weekly(const SmzUi *u);
/* The option values a run of the current mode must keep to count as rules-unchanged (defaults, except what the weekly run limits). */
void ui_expected_values(const SmzUi *u, int *want);
/* 1 when the run being started is the monthly seed run. */
int ui_run_monthly(const SmzUi *u);
/* The name of the way the run was set up ("Cozy run", "Original", a saved custom's name), for naming the run being created. */
const char *ui_run_label(const SmzUi *u);
/* The start menu: a logo for a fixed 2 seconds, then Start / Load game, the online modes and Options; B on the run list comes back
 * to it. mask holds the UI_OPT_* rows the host opens itself: picking one comes back as ACT_OPTIONS with the row in action_slot.
 * The Player row is there whenever ui_player_enable is on. ui_home_open shows the logo; the host calls ui_tick every frame. */
enum { UI_OPT_BUTTONS = 1, UI_OPT_VIDEO = 2, UI_OPT_SOUND = 4, UI_OPT_QUIT = 8 };
void ui_home_enable(SmzUi *u, int mask);
void ui_home_open(SmzUi *u);
void ui_tick(SmzUi *u, int ms);
void ui_busy(SmzUi *u, const char *msg);
void ui_idle(SmzUi *u);
/* Rebuilds u->cmd. */
void ui_draw(SmzUi *u);

/* Writes the settings JSON the frontends store with a run; returns its length or -1 when cap is too small. */
int ui_settings_json(const SmzUi *u, int32_t seed_if_random, char *out, size_t cap);

#endif
