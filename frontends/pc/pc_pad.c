#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif
#include "pc_emu.h"
#include "pc_pad.h"
#include "pc_store.h"

static int code_of[PA_COUNT];
static int handheld_labels;
static SDL_GameController *the_pad;   /* the pad in use, for the buttons it has */

static const int DEFAULTS[PA_COUNT] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    -1, -1, -1, -1,   /* quick states: free by default, Select + R / L / D-pad also work while playing */
    PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    -1,   /* side panels: H on the keyboard */
};
static const char *const NAMES[PA_COUNT] = {"SNES B", "SNES A", "SNES Y", "SNES X", "SNES L", "SNES R", "Start", "Select", "Tracker", "Leave run", "Fast forward", "Save state", "Load state", "State slot -", "State slot +", "Rewind", "Side panels"};
static const int SNES_BIT[8] = {PAD_B, PAD_A, PAD_Y, PAD_X, PAD_L, PAD_R, PAD_START, PAD_SELECT};
static const SDL_Keycode KEY_DEFAULTS[PA_COUNT] = {SDLK_z, SDLK_x, SDLK_a, SDLK_s, SDLK_q, SDLK_w, SDLK_RETURN, SDLK_BACKSPACE,
    [PA_TRACKER] = SDLK_TAB, [PA_LEAVE] = SDLK_ESCAPE, [PA_FAST] = SDLK_SPACE, [PA_QSAVE] = SDLK_F5, [PA_QLOAD] = SDLK_F7,
    [PA_QPREV] = SDLK_F6, [PA_QNEXT] = SDLK_F8, [PA_REWIND] = SDLK_r, [PA_PANELS] = SDLK_h};
static SDL_Keycode key_of[PA_COUNT];

int pad_keyed(int action) { return action >= 0 && action < PA_COUNT; }

/* The keys the game screen keeps for itself while playing (pc_main.c: the event loop). The tracker's own keys (E, P, F, D, H,
 * the zoom) only act while it is open, when the game takes no input, so a button may share them. */
static const struct { SDL_Keycode k; const char *use; } FIXED_KEYS[] = {
    {SDLK_UP, "the D-pad"}, {SDLK_DOWN, "the D-pad"}, {SDLK_LEFT, "the D-pad"}, {SDLK_RIGHT, "the D-pad"},
    {SDLK_F11, "full screen"},
};

static void keys_save(void)
{
    char text[512];
    int len = 0;
    for (int a = 0; a < PA_COUNT; a++) if (pad_keyed(a)) len += snprintf(text + len, sizeof text - (size_t)len, "%d=%d\n", a, (int)key_of[a]);
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
        if (sscanf(line, "%d=%d", &a, &k) == 2 && pad_keyed(a)) key_of[a] = (SDL_Keycode)k;
    }
    free(text);
}

SDL_Keycode pad_key(int action) { return pad_keyed(action) ? key_of[action] : SDLK_UNKNOWN; }
void pad_set_key(int action, SDL_Keycode k) { if (pad_keyed(action)) { key_of[action] = k; keys_save(); } }

const char *pad_key_label(int action)
{
    static char names[PA_COUNT][24];
    if (action < 0 || action >= PA_COUNT) return "-";
    snprintf(names[action], sizeof names[action], "%s", key_of[action] != SDLK_UNKNOWN ? SDL_GetKeyName(key_of[action]) : "-");
    return names[action];
}

int pad_key_action(SDL_Keycode k)
{
    for (int a = 0; a < PA_COUNT; a++) if (pad_keyed(a) && key_of[a] == k) return a;
    return -1;
}

const char *pad_key_use(SDL_Keycode k)
{
    for (size_t i = 0; i < sizeof FIXED_KEYS / sizeof FIXED_KEYS[0]; i++) if (FIXED_KEYS[i].k == k) return FIXED_KEYS[i].use;
    for (int a = 0; a < PA_COUNT; a++) if (pad_keyed(a) && key_of[a] == k) return NAMES[a];
    return NULL;
}

void pad_cfg_reset(void)
{
    memcpy(code_of, DEFAULTS, sizeof code_of);
    memcpy(key_of, KEY_DEFAULTS, sizeof key_of);
    if (handheld_labels) {   /* SDL's names are the printed letters here, so each SNES button is the one with its letter */
        code_of[PA_B] = SDL_CONTROLLER_BUTTON_B; code_of[PA_A] = SDL_CONTROLLER_BUTTON_A;
        code_of[PA_Y] = SDL_CONTROLLER_BUTTON_Y; code_of[PA_X] = SDL_CONTROLLER_BUTTON_X;
    }
    if (the_pad && !SDL_GameControllerHasButton(the_pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK) && SDL_GameControllerHasButton(the_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)) {
        code_of[PA_TRACKER] = SDL_CONTROLLER_BUTTON_LEFTSTICK;   /* one stick (the RG 40XXV): the tracker on L3, leaving on the menu button */
        code_of[PA_LEAVE] = SDL_GameControllerHasButton(the_pad, SDL_CONTROLLER_BUTTON_GUIDE) ? SDL_CONTROLLER_BUTTON_GUIDE : -1;
    }
}

