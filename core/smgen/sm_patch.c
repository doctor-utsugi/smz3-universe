#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "sm_patch.h"
#include "sm_animals_data.h"

#define SM_VERSION_MAJOR 3
#define SM_VERSION_MINOR 1

int sm_apply_ips(uint8_t *rom, size_t len, const uint8_t *ips, size_t n)
{
    if (n < 8 || memcmp(ips, "PATCH", 5) != 0) return -1;
    size_t i = 5;
    while (i + 3 <= n && memcmp(ips + i, "EOF", 3) != 0) {
        if (i + 5 > n) return -1;
        size_t off = (size_t)ips[i] << 16 | ips[i + 1] << 8 | ips[i + 2];
        size_t sz = (size_t)ips[i + 3] << 8 | ips[i + 4];
        i += 5;
        if (sz > 0) {
            if (i + sz > n || off + sz > len) return -1;
            memcpy(rom + off, ips + i, sz);
            i += sz;
        } else {
            if (i + 3 > n) return -1;
            sz = (size_t)ips[i] << 8 | ips[i + 1];
            if (off + sz > len) return -1;
            memset(rom + off, ips[i + 2], sz);
            i += 3;
        }
    }
    return 0;
}

uint16_t sm_item_plm(int type, int loc_type)
{
    int plm;
    switch (type) {
    case SM_ITEM_ETank: plm = 0xEED7; break;
    case SM_ITEM_Missile: plm = 0xEEDB; break;
    case SM_ITEM_Super: plm = 0xEEDF; break;
    case SM_ITEM_PowerBomb: plm = 0xEEE3; break;
    case SM_ITEM_Bombs: plm = 0xEEE7; break;
    case SM_ITEM_Charge: plm = 0xEEEB; break;
    case SM_ITEM_Ice: plm = 0xEEEF; break;
    case SM_ITEM_HiJump: plm = 0xEEF3; break;
    case SM_ITEM_SpeedBooster: plm = 0xEEF7; break;
    case SM_ITEM_Wave: plm = 0xEEFB; break;
    case SM_ITEM_Spazer: plm = 0xEEFF; break;
    case SM_ITEM_SpringBall: plm = 0xEF03; break;
    case SM_ITEM_Varia: plm = 0xEF07; break;
    case SM_ITEM_Plasma: plm = 0xEF13; break;
    case SM_ITEM_Grapple: plm = 0xEF17; break;
    case SM_ITEM_Morph: plm = 0xEF23; break;
    case SM_ITEM_ReserveTank: plm = 0xEF27; break;
    case SM_ITEM_Gravity: plm = 0xEF0B; break;
    case SM_ITEM_XRay: plm = 0xEF0F; break;
    case SM_ITEM_SpaceJump: plm = 0xEF1B; break;
    case SM_ITEM_ScrewAttack: plm = 0xEF1F; break;
    default: plm = 0xEFE0; break;
    }
    if (plm == 0xEFE0) plm += loc_type == SM_LTYPE_Chozo ? 4 : loc_type == SM_LTYPE_Hidden ? 8 : 0;
    else plm += loc_type == SM_LTYPE_Chozo ? 0x54 : loc_type == SM_LTYPE_Hidden ? 0xA8 : 0;
    return (uint16_t)plm;
}

static void put16(uint8_t *rom, uint32_t at, unsigned v) { rom[at] = (uint8_t)v; rom[at + 1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *rom, uint32_t at, uint32_t v) { put16(rom, at, v & 0xFFFF); put16(rom, at + 2, v >> 16); }

/* Patch.PlayerNameBytes: up to 12 characters, centred, upper case, then four zero bytes */
static void write_player_name(uint8_t *rom, uint32_t at, const char *name)
{
    char buf[13];
    int n = (int)strlen(name);
    if (n > 12) { n = 12; while (n > 0 && isspace((unsigned char)name[n - 1])) n--; }
    int pad = (12 - n) / 2;
    memset(buf, ' ', 12);
    for (int i = 0; i < n; i++) buf[pad + i] = (char)toupper((unsigned char)name[i]);
    memcpy(rom + at, buf, 12);
    put32(rom, at + 12, 0);
}

/* Patch.AsCreditsString: 32 tiles of (character, palette) */
static void write_credits(uint8_t *rom, uint32_t at, int color, const char *text, int align_left)
{
    char clean[64], line[32];
    int n = 0;
    for (const char *c = text; *c && n < 63; c++) {
        char u = (char)toupper((unsigned char)*c);
        if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || strchr(".,'!: ", u)) clean[n++] = u;
    }
    if (n > 30) n = 30;
    clean[n] = 0;
    memset(line, ' ', 32);
    if (align_left) memcpy(line + 1, clean, n);
    else {
        memset(line + 1, '.', 30 - n);
        memcpy(line + 1 + 30 - n, clean, n);
    }
    for (int i = 0; i < 32; i++) {
        char c = line[i];
        uint8_t t;
        switch (c) {
        case ' ': t = 0x7f; break;
        case '!': t = 0x1f; break;
        case ':': t = 0x1e; break;
        case '\'': t = 0x1d; break;
        case '_': t = 0x1c; break;
        case ',': t = 0x1b; break;
        case '.': t = 0x1a; break;
        default: t = (uint8_t)(c - 0x41); break;
        }
        rom[at + i * 2] = t;
        rom[at + i * 2 + 1] = (uint8_t)(c == ' ' ? 0 : color);
    }
}

