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
/* Makes the writes of shortcut id: a tap on the tracker's screen. */
void sc_act(int id);
/* The Thor's rows on the tracker's screen (dual-screen mode): five ALttP slots (-1 empty), and whether each row shows. */
#define SC_SLOTS 5
int sc_slot(int i);
void sc_set_slot(int i, int id);
int sc_row_shown(void);
int sc_weapon_row_shown(void);
void sc_toggle_row(void);
void sc_toggle_weapon_row(void);

#endif
