#include "smz3_sprite.h"
#include <stdlib.h>
#include <string.h>
#include "smz3_generate.h"
#include "smz3_plando.h"
#include "smz3_itemnames.h"
#include "smz3_itemopts.h"
#include "smz3_world_gen.h"

/* Plays the filled seed sphere by sphere like PlaythroughService: collects everything in logic until nothing new
 * opens up, then checks the same critical locations and items. */
int smz_playthrough_ok(const SmzSeed *seed, const SmzSettings *s)
{
    static int items[1024], reach[512], seen[512];
    int n = 0;
    memset(seen, 0, sizeof seen);
    if (s->keysanity != KEYS_SuperMetroid && s->keysanity != KEYS_Both)
        for (int t = ITEM_CardCrateriaL1; t <= ITEM_CardLowerNorfairBoss; t++) items[n++] = t;
    for (int i = 0; i < seed->n_start; i++) items[n++] = seed->start_items[i];
    int base = n;
    for (int sphere = 0; sphere < 101; sphere++) {
        int k = smz_reachable(seed, items, n, reach, 512), added = 0;
        for (int i = 0; i < k; i++) {
            if (seen[reach[i]]) continue;
            seen[reach[i]] = 1; added++;
            int t = seed->world.item_at[reach[i]];
            if (t != ITEM_Nothing && n < 1024) items[n++] = t;
        }
        if (!added) break;
        if (sphere == 100) return 0;
    }
    static const int must[] = {LOC_GanonsTowerMoldormChest, LOC_KraidsLairVariaSuit, LOC_WreckedShipEastSuper,
                               LOC_InnerMaridiaSpaceJump, LOC_LowerNorfairRidleyTank};
    for (unsigned i = 0; i < sizeof must / sizeof *must; i++) if (!seen[must[i]]) return 0;
    int silver = 0, crateria = 0;
    for (int i = 0; i < n; i++) { if (items[i] == ITEM_SilverArrows) silver = 1; if (items[i] == ITEM_CardCrateriaBoss) crateria = 1; }
    (void)base;
    return silver && crateria;
}

#define SMZ_ATTEMPTS 5

int smz_fill_valid(SmzSeed *out, const SmzSettings *s, SmzSettings *eff)
{
    int rc = -7;
    for (int a = 0; a < SMZ_ATTEMPTS; a++) {
        *eff = *s;
        smz_settings_normalize(eff);
        if (a) eff->seed = (int32_t)(((uint32_t)s->seed * 2654435761u + (uint32_t)a * 40503u) & 0x7FFFFFFF);
        rc = smz_fill(out, eff);
        if (rc == 0 && smz_playthrough_ok(out, eff)) return 0;
        if (rc == 0) rc = -7;
    }
    return rc;
}

