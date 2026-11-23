/* The whole offline generator: seed in, playable ROM out. */
#ifndef SMZ3_GENERATE_H
#define SMZ3_GENERATE_H

#include "smz3_rom.h"

/* Returns the bytes of the IPS patch with that file name, or NULL. */
typedef const uint8_t *(*SmzIpsLoader)(void *user, const char *name, size_t *len);

/* rom_out holds SMZ_ROM_SIZE bytes. sm is the 3 MB Super Metroid ROM and z3 the 1 MB A Link to the Past ROM, both
 * without copier header. Returns 0, or a negative number: -1 missing IPS, -2 patch error, -3..-5 fill failed, -6 bad sprite file, -7 the filled seed cannot be completed.
 * smz_generate retries internally (see smz_fill_valid); -7 means all attempts failed. */
int smz_generate(uint8_t *rom_out, SmzSeed *seed_out, const uint8_t *sm, const uint8_t *z3, const SmzSettings *s,
                 const SmzPatchCfg *cfg, SmzIpsLoader load, void *user, SmzPatchLog log, void *log_user);

/* Like smz_generate, for a parsed plando config (smz3_plando.h): no filling, the items are the config's. -8 when the config is incomplete. */
struct SmzPlando;
int smz_generate_plando(uint8_t *rom_out, SmzSeed *seed_out, const uint8_t *sm, const uint8_t *z3, const struct SmzPlando *pl,
                        const SmzPatchCfg *cfg, SmzIpsLoader load, void *user, SmzPatchLog log, void *log_user);

/* Fills like smz_fill but retries (up to 5 times, with a seed derived from the first) until the playthrough check passes.
 * eff receives the settings of the attempt that worked; the tracker's logic must use the same call. */
int smz_fill_valid(SmzSeed *out, const SmzSettings *s, SmzSettings *eff);
int smz_playthrough_ok(const SmzSeed *seed, const SmzSettings *s);

/* Sets one logic option of a configuration by its C# property name ("WallJumpDifficulty" takes 0..3). 0 on success. */
int smz_cfg_set(SmzCfg *cfg, const char *name, int value);
int smz_cfg_get(const SmzCfg *cfg, const char *name);
/* Sets any setting a run stores by name: a logic option (smz_cfg_set), "GameMode" (MODE_*), "SpazersInPool", "SpazersRequired"
 * or "opt:<item group>" (the chosen option of a start-with / early item group). 0 on success, -1 for an unknown name. */
int smz_setting_set(SmzSettings *s, const char *name, int value);
/* ItemType number for its C# name, or -1. */
int smz_item_by_name(const char *name);

/* Locations in logic (ids) for a filled seed when the player owns these item types; rewards and bosses that the
 * items allow are counted too. Returns how many ids were written to out. */
int smz_reachable(const SmzSeed *seed, const int *items, int n, int *out, int cap);
/* Items the logic assumes the player has without them being read from the game: the start items, every keycard when Metroid
 * keysanity is off and every key of the seed (placed in its own dungeon) when Zelda keysanity is off. Returns how many were written. */
int smz_assumed_items(const SmzSeed *seed, int *out, int cap);
/* The same, also returning the progression (items, rewards and bosses) the check used. */
int smz_reachable_prog(const SmzSeed *seed, const int *items, int n, int *out, int cap, SmzProg *prog_out);

#endif
