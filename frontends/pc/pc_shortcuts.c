#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_emu.h"
#include "pc_pad.h"
#include "pc_shortcuts.h"
#include "pc_store.h"
#include "smz3_shortcuts.h"

static SDL_Keycode key_of[SC_COUNT];
static int pad_of[SC_COUNT];

void sc_cfg_load(void)
{
    for (int i = 0; i < SC_COUNT; i++) { key_of[i] = SDLK_UNKNOWN; pad_of[i] = -1; }
    size_t n;
    char *text = (char *)store_read(store_path("shortcuts.cfg"), &n);
    if (!text) return;
    text = realloc(text, n + 1);
    text[n] = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        int id, k, p;
        if (sscanf(line, "%d=%d,%d", &id, &k, &p) == 3 && id >= 0 && id < SC_COUNT) { key_of[id] = k; pad_of[id] = p; }
    }
    free(text);
}

void sc_cfg_save(void)
{
    char text[SC_COUNT * 32];
    int len = 0;
    for (int i = 0; i < SC_COUNT; i++)
        if (key_of[i] != SDLK_UNKNOWN || pad_of[i] >= 0) len += snprintf(text + len, sizeof text - (size_t)len, "%d=%d,%d\n", i, (int)key_of[i], pad_of[i]);
    store_write(store_path("shortcuts.cfg"), text, (size_t)len);
}

SDL_Keycode sc_key(int id) { return id >= 0 && id < SC_COUNT ? key_of[id] : SDLK_UNKNOWN; }
int sc_pad(int id) { return id >= 0 && id < SC_COUNT ? pad_of[id] : -1; }
void sc_clear(int id) { if (id >= 0 && id < SC_COUNT) { key_of[id] = SDLK_UNKNOWN; pad_of[id] = -1; sc_cfg_save(); } }

static char why[96];

const char *sc_bind_key(int id, SDL_Keycode k)
{
    if (id < 0 || id >= SC_COUNT) return "nothing";
    const char *use = pad_key_use(k);
    if (use) { snprintf(why, sizeof why, "%s is used by %s", SDL_GetKeyName(k), use); return why; }
    for (int i = 0; i < SC_COUNT; i++)
        if (i != id && key_of[i] == k) { snprintf(why, sizeof why, "%s is used by %s", SDL_GetKeyName(k), smz_shortcuts[i].name); return why; }
    key_of[id] = k;
    sc_cfg_save();
    return NULL;
}

const char *sc_bind_pad(int id, int code)
{
    if (id < 0 || id >= SC_COUNT) return "nothing";
    for (int a = 0; a < PA_COUNT; a++)
        if (pad_code(a) == code) { snprintf(why, sizeof why, "%s is used by %s", pad_code_name(code), pad_action_name(a)); return why; }
    if (code >= SDL_CONTROLLER_BUTTON_DPAD_UP && code <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) { snprintf(why, sizeof why, "%s is used by the SNES D-pad", pad_code_name(code)); return why; }
    if (code == SDL_CONTROLLER_BUTTON_GUIDE) { snprintf(why, sizeof why, "Guide belongs to the system"); return why; }
    for (int i = 0; i < SC_COUNT; i++)
        if (i != id && pad_of[i] == code) { snprintf(why, sizeof why, "%s is used by %s", pad_code_name(code), smz_shortcuts[i].name); return why; }
    pad_of[id] = code;
    sc_cfg_save();
    return NULL;
}

static void act(int id)
{
    size_t wl, sl;
    uint8_t *w = (uint8_t *)emu_wram(&wl);
    uint8_t *s = emu_sram(&sl);
    SmzPoke p[4];
    if (!w || !s) return;
    int n = smz_shortcut_pokes(id, w, wl, s, sl, p, 4);
    for (int i = 0; i < n; i++) if (p[i].offset < wl) w[p[i].offset] = p[i].value;
}

int sc_key_press(SDL_Keycode k)
{
    if (k == SDLK_UNKNOWN) return 0;
    for (int i = 0; i < SC_COUNT; i++) if (key_of[i] == k) { act(i); return 1; }
    return 0;
}

int sc_pad_press(int code)
{
    if (code < 0) return 0;
    for (int i = 0; i < SC_COUNT; i++) if (pad_of[i] == code) { act(i); return 1; }
    return 0;
}
