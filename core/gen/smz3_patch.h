/* The seed-dependent changes the generator writes into the combined ROM, following the C# RomPatch classes. */
#ifndef SMZ3_PATCH_H
#define SMZ3_PATCH_H

#include "smz3_fill.h"

#define SMZ_ROM_SIZE 0x600000

typedef struct {
    int pyramid_open;
    int tourian_door_open;
    const char *player_name;      /* shown in the multiworld player table */
    char seed_guid[33];           /* identifies the seed for the auto tracker */
    char world_guid[33];
    /* Cosmetic and Cas' options: the tracker's defaults */
    int heart_color;              /* 0 red, 1 yellow, 2 green, 3 blue */
    int low_health_beep;          /* LowHealthBeepSpeed byte */
    int menu_speed;               /* 0 slow, 1 default, 2 fast, 3 instant */
    int disable_flashing, prevent_scams, randomized_bottles, infinite_space_jump, no_bozo_softlock;
    int metroid_fast_pause_menu, metroid_auto_save, disable_low_energy_beep;
    int zelda_drops;              /* 0 randomized, 1 vanilla, 2 easy, 3 difficult */
    uint16_t controls[7];         /* shoot, jump, dash, item select, item cancel, aim up, aim down */
    int moonwalk, item_cancel_hold;
    const uint8_t *link_rdc, *samus_rdc;   /* optional sprite files (RDC), applied before the Cas IPS patches */
    size_t link_rdc_len, samus_rdc_len;
    int hint_tiles;               /* in-game hint tiles (0..15; the tracker default is 15) */
    int32_t hint_seed;            /* second value drawn from the generator after filling */
    const struct SmzPlando *plando;   /* plando config: prize, fairy trade and text overrides (NULL for a generated seed) */
    int etecoons_jingle;          /* 0 random (picked from the seed), 1..SMZ_JINGLE_COUNT fixed (EtecoonsJingle enum order) */
    int light_dark_rooms;         /* clear the dark flag of every underworld room header */
    int sm_default_controls;      /* Super Metroid's own controller settings start as shot Y, jump B, dash A, item cancel X (off in the oracle) */
    int quick_morph;              /* apply UnifiedAim.ips: aim-down becomes a quick morph and aim up/down share one button */
    int nerfed_charge;            /* apply nerfed_charge.ips: the beam charges without the item, at a third of the damage (off, as on the web) */
} SmzPatchCfg;

typedef void (*SmzPatchLog)(void *user, const char *cls, int offset, const uint8_t *data, int n);

void smz_patch_cfg_defaults(SmzPatchCfg *c, const SmzSettings *s);

/* Writes the patches into rom (SMZ_ROM_SIZE bytes, already holding the combined ROM and the base IPS patch).
 * patch_seed is the first value drawn from the generator after filling. Every change is also reported to log. */
int smz_apply_patches(uint8_t *rom, const SmzSeed *seed, const SmzSettings *s, const SmzPatchCfg *cfg,
                      int32_t patch_seed, SmzPatchLog log, void *user);

#endif
