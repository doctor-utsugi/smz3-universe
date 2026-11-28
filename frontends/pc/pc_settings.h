/* The settings screen of the PC app, drawn over the menu and driven by keyboard, controller or mouse: controller buttons, a file browser
 * to pick the base ROMs (no drag and drop needed), full screen and quit. */
#ifndef PC_SETTINGS_H
#define PC_SETTINGS_H

#include <SDL.h>
#include "smz3_ui.h"

typedef struct {
    const char *(*setup)(const char *path);   /* keeps the ROMs found at a path (file, zip or folder); returns a message */
    void (*note)(const char *msg);
    void (*fullscreen)(void);
    void (*quit)(void);
    int (*is_fullscreen)(void);
    void (*window_size)(int w, int h);        /* w = h = 0: the whole usable desktop */
} SettingsHooks;
/* The last row of the main settings: "Quit" on the menu, "Back to main menu" in a run (both go through hooks.quit). */
void settings_quit_label(const char *label);

void settings_init(const SettingsHooks *hooks);
int settings_active(void);
void settings_open(void);
/* Opens straight at Buttons and keys (video 0) or Video (1); Back there closes the settings. */
void settings_open_page(int video);
void settings_open_files(void);
/* Opens straight at Item shortcuts; Back there closes the settings. */
void settings_open_shortcuts(void);
/* Video options, kept in video.cfg: aspect 0 run default, 1 4:3, 2 8:7, 3 stretch, 4 pixel perfect; filter 0 sharp, 1 smooth, 2 scanlines, 3 LCD. */
int settings_aspect(void);
int settings_filter(void);
void settings_cycle_aspect(void);
void settings_cycle_filter(void);
const char *settings_aspect_name(void);
/* Reads video.cfg and applies the saved window state; call once after the hooks are set. */
void settings_video_load(void);
/* The Menu music option, kept in video.cfg with the screen settings. */
int settings_menu_music(void);
void settings_set_menu_music(int on);
int settings_seed_code(void);   /* a single game's seed code on the RUN tab and the side panel; hidden by default */
void settings_set_seed_code(int on);
int settings_mr_where(void);   /* the Map Rando seeds option, a UI_MR_* */
void settings_set_mr_where(int where);
void settings_video_save_state(void);
/* Applies the picked window size (called when leaving full screen, where a size change waits). */
void settings_apply_size(void);
/* Two windows (dual screen): the Full screen and Window size rows are left out. */
void settings_set_two_windows(int on);
/* The video rows (full screen, window size, aspect, filter, minimap side and size) for the panel over the game: the label, and the value in val. */
int settings_video_rows(void);
const char *settings_video_row(int row, char *val, size_t cap);
void settings_video_change(int row, int dir);
/* Two screens (Linux): 0 auto (a dual-screen handheld), 1 always one window, 2 game and tracker on the first two displays. */
int settings_screens(void);
/* The same, read from video.cfg before any window exists (the windows depend on it). */
int settings_screens_saved(void);
/* Fast-forward speed on the tracker's CONFIG tab (dual-screen mode): frames per frame, 2 3 4 6 or 8. */
int settings_ff_speed(void);
void settings_ff_next(void);
/* Minimap: 0 right panel, 1 left panel, 2 off; its size as a share of the panel width. */
int settings_minimap_side(void);
float settings_minimap_scale(void);
/* One window with no room for the side panels: 0 the % and the time in the game's corner, 1 the % only, 2 nothing. */
int settings_corner(void);
/* What a daily challenge shows over the game: 0 the window clock, 1 the own time, 2 the goal and new hints, 3 the time to the next hint. */
int settings_challenge_show(int what);
/* Every setting in one list, shown as rows on one screen (the Settings screen here) and as touch buttons on the tracker's
 * CONFIG tab with two screens (pc_dual): each row has its section, and settings_cfg_shown says whether it means something on
 * this screen setup and in this moment (the save slot only in a run). */
enum { CFG_SLOT, CFG_SPEED, CFG_FULLSCREEN, CFG_SIZE, CFG_ASPECT, CFG_FILTER, CFG_MINIMAP, CFG_MINIMAP_SIZE, CFG_CORNER, CFG_SCREENS,
       CFG_ROW, CFG_WEAPONS, CFG_FIXED, CFG_DAILY, CFG_COUNT = CFG_DAILY + 4 };
/* The run being played: its active save slot and how many there are (NULL out of a run). */
void settings_run(int *qslot, int qslots);
int settings_cfg_shown(int row);
const char *settings_cfg_section(int row);
/* The row's title, and its value in val. */
const char *settings_cfg_row(int row, char *val, size_t cap);
/* A line under the row on the CONFIG tab, or NULL. */
const char *settings_cfg_note(int row);
void settings_cfg_change(int row, int dir);
/* The save slot, the fast-forward speed and the aspect back to their defaults. */
void settings_cfg_reset(void);
/* A menu button (from the keyboard or the controller). */
void settings_nav(UiButton b);
/* Raw events while a button is being assigned; returns 1 when the event was used. */
int settings_raw(const SDL_Event *e, int pad_code);
/* Draws the screen in a 320 x 240 area scaled by k at (ox, oy). */
void settings_draw(float k, float ox, float oy);

#endif
