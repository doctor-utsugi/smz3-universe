/* RandomizerController::prepSeed for the standalone Z3 generator: settings, base patch, fill, ROM. */
#include <stdio.h>
#include <string.h>
#include "z3_generate.h"
#include "z3_rom.h"

void z3_settings_default(Z3GenSettings *s)
{
    *s = (Z3GenSettings){
        .mode_state = Z3_MODE_STATE_OPEN, .goal = Z3_GOAL_GANON, .crystals_tower = 7, .crystals_ganon = 7,
        .dungeon_items = Z3_DUNGEONITEMS_STANDARD, .accessibility = Z3_ACCESSIBILITY_ITEMS,
        .item_placement = Z3_ITEMPLACEMENT_ADVANCED, .weapons = Z3_MODE_WEAPONS_RANDOMIZED,
        .item_pool = Z3_ITEM_POOL_NORMAL, .item_functionality = Z3_ITEM_FUNCTIONALITY_NORMAL, .hints = true,
        .tournament = false, .heart_beep = -1, .menu_speed = -1, .quickswap = -1,
    };
}

/* Every dropdown's values are listed in the enum's order, so the value's index is the setting. */
int z3_option_set(Z3GenSettings *s, const char *key, const char *value)
{
    bool yes = strcmp(value, "true") == 0;
    if (strcmp(key, "tournament") == 0 || strcmp(key, "quickswap") == 0) {
        if (!yes && strcmp(value, "false")) return -1;
        if (key[0] == 't') s->tournament = yes;
        else s->quickswap = yes;
        return 0;
    }
    for (int o = 0; o < Z3_OPTION_COUNT; o++) {
        const Z3Option *op = &Z3_OPTIONS[o];
        if (strcmp(op->key, key) || !op->values) continue;
        for (int v = 0; v < op->n_values; v++) {
            if (strcmp(op->values[v].key, value)) continue;
            int crystals = strcmp(value, "random") == 0 ? -1 : v;
            if (!strcmp(key, "world_state")) s->mode_state = v;
            else if (!strcmp(key, "goals")) s->goal = v;
            else if (!strcmp(key, "tower_open")) s->crystals_tower = crystals;
            else if (!strcmp(key, "ganon_open")) s->crystals_ganon = crystals;
            else if (!strcmp(key, "dungeon_items")) s->dungeon_items = v;
            else if (!strcmp(key, "accessibility")) s->accessibility = v;
            else if (!strcmp(key, "item_placement")) s->item_placement = v;
            else if (!strcmp(key, "weapons")) s->weapons = v;
            else if (!strcmp(key, "item_pool")) s->item_pool = v;
            else if (!strcmp(key, "item_functionality")) s->item_functionality = v;
            else if (!strcmp(key, "hints")) s->hints = v == 0;
            else if (!strcmp(key, "glitches_required")) s->logic = v;
            else if (!strcmp(key, "heart_beep")) s->heart_beep = v;
            else if (!strcmp(key, "menu_speed")) s->menu_speed = v;
            else return -1;
            return 0;
        }
    }
    return -1;
}

/* IPS records onto rom (EOF marker ends it; a truncation length after it is ignored, as Rom::resize did first). */
static int apply_ips(uint8_t *rom, size_t size, const uint8_t *p, size_t n)
{
    if (n < 8 || memcmp(p, "PATCH", 5)) return -1;
    for (size_t i = 5; i + 3 <= n;) {
        if (!memcmp(p + i, "EOF", 3)) return 0;
        if (i + 5 > n) return -1;
        size_t at = (size_t)p[i] << 16 | (size_t)p[i + 1] << 8 | p[i + 2], len = (size_t)p[i + 3] << 8 | p[i + 4];
        i += 5;
        if (len) {
            if (i + len > n || at + len > size) return -1;
            memcpy(rom + at, p + i, len);
            i += len;
        } else {
            if (i + 3 > n) return -1;
            size_t rle = (size_t)p[i] << 8 | p[i + 1];
            if (at + rle > size) return -1;
            memset(rom + at, p[i + 2], rle);
            i += 3;
        }
    }
    return -1;
}

int z3_generate(const Z3GenSettings *s, const uint8_t *jp_rom, size_t jp_len, uint8_t *out_rom, size_t out_cap,
                uint32_t seed, char *hash_out, uint8_t *icons_out)
{
    Z3Rng r;
    z3_rng_init(&r, seed);
    return z3_generate_rng(s, jp_rom, jp_len, out_rom, out_cap, &r, seed, hash_out, icons_out);
}

int z3_generate_rng(const Z3GenSettings *s, const uint8_t *jp_rom, size_t jp_len, uint8_t *out_rom, size_t out_cap,
                    Z3Rng *r, uint32_t seed, char *hash_out, uint8_t *icons_out)
{
    static Z3Seed z;   /* static: the 3DS stack is small */
    if (jp_len != Z3_JP_SIZE || out_cap < Z3_ROM_SIZE) return -1;
    memcpy(out_rom, jp_rom, Z3_JP_SIZE);
    memset(out_rom + Z3_JP_SIZE, 0, Z3_ROM_SIZE - Z3_JP_SIZE);   /* Rom::resize before the base patch */
    if (apply_ips(out_rom, Z3_ROM_SIZE, Z3_BASE_IPS, Z3_BASE_IPS_LEN)) return -2;

    /* prepSeed draws the random crystal counts first, Ganon's before the tower's. */
    int ganon = s->crystals_ganon < 0 ? z3_rng_int(r, 0, 7) : s->crystals_ganon;
    int tower = s->crystals_tower < 0 ? z3_rng_int(r, 0, 7) : s->crystals_tower;

    Z3Cfg c;
    z3_cfg_defaults(&c);
    c.mode_state = s->mode_state;
    c.logic = s->logic;
    c.itemPlacement = s->item_placement;
    c.dungeonItems = s->dungeon_items;
    c.accessibility = s->accessibility;
    c.goal = s->goal;
    c.crystals_ganon = ganon;
    c.crystals_tower = tower;
    c.mode_weapons = s->weapons;
    c.item_pool = s->item_pool;
    c.item_functionality = s->item_functionality;
    z3_cfg_derive(&c);

    z3_seed_init(&z, &c);
    if (z3_fill(&z, r)) return -3;
    z3_texts_randomize(&z, r, s->hints);
    if (z3_rom_write(out_rom, &z, s, r)) return -5;
    if (!z3_seed_winnable(&z)) return -4;   /* prepSeed: "Game Unwinnable" */
    if (s->tournament) z3_rom_set_tournament(out_rom, true);   /* rummageTable() is an empty stub upstream */

    /* alttpr's icons come from the seed's database id (hash_array); here a stream of their own, so they depend on
       the seed number alone and not on how many draws the fill took. */
    Z3Rng hr;
    uint8_t icons[5];
    char str[22];
    z3_rng_init(&hr, (uint64_t)seed ^ 0x5A3348415348ull);
    for (int i = 0; i < 5; i++) icons[i] = (uint8_t)z3_rng_int(&hr, 0, 31);
    snprintf(str, sizeof str, "VT %-18u", (unsigned)seed);   /* str_pad("VT $hash", 21) */
    z3_rom_set_seed_string(out_rom, str);
    z3_rom_set_hash(out_rom, icons);
    z3_rom_cosmetics(out_rom, s);
    z3_rom_update_checksum(out_rom);
    if (hash_out) memcpy(hash_out, str, 22);
    if (icons_out) memcpy(icons_out, icons, 5);
    return 0;
}
