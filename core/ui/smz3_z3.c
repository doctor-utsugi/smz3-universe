#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z3_generate.h"
#include "z3_rom.h"
#include "z3_fill.h"
#include "smz3_z3.h"
#include "smz3_run.h"
#include "smz3_state.h"

/* The 32 icons of z3randomizer's hashalphabet.asm, in its order (the hash bytes at 0x180215 are indices). */
static const char *const ICONS[32] = {
    "Bow", "Boomerang", "Hookshot", "Bomb", "Mushroom", "Powder", "Ice Rod", "Pendant", "Bombos", "Ether", "Quake", "Lamp",
    "Hammer", "Shovel", "Flute", "Bug Net", "Book", "Bottle", "Potion", "Cane", "Cape", "Mirror", "Boots", "Gloves",
    "Flippers", "Pearl", "Shield", "Tunic", "Heart", "Map", "Compass", "Key"};

void smz_z3_hash_text(const uint8_t *rom, size_t len, char *out, size_t cap)
{
    if (!cap) return;
    out[0] = 0;
    if (len < 0x18021A) return;
    const uint8_t *h = rom + 0x180215;
    snprintf(out, cap, "%s %s %s %s %s", ICONS[h[0] & 31], ICONS[h[1] & 31], ICONS[h[2] & 31], ICONS[h[3] & 31], ICONS[h[4] & 31]);
}