void pad_set_device(SDL_GameController *c) { the_pad = c; }

int pad_handheld_labels(void) { return handheld_labels; }

/* A handheld whose own buttons a service hands over as a virtual DualSense (InputPlumber on ARM boards like the RG DS): its
 * buttons carry Nintendo letters, not PlayStation shapes. A real Sony pad plugged in is a USB or Bluetooth device instead. */
int pad_virtual_handheld(void)
{
#ifdef __linux__
    static int known = -1;
    if (known >= 0) return known;
    known = 0;
    if (access("/sys/firmware/devicetree/base/model", F_OK)) return known;
    for (int i = 0; i < 64; i++) {
        char path[96], vendor[8] = "", real[512];
        snprintf(path, sizeof path, "/sys/class/input/event%d/device/id/vendor", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int got = fgets(vendor, sizeof vendor, f) != NULL;
        fclose(f);
        if (!got || strncmp(vendor, "054c", 4)) continue;
        snprintf(path, sizeof path, "/sys/class/input/event%d", i);
        if (!realpath(path, real)) continue;
        if (!strstr(real, "/devices/virtual/")) return known = 0;
        known = 1;
    }
    return known;
#else
    return 0;
#endif
}

/* A pad built into a Linux handheld (bus 0x19) has Nintendo's letters, and the mappings in gamecontrollerdb mix letters and
 * positions from one device to the next. The kernel's button order is reliable (south, east, north, west), so that is used. */
#ifdef __linux__
/* The SDL button of each evdev key code of the input device with this name: SDL numbers the joystick range first. */
static int sdl_button_of(const char *name, int code)
{
    for (int i = 0; i < 64; i++) {
        char path[96], line[512] = "";
        snprintf(path, sizeof path, "/sys/class/input/event%d/device/name", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int got = fgets(line, sizeof line, f) != NULL;
        fclose(f);
        line[strcspn(line, "\n")] = 0;
        if (!got || strcmp(line, name)) continue;
        snprintf(path, sizeof path, "/sys/class/input/event%d/device/capabilities/key", i);
        if (!(f = fopen(path, "r"))) return -1;
        got = fgets(line, sizeof line, f) != NULL;
        fclose(f);
        if (!got) return -1;
        unsigned long long w[16];
        int n = 0;
        for (char *t = strtok(line, " \n"); t && n < 16; t = strtok(NULL, " \n")) w[n++] = strtoull(t, NULL, 16);
        if (!n) return -1;
        int idx = 0;   /* the words come most significant first */
        for (int pass = 0; pass < 2; pass++)
            for (int k = pass ? 0 : 0x120; k < (pass ? 0x120 : n * 64); k++) {
                int wi = n - 1 - k / 64;
                if (wi < 0 || !((w[wi] >> (k % 64)) & 1)) continue;
                if (k == code) return idx;
                idx++;
            }
        return -1;
    }
    return -1;
}

/* Batocera and Knulli ship each handheld's pad in es_input.cfg with the evdev code of every button, A B X Y as the printed
 * letters; their drivers do not all follow the kernel's south, east, north, west, and L2 / R2 may be plain buttons. Writes
 * the SDL fields it knows ("a:b0,b:b1,lefttrigger:b10,...") and returns how many, 0 when the device is not listed. */
static int es_buttons(const char *name, char *out, size_t cap)
{
    static const char *const FILES[] = {"/userdata/system/configs/emulationstation/es_input.cfg", "/usr/share/emulationstation/es_input.cfg"};
    static const char *const ES[] = {"a", "b", "x", "y", "pageup", "pagedown", "select", "start", "hotkey", "l3", "r3", "l2", "r2"};
    static const char *const SDLN[] = {"a", "b", "x", "y", "leftshoulder", "rightshoulder", "back", "start", "guide", "leftstick", "rightstick", "lefttrigger", "righttrigger"};
    char want[160];
    snprintf(want, sizeof want, "deviceName=\"%s\"", name);
    out[0] = 0;
    for (int f = 0; f < 2; f++) {
        FILE *fp = fopen(FILES[f], "r");
        if (!fp) continue;
        char line[512];
        int in = 0, n = 0;
        while (fgets(line, sizeof line, fp)) {
            if (!in) { in = strstr(line, want) != NULL; continue; }
            if (strstr(line, "</inputConfig>")) break;
            char *c = strstr(line, "code=\"");
            if (!c || !strstr(line, "type=\"button\"")) continue;
            for (int k = 0; k < 13; k++) {
                char key[24];
                snprintf(key, sizeof key, "name=\"%s\"", ES[k]);
                int b;
                if (!strstr(line, key) || (b = sdl_button_of(name, atoi(c + 6))) < 0) continue;
                size_t l = strlen(out);
                snprintf(out + l, cap - l, "%s:b%d,", SDLN[k], b);
                n++;
            }
        }
        fclose(fp);
        if (in) return n;
    }
    return 0;
}
#endif

void pad_handheld_setup(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        char *map = SDL_JoystickGetDeviceGUID(i).data[0] == 0x19 ? SDL_GameControllerMappingForDeviceIndex(i) : NULL;
        if (!map) continue;
        char known[512] = "", out[1024] = "";
#ifdef __linux__
        const char *jn = SDL_JoystickNameForIndex(i);
        if (jn) es_buttons(jn, known, sizeof known);
#endif
        if (!known[0]) snprintf(known, sizeof known, "a:b1,b:b0,x:b2,y:b3,");
        int field = 0;
        for (char *tok = strtok(map, ","); tok; tok = strtok(NULL, ","), field++) {
            char *colon = strchr(tok, ':'), key[40];
            if (field >= 2 && colon && colon - tok < (int)sizeof key - 2) {   /* a field the list above sets is left out */
                snprintf(key, sizeof key, ",%.*s:", (int)(colon - tok), tok);
                char hay[520];
                snprintf(hay, sizeof hay, ",%s", known);
                if (strstr(hay, key)) continue;
            }
            strncat(out, tok, sizeof out - strlen(out) - 2);
            strcat(out, ",");
        }
        strncat(out, known, sizeof out - strlen(out) - 1);
        SDL_free(map);
        if (SDL_GameControllerAddMapping(out) >= 0) handheld_labels = 1;
    }
}

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
    if (handheld_labels && code == SDL_CONTROLLER_BUTTON_BACK) return "Select";
    if (handheld_labels && code == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) return "L1";
    if (handheld_labels && code == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) return "R1";
    if (code >= PAD_AXIS_BASE) {
        int axis = code - PAD_AXIS_BASE;
        if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) return handheld_labels ? "L2" : "LT";
        if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return handheld_labels ? "R2" : "RT";
        snprintf(other, sizeof other, "Axis %d", axis);
        return other;
    }
    if (code >= 0 && code < (int)(sizeof BUTTON / sizeof BUTTON[0])) return BUTTON[code];
    snprintf(other, sizeof other, "Button %d", code);
    return other;
}

