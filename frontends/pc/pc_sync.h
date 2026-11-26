/* Cloud sync on the PC (docs/PLAN_ROMM_SYNC.md): the runs, saves and states of every game kept in RomM. */
#ifndef PC_SYNC_H
#define PC_SYNC_H

#include <stddef.h>
#include "smz3_run.h"
#include "smz3_ui.h"

/* Reads the sign-in, turns the Options row on and, once signed in, brings the runs made on other devices into free slots. */
void sync_init(SmzUi *u);
/* Each frame: shows what the background calls finished. Returns 1 when the run list changed (the host refills the slots). */
int sync_poll(SmzUi *u);
/* ACT_CLOUD. present redraws the window while a call blocks. Returns the slot (0-based) to start playing, or -1. */
int sync_action(SmzUi *u, int what, void (*present)(void));
/* Before a run starts: brings RomM's newer files down. Returns 0 to play, 1 when it must not start now (a conflict to choose). */
int sync_open(SmzUi *u, int slot, void (*present)(void));
/* After a run: sends what changed up, in the background. */
void sync_left(int slot);
/* SMZ3's emulator wrote a run file (SMZ_SYNC_* kind). */
void sync_own(int slot, int kind, const void *data, size_t len);
/* A run is being deleted here: it goes from RomM too. Call before the local files go. */
void sync_deleted(int slot);
/* At exit: waits a little for the uploads still running. */
void sync_quit(void);

#endif