int smz_z3_hash_icons(const char *text, int out[5])
{
    int n = 0;
    while (text && *text && n < 5) {
        while (*text == ' ') text++;
        int best = -1;
        size_t blen = 0;
        for (int i = 0; i < 32; i++) {   /* the longest name that starts here ("Ice Rod" before a one-word name) */
            size_t l = strlen(ICONS[i]);
            if (l > blen && !strncmp(text, ICONS[i], l) && (text[l] == ' ' || !text[l])) { best = i; blen = l; }
        }
        if (best < 0) return 0;
        out[n++] = best;
        text += blen;
    }
    while (text && *text == ' ') text++;
    return n == 5 && text && !*text ? 5 : 0;
}

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* The Link block of an RDC sprite (smz3_sprite.c's fields: graphics, palettes, gloves) where alttpr's rom.js writes a ZSPR. */
static int sprite_apply(uint8_t *rom, const uint8_t *rdc, size_t len)
{
    static const struct { long at; size_t len; } LINK[3] = {{0x80000, 0x7000}, {0xDD308, 120}, {0xDEDF5, 4}};
    if (len < 23 || memcmp(rdc, "RETRODATACONTAINER", 18) || rdc[18] != 1) return -1;
    uint32_t blocks = u32(rdc + 19);
    if (blocks > 64 || 23 + (size_t)blocks * 8 > len) return -1;
    for (uint32_t b = 0; b < blocks; b++) {
        if (u32(rdc + 23 + b * 8) != 1) continue;
        size_t pos = u32(rdc + 27 + b * 8);
        if (pos > len || len - pos < 0x7000 + 120 + 4) return -1;
        for (int f = 0; f < 3; f++) { memcpy(rom + LINK[f].at, rdc + pos, LINK[f].len); pos += LINK[f].len; }
        z3_rom_update_checksum(rom);
        return 0;
    }
    return -1;
}

/* The "z3" object of the settings: every "key":"value" pair goes to z3_option_set. */
static int z3_settings(const char *json, Z3GenSettings *s, uint32_t *seed)
{
    z3_settings_default(s);
    s->quickswap = 1;   /* on by default in the options, which then leave it out of the JSON */
    const char *p = strstr(json, "\"z3\":{"), *sd = strstr(json, "\"seed\":\"");
    if (!p || !sd) return -1;
    *seed = (uint32_t)strtoul(sd + 8, NULL, 10);
    for (p += 6; *p == '"';) {
        char key[24], val[24];
        const char *k = p + 1, *ke = strchr(k, '"');
        if (!ke || ke[1] != ':' || ke[2] != '"') return -1;
        const char *w = ke + 3, *we = strchr(w, '"');
        if (!we || (size_t)(ke - k) >= sizeof key || (size_t)(we - w) >= sizeof val) return -1;
        memcpy(key, k, ke - k); key[ke - k] = 0;
        memcpy(val, w, we - w); val[we - w] = 0;
        if (z3_option_set(s, key, val)) return -1;
        p = we + 1 + (we[1] == ',');
    }
    return 0;
}

static int insert(char *json, size_t cap, size_t at, const char *text)
{
    size_t n = strlen(json), k = strlen(text);
    if (n + k + 1 > cap || at > n) return -1;
    memmove(json + at + k, json + at, n - at + 1);
    memcpy(json + at, text, k);
    return 0;
}

int smz_z3_generate(char *json, size_t json_cap, const uint8_t *z3, const uint8_t *link_rdc, size_t link_len,
                    uint8_t *rom_out, size_t cap)
{
    Z3GenSettings s;
    uint32_t seed;
    if (smz_settings_mode(json) != SMZ_MODE_Z3 || z3_settings(json, &s, &seed)) return -7;
    int rc = -3;
    for (int k = 0; (rc == -3 || rc == -4) && k < SMZ_Z3_SEED_TRIES; k++) rc = z3_generate(&s, z3, SMZ_Z3_BASE_SIZE, rom_out, cap, seed + (uint32_t)k, NULL, NULL);
    if (rc) return rc;
    if (!z3_rom_shortcut_hooks(rom_out)) z3_rom_update_checksum(rom_out);   /* kept out of z3_generate, alttpr's own ROM */
    if (link_rdc && sprite_apply(rom_out, link_rdc, link_len)) return SMZ_Z3_ERR_SPRITE;
    if (strstr(json, "\"lightDarkRooms\":true")) {
        /* as SMZ3's LightDarkRooms: bit 0 of each room header's first byte is the dark flag, headers through the table at $04:F1E2 */
        for (int room = 0; room < 296; room++) {
            const uint8_t *t = rom_out + 0x271E2 + 2 * room;
            rom_out[0x20000 + ((t[0] | t[1] << 8) & 0x7FFF)] &= 0xFE;
        }
        z3_rom_update_checksum(rom_out);
    }
    char words[96], add[128];
    const char *diff = strstr(json, "\"diff\":[");
    smz_z3_hash_text(rom_out, SMZ_Z3_ROM_SIZE, words, sizeof words);
    if (diff && !strstr(json, "\"hash\":\"")) {
        snprintf(add, sizeof add, "\"Hash: %s\"%s", words, diff[8] == ']' ? "" : ",");
        insert(json, json_cap, (size_t)(diff - json) + 8, add);
        snprintf(add, sizeof add, "\"hash\":\"%s\",", words);
        insert(json, json_cap, (size_t)(diff - json), add);
    }
    return 0;
}

const char *smz_z3_error(int rc)
{
    switch (rc) {
    case -3: case -4: return "no seed could be made from this number";
    case -6: return "the boss shuffle could not be written";
    case SMZ_Z3_ERR_SPRITE: return "the sprite file is not valid";
    default: return NULL;
    }
}

int smz_z3_rebuild(const Z3GenSettings *s, uint32_t seed, Z3Seed *z)
{
    for (int k = 0; k < SMZ_Z3_SEED_TRIES; k++) {   /* z3_generate_rng's steps up to isWinnable, without the ROM */
        Z3Rng r;
        z3_rng_init(&r, seed + (uint32_t)k);
        int ganon = s->crystals_ganon < 0 ? z3_rng_int(&r, 0, 7) : s->crystals_ganon;
        int tower = s->crystals_tower < 0 ? z3_rng_int(&r, 0, 7) : s->crystals_tower;
        Z3Cfg c;
        z3_cfg_defaults(&c);
        c.mode_state = s->mode_state; c.logic = s->logic; c.itemPlacement = s->item_placement; c.dungeonItems = s->dungeon_items;
        c.accessibility = s->accessibility; c.goal = s->goal; c.crystals_ganon = ganon; c.crystals_tower = tower;
        c.mode_weapons = s->weapons; c.item_pool = s->item_pool; c.item_functionality = s->item_functionality;
        c.enemizer_bossShuffle = s->boss_shuffle;
        z3_cfg_derive(&c);
        z3_seed_init(z, &c);
        z->start_boots = s->start_boots;
        if (z3_fill(z, &r)) continue;
        z3_texts_randomize(z, &r, s->hints);
        if (z3_seed_winnable(z)) return 0;
    }
    return -1;
}

int smz_z3_settings(const char *json, Z3GenSettings *s, uint32_t *seed) { return z3_settings(json, s, seed); }

static void norm(const char *a, const char *b, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = a; p; p = p == a ? b : NULL)
        for (const char *c = p; *c && n + 1 < cap; c++)
            if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) out[n++] = *c;
            else if (*c >= 'A' && *c <= 'Z') out[n++] = (char)(*c - 'A' + 'a');
    out[n] = 0;
}

