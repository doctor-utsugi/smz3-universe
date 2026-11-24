/* The whole standalone Super Metroid generator (port of tewtal's Randomizer.SuperMetroid): seed in, ROM out. */
#ifndef SM_GENERATE_H
#define SM_GENERATE_H

#include "sm_patch.h"

#define SM_JU_SIZE 0x300000   /* Super Metroid (JU), no copier header, MD5 21f3e98df4780ee1c667b84e57d88675 */

/* The base patch (patches/sm_randomizer.ips, built by tools/build_sm_ips.sh), embedded by tools/gen_sm_ips_c.py. */
extern const uint8_t SM_BASE_IPS[];
extern const size_t SM_BASE_IPS_LEN;

/* Option table of the C# generator (tools/gen_sm_options.py): logic, goal, placement, race. */
typedef struct { const char *key, *description; } SmOptValue;
typedef struct { const char *key, *description, *type; const SmOptValue *values; int n_values; const char *def; } SmOption;
extern const SmOption SM_OPTIONS[];
extern const int SM_OPTION_COUNT;

/* Sets one option by its C# key and value ("logic" "casual", "placement" "full", "race" "true", "seed" "123").
 * 0 on success, -1 for an unknown key or value. */
int sm_option_set(SmGenSettings *s, const char *key, const char *value);

/* sm_rom holds SM_JU_SIZE bytes; out_rom gets SM_ROM_SIZE bytes. seed_out and hash_out (4 bytes) may be NULL.
 * Returns 0, or -1 out_cap too small, -2 base patch error, -3 fill failed, -4 the seed cannot be completed. */
int sm_generate(const SmGenSettings *s, const uint8_t *sm_rom, uint8_t *out_rom, size_t out_cap,
                const SmPatchIds *ids, SmSeed *seed_out, uint8_t *hash_out);

#endif
