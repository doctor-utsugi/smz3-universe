/* The run's helps as on the Thor (helpMode): the hints asked for and the checks revealed, kept beside the run as runs/slotN.help. */
#ifndef PC_HINTS_H
#define PC_HINTS_H

#include <stdint.h>
#include "smz3_run.h"

enum { HELP_ALL, HELP_LIMITED, HELP_NONE };

void hints_open(int slot, const SmzRun *r);
/* Waits for a hint on its way: the generator's tables are not shared with what runs after the game. */
void hints_close(void);
int hints_mode(void);
int hints_total(void);
int hints_left(void);
int hints_can_ask(void);
int hints_count(void);
const char *hints_text(int i);
/* The texts are built on their own thread the first time: while busy the seed's tables are in use, so nothing else may ask the generator. */
int hints_busy(void);
void hints_ask(void);
/* A hint asked for has come: returns 1 and says so in note (cap bytes). */
int hints_poll(char *note, size_t cap);
/* Shows the pending checks of a region and spends a help in the limited mode. */
void hints_reveal(const int *locations, int n);
/* The checks that may be drawn while pending: NULL for all of them. */
const uint8_t *hints_visible(void);
int hints_fixed_keys(void);
void hints_set_fixed_keys(int on);

#endif
