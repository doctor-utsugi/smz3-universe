#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_z3.h"
#include "smz3_run.h"
#include "z3_generate.h"
#include "z3_rom.h"

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
    int rc = z3_generate(&s, z3, SMZ_Z3_BASE_SIZE, rom_out, cap, seed, NULL, NULL);
    if (rc) return rc;
    if (link_rdc && sprite_apply(rom_out, link_rdc, link_len)) return -6;
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
