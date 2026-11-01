/* Item shortcuts on PC: a keyboard key and a controller button per ALttP item (equips it on Y) or Super Metroid beam
 * (turns it on or off), kept in shortcuts.cfg and set from the settings screen. */
#ifndef PC_SHORTCUTS_H
#define PC_SHORTCUTS_H

#include <SDL.h>

void sc_cfg_load(void);
void sc_cfg_save(void);
/* The key (SDLK_UNKNOWN for none) and the controller code (pad_event_code's, -1 for none) of shortcut id. */
SDL_Keycode sc_key(int id);
int sc_pad(int id);
void sc_clear(int id);
/* Binds a key or a controller code to id; returns what already uses it instead, or NULL when bound. */
const char *sc_bind_key(int id, SDL_Keycode k);
const char *sc_bind_pad(int id, int code);
/* A press while playing: makes the writes of the shortcut bound to it, if any. Returns 1 when the press was a shortcut. */
int sc_key_press(SDL_Keycode k);
int sc_pad_press(int code);

#endif
