/* Map Rando's generator (blkerby/MapRandomizer, Rust) behind a C interface. The crate in this folder builds it as a
 * static library for each host; tools/maprando/fetch.sh pins the upstream sources and packs the data folder. */
#ifndef SMZ3_MAPRANDO_H
#define SMZ3_MAPRANDO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMZ_MR_ROM_SIZE 0x400000   /* a Map Rando ROM is always 4 MB */

/* Loads the logic, presets and map pool from the packed data folder (about 600 MB of memory on a PC, half a second). Once
 * per process; later calls return at once. 0, or -1 with the reason in err. */
int smz_mr_init(const char *data_dir, char *err, size_t err_cap);

/* Builds the seed of settings_json ({"preset","skill","items","qol","objectives"}, upstream preset names) into out, the same
 * ROM on every host; info gets {"seedName","displaySeed","hash","attempts"}, or {"error"} and -1. */
int smz_mr_generate(const char *settings_json, uint64_t seed, uint32_t max_attempts, const uint8_t *rom, size_t rom_len,
                    uint8_t *out, size_t out_cap, char *info, size_t info_cap);

/* The attempts of the seed smz_mr_generate is building now (from another thread), for a progress line. */
uint32_t smz_mr_attempts(void);

void smz_mr_free(void);

#ifdef __cplusplus
}
#endif

#endif
