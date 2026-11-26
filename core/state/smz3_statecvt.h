/* Save state conversion between snes9x 1.43 (snes9x_3ds, "#!snes9x:0001") and snes9x 1.63 libretro (PC and Android, "#!s9xsnp:0014").
 * Memory, registers and timers carry over exactly; the DSP's per-voice state is rebuilt, so a few milliseconds of sound may differ. */
#ifndef SMZ3_STATECVT_H
#define SMZ3_STATECVT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SMZ_STATE_UNKNOWN = 0, SMZ_STATE_143 = 1, SMZ_STATE_V14 = 2 };

/* SMZ_STATE_143, SMZ_STATE_V14 (libretro versions 12 to 14, which share the layout) or SMZ_STATE_UNKNOWN. */
int smz_state_format(const uint8_t *data, size_t n);

/* Both return 0 and a malloc'd *out the caller frees, or -1. */
int smz_state_143_to_v14(const uint8_t *in, size_t n, uint8_t **out, size_t *outn);
int smz_state_v14_to_143(const uint8_t *in, size_t n, uint8_t **out, size_t *outn);

#ifdef __cplusplus
}
#endif

#endif
