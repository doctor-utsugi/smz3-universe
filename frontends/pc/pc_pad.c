#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_emu.h"
#include "pc_pad.h"
#include "pc_store.h"

static int code_of[PA_COUNT];

static const int DEFAULTS[PA_COUNT] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    -1, -1, -1, -1,   /* quick states: free by default, Select + R / L / D-pad also work while playing */
};
static const char *const NAMES[PA_COUNT] = {"SNES B", "SNES A", "SNES Y", "SNES X", "SNES L", "SNES R", "Start", "Select", "Tracker", "Leave run", "Fast forward", "Save state", "Load state", "State slot -", "State slot +"};
static const int SNES_BIT[8] = {PAD_B, PAD_A, PAD_Y, PAD_X, PAD_L, PAD_R, PAD_START, PAD_SELECT};
static const SDL_Keycode KEY_DEFAULTS[8] = {SDLK_z, SDLK_x, SDLK_a, SDLK_s, SDLK_q, SDLK_w, SDLK_RETURN, SDLK_BACKSPACE};
static SDL_Keycode key_of[8];

/* The keys the game screen keeps for itself (pc_main.c: the event loop). */
static const struct { SDL_Keycode k; const char *use; } FIXED_KEYS[] = {
    {SDLK_UP, "the SNES D-pad"}, {SDLK_DOWN, "the SNES D-pad"}, {SDLK_LEFT, "the SNES D-pad"}, {SDLK_RIGHT, "the SNES D-pad"},
    {SDLK_TAB, "the tracker"}, {SDLK_ESCAPE, "leaving the run"}, {SDLK_SPACE, "fast forward"}, {SDLK_F5, "save state"}, {SDLK_F7, "load state"},
    {SDLK_F6, "the state slot"}, {SDLK_F8, "the state slot"}, {SDLK_F9, "the aspect ratio"}, {SDLK_F11, "full screen"}, {SDLK_F1, "the settings"},
    {SDLK_v, "the video panel"}, {SDLK_e, "the tracker's tabs"}, {SDLK_p, "the tracker's pause"}, {SDLK_f, "the map's follow"},
    {SDLK_d, "the dungeon maps"}, {SDLK_h, "the dungeon maps"}, {SDLK_EQUALS, "the map's zoom"}, {SDLK_MINUS, "the map's zoom"},
    {SDLK_KP_PLUS, "the map's zoom"}, {SDLK_KP_MINUS, "the map's zoom"}, {SDLK_PAGEUP, "the map's zoom"}, {SDLK_PAGEDOWN, "the map's zoom"},
};

static void keys_save(void)
{
    char text[160];
    int len = 0;
    for (int a = 0; a < 8; a++) len += snprintf(text + len, sizeof text - (size_t)len, "%d=%d\n", a, (int)key_of[a]);
    store_write(store_path("keys.cfg"), text, (size_t)len);
}

static void keys_load(void)
{
    memcpy(key_of, KEY_DEFAULTS, sizeof key_of);
    size_t n;
    char *text = (char *)store_read(store_path("keys.cfg"), &n);
    if (!text) return;
    text = realloc(text, n + 1);
    text[n] = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        int a, k;
        if (sscanf(line, "%d=%d", &a, &k) == 2 && a >= 0 && a < 8) key_of[a] = (SDL_Keycode)k;
    }
    free(text);
}

SDL_Keycode pad_key(int action) { return action >= 0 && action < 8 ? key_of[action] : SDLK_UNKNOWN; }
void pad_set_key(int action, SDL_Keycode k) { if (action >= 0 && action < 8) { key_of[action] = k; keys_save(); } }

const char *pad_key_use(SDL_Keycode k)
{
    for (size_t i = 0; i < sizeof FIXED_KEYS / sizeof FIXED_KEYS[0]; i++) if (FIXED_KEYS[i].k == k) return FIXED_KEYS[i].use;
    for (int a = 0; a < 8; a++) if (key_of[a] == k) return NAMES[a];
    return NULL;
}

void pad_cfg_reset(void) { memcpy(code_of, DEFAULTS, sizeof code_of); memcpy(key_of, KEY_DEFAULTS, sizeof key_of); }

void pad_cfg_load(void)
{
    pad_cfg_reset();
    keys_load();
    size_t n;
    char *text = (char *)store_read(store_path("pad.cfg"), &n);
    if (!text) return;
    text = realloc(text, n + 1);
    text[n] = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        int a, c;
        if (sscanf(line, "%d=%d", &a, &c) == 2 && a >= 0 && a < PA_COUNT && c >= -1 && c < PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_MAX) code_of[a] = c;
    }
    free(text);
}

void pad_cfg_save(void)
{
    char text[256];
    int len = 0;
    for (int a = 0; a < PA_COUNT; a++) len += snprintf(text + len, sizeof text - (size_t)len, "%d=%d\n", a, code_of[a]);
    store_write(store_path("pad.cfg"), text, (size_t)len);
    keys_save();
}

int pad_code(int action) { return code_of[action]; }
void pad_set(int action, int code) { code_of[action] = code; }
const char *pad_action_name(int action) { return NAMES[action]; }

const char *pad_code_name(int code)
{
    static const char *const BUTTON[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB", "Up", "Down", "Left", "Right", "Share", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad"};
    static char other[24];
    if (code < 0) return "None";
    if (code >= PAD_AXIS_BASE) {
        int axis = code - PAD_AXIS_BASE;
        if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) return "LT";
        if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return "RT";
        snprintf(other, sizeof other, "Axis %d", axis);
        return other;
    }
    if (code >= 0 && code < (int)(sizeof BUTTON / sizeof BUTTON[0])) return BUTTON[code];
    snprintf(other, sizeof other, "Button %d", code);
    return other;
}

int pad_held(SDL_GameController *c, int code)
{
    if (!c || code < 0) return 0;
    if (code >= PAD_AXIS_BASE) return SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)(code - PAD_AXIS_BASE)) > 16000;
    return SDL_GameControllerGetButton(c, (SDL_GameControllerButton)code) != 0;
}

int pad_event_code(const SDL_Event *e)
{
    static int trigger_down[SDL_CONTROLLER_AXIS_MAX];
    if (e->type == SDL_CONTROLLERBUTTONDOWN) return e->cbutton.button;
    if (e->type == SDL_CONTROLLERAXISMOTION && e->caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
        int on = e->caxis.value > 16000, was = trigger_down[e->caxis.axis];
        trigger_down[e->caxis.axis] = on;
        if (on && !was) return PAD_AXIS_BASE + e->caxis.axis;
    }
    return -1;
}

int pad_dirs(SDL_GameController *c)
{
    if (!c) return 0;
    int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY), d = 0;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -16000) d |= 1;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 16000) d |= 2;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -16000) d |= 4;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > 16000) d |= 8;
    return d;
}

int pad_snes_bits(SDL_GameController *c)
{
    if (!c) return 0;
    int b = 0, d = pad_dirs(c);
    if (d & 1) b |= PAD_UP;
    if (d & 2) b |= PAD_DOWN;
    if (d & 4) b |= PAD_LEFT;
    if (d & 8) b |= PAD_RIGHT;
    for (int a = 0; a < 8; a++) if (pad_held(c, code_of[a])) b |= SNES_BIT[a];
    return b;
}
