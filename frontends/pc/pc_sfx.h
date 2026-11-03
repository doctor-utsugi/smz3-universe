/* Soft interface sounds for the tracker panel, made on the fly and played on an audio device of their own. */
#ifndef PC_SFX_H
#define PC_SFX_H

typedef enum { SFX_MOVE, SFX_TAB, SFX_SELECT, SFX_BACK, SFX_OPEN, SFX_CLOSE, SFX_CONFIRM, SFX_TICK, SFX_FANFARE } SfxKind;

void sfx_play(SfxKind kind);
/* One of the rewind screen's sounds (SMZ_RW_SFX_* of smz3_rewind.h). */
void sfx_rewind(int kind);

#endif
