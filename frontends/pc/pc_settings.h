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

void settings_init(const SettingsHooks *hooks);
int settings_active(void);
void settings_open(void);
void settings_open_files(void);
/* Video options, kept in video.cfg: aspect 0 run default, 1 4:3, 2 8:7, 3 stretch, 4 pixel perfect; filter 0 sharp, 1 smooth, 2 scanlines, 3 LCD. */
int settings_aspect(void);
int settings_filter(void);
void settings_cycle_aspect(void);
void settings_cycle_filter(void);
const char *settings_aspect_name(void);
/* Reads video.cfg and applies the saved window state; call once after the hooks are set. */
void settings_video_load(void);
void settings_video_save_state(void);
/* The video rows (full screen, window size, aspect, filter, minimap side and size) for the panel over the game: the label, and the value in val. */
int settings_video_rows(void);
const char *settings_video_row(int row, char *val, size_t cap);
void settings_video_change(int row, int dir);
/* Minimap: 0 right panel, 1 left panel, 2 off; its size as a share of the panel width. */
int settings_minimap_side(void);
float settings_minimap_scale(void);
/* What a daily challenge shows over the game: 0 the window clock, 1 the own time, 2 the goal and new hints, 3 the time to the next hint. */
int settings_challenge_show(int what);
/* A menu button (from the keyboard or the controller). */
void settings_nav(UiButton b);
/* Raw events while a button is being assigned; returns 1 when the event was used. */
int settings_raw(const SDL_Event *e, int pad_code);
/* Draws the screen in a 320 x 240 area scaled by k at (ox, oy). */
void settings_draw(float k, float ox, float oy);

#endif
