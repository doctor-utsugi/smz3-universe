/* Rewind: a ring of emulator states, each kept as the XOR with the next one and run-length packed, so a step back or
 * forward from the cursor costs one unpack. The newest state is kept whole. */
#ifndef SMZ3_REWIND_H
#define SMZ3_REWIND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmzRewind SmzRewind;

/* state_size: bytes of one state; budget: bytes the packed history may use; max_states caps the count (the time kept
 * is max_states * the frames between pushes). NULL when out of memory. */
SmzRewind *smz_rewind_new(size_t state_size, size_t budget, int max_states);
void smz_rewind_free(SmzRewind *r);
void smz_rewind_clear(SmzRewind *r);

/* Adds the state of the current frame as the newest one; drops the oldest ones over the budget. */
void smz_rewind_push(SmzRewind *r, const void *state);
int smz_rewind_count(const SmzRewind *r);

/* Browsing: open puts the cursor on the newest state; step moves it by delta (clamped) and returns the index it
 * reached, 0 the oldest; the cursor's state is in smz_rewind_state. */
int smz_rewind_open(SmzRewind *r);
int smz_rewind_step(SmzRewind *r, int delta);
int smz_rewind_cursor(const SmzRewind *r);
const void *smz_rewind_state(const SmzRewind *r);
/* Playing on from the cursor: the states after it are dropped and the cursor's becomes the newest. */
void smz_rewind_cut(SmzRewind *r);

/* The rewind screen's sounds as mono 16-bit PCM at [rate]: opening (a tape catch and a falling sweep), a tick while
 * going back or forward, playing on from the cursor, and back to the present. Returns the samples written. */
enum { SMZ_RW_SFX_OPEN, SMZ_RW_SFX_TICK_BACK, SMZ_RW_SFX_TICK_FWD, SMZ_RW_SFX_PLAY, SMZ_RW_SFX_CANCEL };
int smz_rewind_sound(int kind, int rate, int16_t *out, int cap);

#ifdef __cplusplus
}
#endif

#endif
