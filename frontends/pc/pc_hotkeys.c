#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_hotkeys.h"
#include "pc_pad.h"
#include "pc_store.h"

#define L2 (PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT)
#define R2 (PAD_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT)

static const char *const TITLES[HK_COUNT] = {"Save state", "Load state", "Fast forward", "Next save slot", "Rewind", "Leave the run"};
static const int DEFAULTS[HK_COUNT][2] = {
    {L2, SDL_CONTROLLER_BUTTON_START}, {L2, SDL_CONTROLLER_BUTTON_BACK}, {R2, -1},
    {L2, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER}, {L2, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START}};
/* The order the config tab cycles through them. */
static const struct { int code; const char *name; } BUTTONS[] = {
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "L1"}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "R1"}, {L2, "L2"}, {R2, "R2"},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, "L3"}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, "R3"}, {SDL_CONTROLLER_BUTTON_BACK, "Select"},
    {SDL_CONTROLLER_BUTTON_START, "Start"}, {SDL_CONTROLLER_BUTTON_A, "A"}, {SDL_CONTROLLER_BUTTON_B, "B"},
    {SDL_CONTROLLER_BUTTON_X, "X"}, {SDL_CONTROLLER_BUTTON_Y, "Y"}};
#define N_BUTTONS (int)(sizeof BUTTONS / sizeof BUTTONS[0])

static int keys[HK_COUNT][2];
static int taken[N_BUTTONS];

static int index_of(int code)
{
    for (int i = 0; i < N_BUTTONS; i++) if (BUTTONS[i].code == code) return i;
    return -1;
}

static void save(void)
{
    char text[HK_COUNT * 32];
    int len = 0;
    for (int a = 0; a < HK_COUNT; a++) len += snprintf(text + len, sizeof text - (size_t)len, "%d=%d,%d\n", a, keys[a][0], keys[a][1]);
    store_write(store_path("hotkeys.cfg"), text, (size_t)len);
}

void hk_load(void)
{
    memcpy(keys, DEFAULTS, sizeof keys);
    size_t n;
    char *text = (char *)store_read(store_path("hotkeys.cfg"), &n);
    if (!text) return;
    text = realloc(text, n + 1);
    text[n] = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        int a, k1, k2;
        if (sscanf(line, "%d=%d,%d", &a, &k1, &k2) == 3 && a >= 0 && a < HK_COUNT && index_of(k1) >= 0 && (k2 < 0 || index_of(k2) >= 0)) { keys[a][0] = k1; keys[a][1] = k2; }
    }
    free(text);
}

void hk_reset(void) { memcpy(keys, DEFAULTS, sizeof keys); save(); }

const char *hk_title(int action) { return TITLES[action]; }

const char *hk_name(int action, int which)
{
    int i = index_of(keys[action][which]);
    return i < 0 ? "none" : BUTTONS[i].name;
}

void hk_cycle(int action, int which)
{
    int *k = keys[action], next = index_of(k[which]) + 1;
    if (which == 0) {
        k[0] = BUTTONS[next % N_BUTTONS].code;
        if (k[1] == k[0]) k[1] = -1;
    } else k[1] = next >= N_BUTTONS || BUTTONS[next].code == k[0] ? -1 : BUTTONS[next].code;
    save();
}

int hk_held(SDL_GameController *c, int action)
{
    const int *k = keys[action];
    return pad_held(c, k[0]) && (k[1] < 0 || pad_held(c, k[1]));
}

int hk_press(SDL_GameController *c, int code)
{
    int best = -1, size = 0;
    for (int a = 0; a < HK_COUNT; a++) {
        int n = keys[a][1] < 0 ? 1 : 2;
        if ((code == keys[a][0] || code == keys[a][1]) && hk_held(c, a) && n > size) { best = a; size = n; }
    }
    for (int w = 0; best >= 0 && w < size; w++) {
        int i = index_of(keys[best][w]);
        if (!taken[i]) { taken[i] = 1; pad_hide(keys[best][w], 1); }
    }
    return best;
}

void hk_tick(SDL_GameController *c)
{
    for (int i = 0; i < N_BUTTONS; i++)
        if (taken[i] && !pad_held(c, BUTTONS[i].code)) { taken[i] = 0; pad_hide(BUTTONS[i].code, 0); }
}
