/* The game controller: which button does what while playing, kept in pad.cfg in the data folder. Directions are the D-pad and the left
 * stick; everything else can be remapped. A button code is an SDL_GameControllerButton, or PAD_AXIS_BASE + an SDL axis for the triggers. */
#ifndef PC_PAD_H
#define PC_PAD_H

#include <SDL.h>

enum { PA_B, PA_A, PA_Y, PA_X, PA_L, PA_R, PA_START, PA_SELECT, PA_TRACKER, PA_LEAVE, PA_FAST, PA_QSAVE, PA_QLOAD, PA_QPREV, PA_QNEXT, PA_COUNT };
#define PAD_AXIS_BASE 100

void pad_cfg_load(void);
void pad_cfg_save(void);
void pad_cfg_reset(void);
int pad_code(int action);
void pad_set(int action, int code);
const char *pad_action_name(int action);
const char *pad_code_name(int code);
/* The keyboard: the key of an SNES button (PA_B .. PA_SELECT), kept in keys.cfg; SDLK_UNKNOWN when it has none or for the other actions. */
SDL_Keycode pad_key(int action);
void pad_set_key(int action, SDL_Keycode k);
/* What the game screen uses a key for (an SNES button or one of the fixed keys), NULL when it is free. */
const char *pad_key_use(SDL_Keycode k);

/* True while the button or trigger of that code is held / the event is a press of it. */
int pad_held(SDL_GameController *c, int code);
/* The code a controller event presses (a button down, or a trigger crossing its threshold), -1 for any other event. */
int pad_event_code(const SDL_Event *e);
/* The SNES joypad bits (PAD_* of pc_emu.h) for the buttons held now, including the D-pad and the left stick. */
int pad_snes_bits(SDL_GameController *c);
/* Directions held: bit 0 up, 1 down, 2 left, 3 right (D-pad or left stick). */
int pad_dirs(SDL_GameController *c);

#endif