static int item_location(int l)
{
    const char *k = Z3_LOCS[l].kind;
    return strncmp(k, "Prize", 5) && strcmp(k, "Trade") && strcmp(k, "Medallion") && strcmp(k, "Fountain");
}

static int z3_named(const char *want)
{
    char n[96];
    for (int l = 0; l < Z3_LOC_COUNT; l++) {
        if (!item_location(l)) continue;
        norm(Z3_LOCS[l].name, "", n, sizeof n);
        if (!strcmp(n, want)) return l;
    }
    return -1;
}

/* SMZ3's names for a few of alttpr's: the bosses' heart containers, Castle Tower's first chest, Cave 45. */
static const char *const BOSSES[] = {"Armos Knights", "Lanmolas", "Moldorm", "Helmasaur King", "Arrghus", "Mothula", "Blind", "Kholdstare", "Vitreous", "Trinexx"};

int smz_z3_loc_of(int tracker_index)
{
    static int16_t map[512];
    static int made;
    if (!made) {
        for (unsigned j = 0; j < smz3_location_count && j < 512; j++) {
            const smz3_location *t = &smz3_locations[j];
            char want[96];
            int l = -1;
            map[j] = -1;
            if (t->game != SMZ3_GAME_ZELDA) continue;
            for (unsigned b = 0; b < sizeof BOSSES / sizeof BOSSES[0] && l < 0; b++)
                if (!strcmp(t->name, BOSSES[b])) { norm(t->region, "Boss", want, sizeof want); l = z3_named(want); }
            if (l < 0 && !strcmp(t->id, "CastleTowerFoyer")) l = z3_named("castletowerroom03");
            if (l < 0 && !strcmp(t->id, "SouthOfGrove")) l = z3_named("cave45");
            if (l < 0) { norm(t->name, "", want, sizeof want); l = z3_named(want); }
            if (l < 0) { norm(t->region, t->name, want, sizeof want); l = z3_named(want); }
            if (l < 0) { norm(t->id, "", want, sizeof want); l = z3_named(want); }
            map[j] = (int16_t)l;
        }
        made = 1;
    }
    return tracker_index >= 0 && tracker_index < (int)smz3_location_count && tracker_index < 512 ? map[tracker_index] : -1;
}

int smz_z3_tracker_index(int z3_loc)
{
    if (z3_loc < 0 || z3_loc >= Z3_LOC_COUNT) return -1;
    for (int j = 0; j < (int)smz3_location_count; j++) {
        int l = smz_z3_loc_of(j);
        if (l >= 0 && !strcmp(Z3_LOCS[l].name, Z3_LOCS[z3_loc].name)) return j;
    }
    return -1;
}

/* Every prize and event (pendants, crystals, Agahnim) the items so far reach, as the logic counts them. */
static void take_prizes(const Z3World *w, Z3Items *it, uint8_t *taken)
{
    for (int more = 1; more;) {
        more = 0;
        for (int l = 0; l < Z3_LOC_COUNT; l++) {
            if (taken[l] || strncmp(Z3_LOCS[l].kind, "Prize", 5) || !z3_loc_exists(w, l) || w->item_at[l] == Z3_ITEM_NONE) continue;
            if (!z3_loc_access(w, it, l)) continue;
            taken[l] = 1; more = 1;
            z3_items_add(it, w->item_at[l], 1);
        }
    }
}

int smz_z3_proof_logic(const char *json, const SmzProof *p, SmzProofVerdict *v)
{
    static Z3Seed z;
    Z3GenSettings s;
    uint32_t seed;
    if (z3_settings(json, &s, &seed) || smz_z3_rebuild(&s, seed, &z)) return -1;
    z.w.col = NULL;   /* every sweep fresh */
    const Z3World *w = &z.w;
    Z3Items it = w->pre;
    uint8_t taken[Z3_LOC_COUNT] = {0};
    for (int i = 0; i < p->n; i++) {
        int l = smz_z3_loc_of(p->loc[i]);
        if (l < 0) { v->out_of_logic++; continue; }
        l = z3_loc_named(w, l);
        take_prizes(w, &it, taken);
        if (!z3_loc_access(w, &it, l)) v->out_of_logic++;
        if (w->item_at[l] != Z3_ITEM_NONE) z3_items_add(&it, w->item_at[l], 1);
    }
    return 0;
}