const char *pad_action_icon(int action)
{
    static const char *const ICON[] = {"A", "B", "X", "Y", "SELECT", NULL, "START", "L3", "R3", "L", "R", "UP", "DOWN", "LEFT", "RIGHT"};
    int code = action >= 0 && action < PA_COUNT ? code_of[action] : -1;
    if (code < 0) return NULL;
    if (code == PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT) return "LT";
    if (code == PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return "RT";
    if (code < (int)(sizeof ICON / sizeof ICON[0]) && ICON[code]) return ICON[code];
    return pad_code_name(code);
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

static int hidden[2];   /* the codes in use by a hotkey: buttons, and triggers from PAD_AXIS_BASE */
void pad_hide(int code, int on)
{
    int *set = &hidden[code >= PAD_AXIS_BASE], bit = 1 << (code >= PAD_AXIS_BASE ? code - PAD_AXIS_BASE : code);
    *set = on ? *set | bit : *set & ~bit;
}
static int is_hidden(int code) { return code >= 0 && (hidden[code >= PAD_AXIS_BASE] >> (code >= PAD_AXIS_BASE ? code - PAD_AXIS_BASE : code) & 1); }

int pad_snes_bits(SDL_GameController *c)
{
    if (!c) return 0;
    int b = 0, d = pad_dirs(c);
    if (d & 1) b |= PAD_UP;
    if (d & 2) b |= PAD_DOWN;
    if (d & 4) b |= PAD_LEFT;
    if (d & 8) b |= PAD_RIGHT;
    for (int a = 0; a < 8; a++) if (pad_held(c, code_of[a]) && !is_hidden(code_of[a])) b |= SNES_BIT[a];
    return b;
}