static void write_guid(uint8_t *rom, uint32_t at, const char *guid)
{
    memset(rom + at, 0, 32);
    if (guid) memcpy(rom + at, guid, strlen(guid) < 32 ? strlen(guid) : 32);
}

void sm_patch(uint8_t *rom, const SmSeed *seed, const SmPatchIds *ids, uint8_t *hash_out)
{
    const SmGenSettings *s = &seed->s;
    int32_t shown_seed = s->race ? 0 : s->seed;
    SmzRng rnd;
    rng_init(&rnd, seed->patch_seed);

    /* WriteLocations: the item PLM at each location (the item table at 0x1C6000 is multiworld only) */
    for (int i = 0; i < SM_LOC_COUNT; i++)
        put16(rom, SM_LOCS[i].addr, sm_item_plm(seed->item_at[i].type, SM_LOCS[i].type));

    /* WritePlayerNames: the single world is "Player", id 0 */
    write_player_name(rom, 0x1C5000, "Player");

    /* WriteSeedData */
    unsigned config = (unsigned)(s->race ? 1 : 0) << 15 | (unsigned)s->logic << 8 | SM_VERSION_MAJOR << 4 | SM_VERSION_MINOR;
    put16(rom, 0x1C4F00, 0);
    put16(rom, 0x1C4F02, config);
    put32(rom, 0x1C4F04, (uint32_t)shown_seed);
    memset(rom + 0x1C4F08, 0, 8);
    write_guid(rom, 0x1C4F10, ids ? ids->seed_guid : NULL);
    write_guid(rom, 0x1C4F30, ids ? ids->world_guid : NULL);

    /* WriteSeedHash: Random.NextBytes, one sample per byte */
    for (int i = 0; i < 4; i++) rom[0x2FFF00 + i] = (uint8_t)rng_next(&rnd);
    if (hash_out) memcpy(hash_out, rom + 0x2FFF00, 4);

    /* WriteItemLocations: major items in the credits, by region name then location name */
    uint32_t at = 0x2F5240;
    for (int k = 0; k < SM_LOC_COUNT; k++) {
        int i = SM_CREDITS_ORDER[k];
        if (seed->item_at[i].cls != SM_CLASS_Major) continue;
        write_credits(rom, at, 0x04, seed->item_at[i].name, 1);
        write_credits(rom, at + 0x40, 0x18, SM_LOCS[i].name, 0);
        at += 0x80;
    }
    put32(rom, at, 0);

    /* WriteAnimalSurprise: Next(Count - 1), so the last patch is never picked */
    int a = rng_next_max(&rnd, SM_ANIMAL_COUNT - 1);
    sm_apply_ips(rom, SM_ROM_SIZE, SM_ANIMALS[a].ips, SM_ANIMALS[a].len);

    /* WriteGameTitle: $"SM{version}{logic}{seed:X8}" padded to 21 */
    char title[32];
    snprintf(title, sizeof title, "SM%d.%d%c%08X", SM_VERSION_MAJOR, SM_VERSION_MINOR, s->logic == SM_LOGIC_Tournament ? 'T' : 'C', (unsigned)shown_seed);
    size_t tl = strlen(title);
    memset(rom + 0x7FC0, ' ', 21);
    memcpy(rom + 0x7FC0, title, tl < 21 ? tl : 21);
}