/* Patches a filled seed into the ROM: the part of generation both smz_generate and smz_generate_plando share. */
static int build_rom(uint8_t *rom, SmzSeed *seed, const uint8_t *sm, const uint8_t *z3, const SmzSettings *s, const SmzPatchCfg *cfg_in,
                     int32_t patch_seed, int32_t hint_seed, SmzIpsLoader load, void *user, SmzPatchLog log, void *log_user)
{
    size_t len;
    const uint8_t *ips;
    smz_rom_combine(rom, sm, z3);
    if (!(ips = load(user, "zsm.ips", &len)) || smz_ips_apply(rom, ips, len)) return -1;
    SmzPatchCfg hc = *cfg_in;
    if (s->game_mode == MODE_AllDungeons) hc.pyramid_open = 0;
    hc.hint_seed = hint_seed;
    const SmzPatchCfg *cfg = &hc;
    if (smz_apply_patches(rom, seed, s, cfg, patch_seed, log, log_user)) return -2;
    if (cfg->link_rdc && smz_sprite_apply_rdc(rom, SMZ_ROM_SIZE, cfg->link_rdc, cfg->link_rdc_len) < 0) return -6;
    if (cfg->samus_rdc && smz_sprite_apply_rdc(rom, SMZ_ROM_SIZE, cfg->samus_rdc, cfg->samus_rdc_len) < 0) return -6;
    for (int i = 0; i < SMZ_DEFAULT_CAS_IPS_COUNT; i++) {
        if (!cfg->nerfed_charge && !strcmp(SMZ_DEFAULT_CAS_IPS[i], "nerfed_charge.ips")) continue;
        if (!(ips = load(user, SMZ_DEFAULT_CAS_IPS[i], &len)) || smz_ips_apply_sm(rom, ips, len)) return -1;
    }
    if (cfg->quick_morph && (!(ips = load(user, "UnifiedAim.ips", &len)) || smz_ips_apply_sm(rom, ips, len))) return -1;
    if (cfg->light_dark_rooms) {
        /* room header pointer table at $04:F1E2 of the ALttP half; bit 0 of each header's first byte is the dark flag */
#define Z3A(bank, off) (0x400000 + (bank) * 0x10000 + 0x8000 + ((off) & 0x7FFF))
        for (int room = 0; room < 296; room++) {
            int tab = Z3A(4, 0x71E2) + 2 * room;
            rom[Z3A(4, rom[tab] | rom[tab + 1] << 8)] &= 0xFE;
        }
#undef Z3A
    }
    if (cfg->sm_default_controls) {
        /* Four places of the Super Metroid code load the default button bindings ($81:B31A, $82:F22C, $8B:BC4A and
         * $91:8845 of the combined ROM): swap X/A/B/Y so shot is Y, jump B, dash A and item cancel X. */
        static const uint32_t SITES[4] = {0x01B31A, 0x02F22C, 0x0BBC4A, 0x118845};
        for (int k = 0; k < 4; k++) {
            for (uint32_t i = SITES[k]; i < SITES[k] + 0x50; i++) {
                if (rom[i] != 0xA9 || rom[i + 3] != 0x8D || rom[i + 5] != 0x09) continue;   /* LDA #imm16 ; STA $09xx */
                uint16_t v = (uint16_t)(rom[i + 1] | rom[i + 2] << 8);
                switch (rom[i + 4]) {
                case 0xB2: v = 0x4000; break;   /* shot */
                case 0xB4: v = 0x8000; break;   /* jump */
                case 0xB6: v = 0x0080; break;   /* dash */
                case 0xB8: v = 0x0040; break;   /* item cancel */
                default: continue;
                }
                rom[i + 1] = (uint8_t)v;
                rom[i + 2] = (uint8_t)(v >> 8);
            }
        }
    }
    smz_rom_shortcut_hooks(rom);   /* an unexpected ROM keeps working, its shortcuts just refresh at the next pause */
    smz_rom_checksum(rom);
    return 0;
}

int smz_generate(uint8_t *rom, SmzSeed *seed, const uint8_t *sm, const uint8_t *z3, const SmzSettings *s,
                 const SmzPatchCfg *cfg, SmzIpsLoader load, void *user, SmzPatchLog log, void *log_user)
{
    SmzSettings eff;
    int rc = smz_fill_valid(seed, s, &eff);
    if (rc) return rc;
    smz_settings_normalize(&eff);
    int32_t patch_seed = rng_next(&seed->rng);
    int32_t hint_seed = rng_next(&seed->rng);   /* C# draws the hint seed right after the patch seed */
    return build_rom(rom, seed, sm, z3, &eff, cfg, patch_seed, hint_seed, load, user, log, log_user);
}

int smz_generate_plando(uint8_t *rom, SmzSeed *seed, const uint8_t *sm, const uint8_t *z3, const struct SmzPlando *pl, const SmzPatchCfg *cfg_in,
                        SmzIpsLoader load, void *user, SmzPatchLog log, void *log_user)
{
    char err[256];
    if (smz_plando_seed(seed, pl, err, sizeof err)) return -8;
    SmzSettings s;
    smz_plando_settings(pl, &s);
    SmzPatchCfg cfg = *cfg_in;
    cfg.plando = pl;
    cfg.pyramid_open = pl->open_pyramid;
    cfg.tourian_door_open = pl->skip_tourian_door;
    cfg.hint_tiles = 0;   /* a plando has no generated hints */
    return build_rom(rom, seed, sm, z3, &s, &cfg, smz_seed_number(pl->seed), 0, load, user, log, log_user);
}

int smz_cfg_set(SmzCfg *c, const char *name, int v)
{
#define B(field) if (!strcmp(name, #field)) { c->field = v != 0; return 0; }
    B(PreventScrewAttackSoftLock) B(PreventFivePowerBombSeed) B(LeftSandPitRequiresSpringBall) B(LaunchPadRequiresIceBeam)
    B(WaterwayNeedsGravitySuit) B(EasyEastCrateriaSkyItem) B(EasyBlueBrinstarTop) B(KholdstareNeedsCaneOfSomaria)
    B(ZoraNeedsRupeeItems) B(QuarterMagic) B(PreventLowResourceDarkWorld) B(FireRodDarkRooms) B(InfiniteBombJump)
    B(ParlorSpeedBooster) B(MoatSpeedBooster) B(MockBall) B(SwordOnlyDarkRooms) B(LightWorldSouthFakeFlippers)
#undef B
    if (!strcmp(name, "WallJumpDifficulty")) { c->WallJumpDifficulty = v; return 0; }
    return -1;
}

