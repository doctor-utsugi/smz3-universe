/* The tracker's screen in dual-screen mode, the same as the AYN Thor's (Smz3Presentation.kt and friends): worked by touch alone, the pad never leaves the game. */
#ifndef PC_DUAL_H
#define PC_DUAL_H

#include <SDL.h>
#include "pc_dungeon.h"
#include "pc_finish.h"
#include "smz3_daily.h"
#include "smz3_discovery.h"
#include "smz3_run.h"
#include "smz3_tracker.h"

typedef struct {
    const SmzTracker *t;
    const SmzDiscovery *disc;
    int discovered_only;
    const uint8_t *in_logic, *wram, *sram;
    size_t wram_len, sram_len;
    const DungeonLive *live;
    const SmzDailyRun *daily;   /* the attempt of a daily run, else NULL */
    int64_t clock_ms;           /* the time to show instead of the play time (a daily's real clock), or -1 */
    const FinishCard *card;     /* the run has ended: its details take the screen */
    int card_open;              /* the top screen still shows the run-complete card */
    int states, qslots, *qslot; /* save states: allowed, how many slots, the active one */
} DualFrame;

enum { DUAL_NONE, DUAL_MENU, DUAL_CREDITS };

void dual_open(int slot, const SmzRun *run);
void dual_close(void);
/* Draws on r, which gfx_use has selected. */
void dual_draw(SDL_Renderer *r, const DualFrame *f, int w, int h);
/* Mouse (what SDL makes of a touch) and finger events of the tracker's window; returns 1 when the event was used. */
int dual_event(const SDL_Event *e, SDL_Window *win, SDL_Renderer *r);
/* What the finish buttons asked for since the last call. */
int dual_take(void);

#endif
