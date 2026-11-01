/* Which checks are in logic for a run: its seed is rebuilt from the settings JSON stored with the run, then asked what the
 * inventory read from the game can reach (the same call the Android tracker makes). */
#ifndef SMZ3_RUNLOGIC_H
#define SMZ3_RUNLOGIC_H

#include <stdint.h>
#include "smz3_generate.h"

typedef struct {
    SmzSeed seed;
    int valid;
} SmzLogic;

/* Rebuilds the seed from a run's settings JSON. Returns 0, or -1 when the run cannot be followed (plando, no valid fill). The
 * generator needs a large stack: call from a thread with at least 32 MB. */
int smz_logic_build(SmzLogic *l, const char *settings_json);
/* The settings JSON fields the logic depends on, as the generator takes them. */
int smz_logic_settings(const char *settings_json, SmzSettings *s);
/* in_logic[i] (i over smz3_locations) is set for the checks reachable with these ItemType ids. Returns how many are. */
int smz_logic_reachable(const SmzLogic *l, const int *items, int n, uint8_t *in_logic, int cap);

#endif
