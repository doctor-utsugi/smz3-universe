/* The second screen of a dual-screen handheld while the run list is in front. */
#ifndef PC_IDLE_H
#define PC_IDLE_H

#include "smz3_ui.h"

/* Draws on the current gfx renderer, whose output is bw x bh pixels. */
void idle_draw(const SmzUi *ui, int bw, int bh, const char *version);
/* A tap at x, y in the screen's pixels: changes the ladder tab when it lands on one. */
void idle_tap(float x, float y);

#endif
