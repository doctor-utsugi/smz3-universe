/* The run-complete card: a flash, light rays and confetti, the title letter by letter, the final time counting up to its
 * value with a fanfare, then the run's details one per second and the buttons. */
#ifndef PC_FINISH_H
#define PC_FINISH_H

#include <SDL.h>
#include <stdint.h>

typedef struct {
    const char *title, *name, *ladder;
    SDL_Color ladder_color;
    int64_t ms;
    unsigned zelda, zelda_total, metroid, metroid_total;
} FinishCard;

void finish_start(void);
/* A press while the card plays: jumps to its end and returns 1; once the buttons are out it returns 0 and the press acts. */
int finish_skip(void);
void finish_draw(const FinishCard *f, int ww, int wh);
/* After the card, while the credits roll: the run's details in place of the tracker panel at x..x+w. */
void finish_panel(const FinishCard *f, float x, float w, float h);

#endif
