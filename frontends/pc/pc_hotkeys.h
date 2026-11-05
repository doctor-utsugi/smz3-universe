/* The Thor's hotkeys on the pad in dual-screen mode: a combo of one or two buttons per action, kept in hotkeys.cfg and set from the tracker's CONFIG tab. */
#ifndef PC_HOTKEYS_H
#define PC_HOTKEYS_H

#include <SDL.h>

enum { HK_SAVE, HK_LOAD, HK_FAST, HK_NEXT, HK_REWIND, HK_LEAVE, HK_COUNT };

void hk_load(void);
void hk_reset(void);
const char *hk_title(int action);
/* The name of the first (which 0) or second button of action; the second can be "none". */
const char *hk_name(int action, int which);
/* Taps on the CONFIG tab: moves that button on to the next one. */
void hk_cycle(int action, int which);
/* A press of code (pad_event_code's): the action it completes, or -1. The combo's buttons stop reaching the game until released. */
int hk_press(SDL_GameController *c, int code);
int hk_held(SDL_GameController *c, int action);
/* Once a frame: gives back to the game the buttons of a combo that were let go. */
void hk_tick(SDL_GameController *c);

#endif
