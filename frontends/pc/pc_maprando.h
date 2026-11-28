#ifndef PC_MAPRANDO_H
#define PC_MAPRANDO_H

#include <stddef.h>

/* Offers Map Rando when this build has it and its data folder is found. */
void maprando_setup(void);
/* 1 when this build can build Map Rando seeds itself (its data folder was found). */
int maprando_here(void);
/* Where the seeds are built, a UI_MR_* (the Map Rando seeds option): automatic decides from the memory. */
void maprando_where(int where);
/* While a Map Rando seed is built on another thread: its progress line in out and a nonzero phase; 0 otherwise. */
int maprando_progress(char *out, size_t cap);

#endif