int smz_cfg_get(const SmzCfg *c, const char *name)
{
#define B(field) if (!strcmp(name, #field)) return c->field != 0;
    B(PreventScrewAttackSoftLock) B(PreventFivePowerBombSeed) B(LeftSandPitRequiresSpringBall) B(LaunchPadRequiresIceBeam)
    B(WaterwayNeedsGravitySuit) B(EasyEastCrateriaSkyItem) B(EasyBlueBrinstarTop) B(KholdstareNeedsCaneOfSomaria)
    B(ZoraNeedsRupeeItems) B(QuarterMagic) B(PreventLowResourceDarkWorld) B(FireRodDarkRooms) B(InfiniteBombJump)
    B(ParlorSpeedBooster) B(MoatSpeedBooster) B(MockBall) B(SwordOnlyDarkRooms) B(LightWorldSouthFakeFlippers)
#undef B
    if (!strcmp(name, "WallJumpDifficulty")) return c->WallJumpDifficulty;
    return 0;
}

int smz_setting_set(SmzSettings *s, const char *name, int value)
{
    if (!strcmp(name, "GameMode")) { s->game_mode = value; return 0; }
    if (!strcmp(name, "SpazersInPool")) { s->spazers_in_pool = value; return 0; }
    if (!strcmp(name, "SpazersRequired")) { s->spazers_required = value; return 0; }
    if (!strncmp(name, "opt:", 4)) return smz_itemopt_select(s, name + 4, value);
    return smz_cfg_set(&s->cfg, name, value);
}

int smz_item_by_name(const char *name)
{
    for (int i = 0; i < SMZ_ITEM_NAME_COUNT; i++) if (!strcmp(SMZ_ITEM_NAMES[i].name, name)) return SMZ_ITEM_NAMES[i].id;
    return -1;
}

int smz_reachable_prog(const SmzSeed *seed, const int *items, int n, int *out, int cap, SmzProg *prog_out)
{
    const SmzWorld *w = &seed->world;
    SmzProg p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i < n; i++) if (items[i] > 0 && items[i] < 256) p.items[items[i]]++;
    /* rewards first, then the bosses that need them */
    SmzProg r = p;
    for (int g = 0; g < REG_COUNT; g++)
        if (SMZ_REGIONS[g].has_reward && region_can_retrieve_reward(w, g, &r, false)) p.rewards[w->reward_of[g]]++;
    SmzProg b = p;
    memset(b.bosses, 0, sizeof b.bosses);
    for (int g = 0; g < REG_COUNT; g++) {
        int boss = w->boss_of[g];
        if (SMZ_REGIONS[g].default_boss != BOSS_None && (boss == BOSS_Kraid || boss == BOSS_Phantoon || boss == BOSS_Draygon || boss == BOSS_Ridley) &&
            region_can_beat_boss(w, g, &b, false))
            p.bosses[boss] = 1;
    }
    int k = 0;
    for (int i = 0; i < SMZ_LOC_COUNT && k < cap; i++)
        if (loc_available(w, SMZ_LOCS[i].id, &p)) out[k++] = SMZ_LOCS[i].id;
    if (prog_out) *prog_out = p;
    return k;
}

int smz_assumed_items(const SmzSeed *seed, int *out, int cap)
{
    int n = 0;
    for (int i = 0; i < seed->n_start && n < cap; i++) out[n++] = seed->start_items[i];
    if (!seed->world.metroid_keysanity)
        for (int t = ITEM_CardCrateriaL1; t <= ITEM_CardLowerNorfairBoss && n < cap; t++) out[n++] = t;
    if (!seed->world.zelda_keysanity)
        for (int i = 0; i < SMZ_LOC_COUNT && n < cap; i++) {
            int t = seed->world.item_at[SMZ_LOCS[i].id];
            if (t > 0 && (SMZ_ITEM_CATS[t] & (ICAT_SmallKey | ICAT_BigKey)) && (SMZ_ITEM_CATS[t] & ICAT_Zelda)) out[n++] = t;
        }
    return n;
}

int smz_reachable(const SmzSeed *seed, const int *items, int n, int *out, int cap)
{
    return smz_reachable_prog(seed, items, n, out, cap, NULL);
}
