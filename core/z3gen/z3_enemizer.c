/* Randomization::MakeRandomization with alttpr's boss-only options. alttpr applies the base patch plus the bytes the
   Enemizer logged; every write here is a logged one, so they go straight onto rom in the Enemizer's order. */
#include <string.h>
#include <stdio.h>
#include "z3_enemizer.h"
#include "z3_enemizer_gen.h"
#include "z3_rng.h"

#define ROM_SIZE 0x200000
#define NOP 0xEA

/* System.Random(seed) of .NET Core 2.0 (Knuth's subtractive generator), for Next(min, max). */
typedef struct { int32_t a[56]; int next, nextp; } NetRandom;

static void net_random_init(NetRandom *r, int32_t seed)
{
    const int32_t mbig = 0x7FFFFFFF;
    int32_t sub = seed == INT32_MIN ? mbig : (seed < 0 ? -seed : seed);
    int32_t mj = 161803398 - sub, mk = 1;
    memset(r, 0, sizeof *r);
    r->a[55] = mj;
    for (int i = 1; i < 55; i++) {
        int ii = (21 * i) % 55;
        r->a[ii] = mk;
        mk = mj - mk;
        if (mk < 0) mk += mbig;
        mj = r->a[ii];
    }
    for (int k = 1; k < 5; k++)
        for (int i = 1; i < 56; i++) {
            r->a[i] -= r->a[1 + (i + 30) % 55];
            if (r->a[i] < 0) r->a[i] += mbig;
        }
    r->next = 0;
    r->nextp = 21;
}

static int net_random_next(NetRandom *r, int min, int max)
{
    int n = r->next + 1 >= 56 ? 1 : r->next + 1, np = r->nextp + 1 >= 56 ? 1 : r->nextp + 1;
    int32_t v = r->a[n] - r->a[np];
    if (v == 0x7FFFFFFF) v--;
    if (v < 0) v += 0x7FFFFFFF;
    r->a[n] = v;
    r->next = n;
    r->nextp = np;
    double sample = v * (1.0 / 0x7FFFFFFF);
    return (int)(sample * ((int64_t)max - min)) + min;
}

uint32_t z3_enemizer_seed(uint32_t seed)
{
    Z3Rng r;
    z3_rng_init(&r, (uint64_t)seed ^ 0x454E454D495A4552ull);
    return (uint32_t)z3_rng_int(&r, 1, 999999999);
}

enum { T_ARMOS, T_LANMOLA, T_MOLDORM, T_HELMASAUR, T_ARRGHUS, T_MOTHULA, T_BLIND, T_KHOLDSTARE, T_VITREOUS,
       T_TRINEXX };

typedef struct {
    uint8_t pointer[2], graphics;
    const uint8_t *sprites;
    int n_sprites;
} EnBoss;

static const uint8_t armos_sp[] = { 0x05, 0x04, 0x53, 0x05, 0x07, 0x53, 0x05, 0x0A, 0x53, 0x08, 0x0A, 0x53,
                                    0x08, 0x07, 0x53, 0x08, 0x04, 0x53, 0x08, 0xE7, 0x19 };
static const uint8_t lanmola_sp[] = { 0x07, 0x06, 0x54, 0x07, 0x09, 0x54, 0x09, 0x07, 0x54 };
static const uint8_t moldorm_sp[] = { 0x09, 0x09, 0x09 };
static const uint8_t helmasaur_sp[] = { 0x06, 0x07, 0x92 };
static const uint8_t arrghus_sp[] = { 0x07, 0x07, 0x8C, 0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D,
                                      0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D,
                                      0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D,
                                      0x07, 0x07, 0x8D, 0x07, 0x07, 0x8D };
static const uint8_t mothula_sp[] = { 0x06, 0x08, 0x88 };
static const uint8_t blind_sp[] = { 0x05, 0x09, 0xCE };
static const uint8_t kholdstare_sp[] = { 0x05, 0x07, 0xA3, 0x05, 0x07, 0xA4, 0x05, 0x07, 0xA2 };
static const uint8_t vitreous_sp[] = { 0x05, 0x07, 0xBD };
static const uint8_t trinexx_sp[] = { 0x05, 0x07, 0xCB, 0x05, 0x07, 0xCC, 0x05, 0x07, 0xCD };

#define SP(a) a, (int)sizeof a
static const EnBoss BOSSES[] = {
    [T_ARMOS] = { { 0x87, 0xE8 }, 9, SP(armos_sp) },
    [T_LANMOLA] = { { 0xCB, 0xDC }, 11, SP(lanmola_sp) },
    [T_MOLDORM] = { { 0xC3, 0xD9 }, 12, SP(moldorm_sp) },
    [T_HELMASAUR] = { { 0x49, 0xE0 }, 21, SP(helmasaur_sp) },
    [T_ARRGHUS] = { { 0x97, 0xD9 }, 20, SP(arrghus_sp) },
    [T_MOTHULA] = { { 0x31, 0xDC }, 26, SP(mothula_sp) },
    [T_BLIND] = { { 0x54, 0xE6 }, 32, SP(blind_sp) },
    [T_KHOLDSTARE] = { { 0x01, 0xEA }, 22, SP(kholdstare_sp) },
    [T_VITREOUS] = { { 0x57, 0xE4 }, 22, SP(vitreous_sp) },
    [T_TRINEXX] = { { 0xBA, 0xE5 }, 23, SP(trinexx_sp) },
};

/* Boss::getEName() as ManualBossRandomizer reads it; Agahnim never sits in a dungeon. */
static int enemizer_boss(int z3_boss)
{
    switch (z3_boss) {
    case Z3_BOSS_Armos_Knights: return T_ARMOS;
    case Z3_BOSS_Lanmolas: return T_LANMOLA;
    case Z3_BOSS_Moldorm: return T_MOLDORM;
    case Z3_BOSS_Helmasaur_King: return T_HELMASAUR;
    case Z3_BOSS_Arrghus: return T_ARRGHUS;
    case Z3_BOSS_Mothula: return T_MOTHULA;
    case Z3_BOSS_Blind: return T_BLIND;
    case Z3_BOSS_Kholdstare: return T_KHOLDSTARE;
    case Z3_BOSS_Vitreous: return T_VITREOUS;
    case Z3_BOSS_Trinexx: return T_TRINEXX;
    default: return -1;
    }
}

enum { GT_NONE, GT1, GT2 };
typedef struct {
    int region, gt_level;      /* where alttpr keeps the boss: boss_of[region], or gt_boss[gt_level] */
    int room, sprite_ptr, shell_x, shell_y;
    bool clear_layer2;
    int gt;
    const uint8_t *extra;
    int n_extra;
} EnDungeon;

static const uint8_t gt1_extra[] = { 0x07, 0x07, 0xE3, 0x07, 0x08, 0xE3, 0x08, 0x07, 0xE3, 0x08, 0x08, 0xE3 };
static const uint8_t gt2_extra[] = { 0x18, 0x17, 0xD1, 0x1C, 0x03, 0xC5 };

/* BossRandomizer::FillDungeonPool's order. */
static const EnDungeon DUNGEONS[13] = {
    { Z3_REG_Eastern_Palace, -1, 200, 0x4D7BE, 0x2B, 0x28, false, GT_NONE, 0, 0 },
    { Z3_REG_Desert_Palace, -1, 51, 0x4D694, 0x0B, 0x28, false, GT_NONE, 0, 0 },
    { Z3_REG_Tower_of_Hera, -1, 7, 0x4D63C, 0x18, 0x16, false, GT_NONE, 0, 0 },
    { Z3_REG_Palace_of_Darkness, -1, 90, 0x4D6E2, 0x2B, 0x28, false, GT_NONE, 0, 0 },
    { Z3_REG_Swamp_Palace, -1, 6, 0x4D63A, 0x0B, 0x28, false, GT_NONE, 0, 0 },
    { Z3_REG_Skull_Woods, -1, 41, 0x4D680, 0x2B, 0x28, false, GT_NONE, 0, 0 },
    { Z3_REG_Thieves_Town, -1, 172, 0x4D786, 0x2B, 0x28, true, GT_NONE, 0, 0 },
    { Z3_REG_Ice_Palace, -1, 222, 0x4D7EA, 0x2B, 0x08, true, GT_NONE, 0, 0 },
    { Z3_REG_Misery_Mire, -1, 144, 0x4D74E, 0x0B, 0x28, true, GT_NONE, 0, 0 },
    { Z3_REG_Turtle_Rock, -1, 164, 0x4D776, 0x0B, 0x28, true, GT_NONE, 0, 0 },
    { Z3_REG_Ganons_Tower, 2, 28, 0x4D666, 0x2B, 0x28, false, GT1, SP(gt1_extra) },    /* bottom */
    { Z3_REG_Ganons_Tower, 1, 108, 0x4D706, 0x0B, 0x28, false, GT2, SP(gt2_extra) },   /* middle */
    { Z3_REG_Ganons_Tower, 0, 77, 0x4D6C8, 0x18, 0x16, false, GT_NONE, 0, 0 },         /* top */
};

#define ROOM_TRINEXX 164
#define ROOM_KHOLDSTARE 222
#define SHELL_TRINEXX 0xFF2
#define SHELL_KHOLDSTARE 0xF95
#define OBJECT_POINTERS 0xF8000
#define MAX_OBJECTS 512

/* DungeonObjectDataTable: one room's object list, read when DungeonObjectDataPointerCollection is built. */
typedef struct {
    uint8_t header[2];
    uint8_t obj[3][MAX_OBJECTS][3];
    uint8_t door[3][MAX_OBJECTS][2];
    int n_obj[3], n_door[3];
} Room;

static int read_room(const uint8_t *rom, int room, Room *out)
{
    int p = OBJECT_POINTERS + room * 3;
    int snes = rom[p] | rom[p + 1] << 8 | rom[p + 2] << 16;
    int at = ((snes & 0x7F0000) >> 1) | (snes & 0x7FFF);
    if (at + 2 > ROM_SIZE) return -1;
    memset(out, 0, sizeof *out);
    out->header[0] = rom[at];
    out->header[1] = rom[at + 1];
    int layer = 0;
    bool door = false;
    for (at += 2; at + 3 <= ROM_SIZE;) {
        if (rom[at] == 0xF0 && rom[at + 1] == 0xFF) {
            door = true;
            at += 2;
        }
        if (rom[at] == 0xFF && rom[at + 1] == 0xFF) {
            if (layer == 2) return 0;
            layer++;
            door = false;
            at += 2;
            continue;
        }
        if (door) {
            if (out->n_door[layer] == MAX_OBJECTS) return -1;
            memcpy(out->door[layer][out->n_door[layer]++], rom + at, 2);
            at += 2;
        } else {
            if (out->n_obj[layer] == MAX_OBJECTS) return -1;
            memcpy(out->obj[layer][out->n_obj[layer]++], rom + at, 3);
            at += 3;
        }
    }
    return -1;
}

/* LayerDungeonObject's OID; only subtype 3 objects (third byte 0xF8 and up) can be a shell. */
static int object_id(const uint8_t b[3])
{
    if (b[0] >= 0xFC) return (b[2] & 0x3F) + 0x100;
    if (b[2] >= 0xF8) return b[2] << 4 | 0x80 | (b[1] & 0x03) << 2 | (b[0] & 0x03);
    return b[2];
}

static int add_shell(Room *r, int x, int y, bool clear_layer2, int shell)
{
    r->header[0] = 0xF0;
    if (clear_layer2) r->n_obj[1] = 0;
    if (r->n_obj[1] == MAX_OBJECTS) return -1;
    uint8_t *o = r->obj[1][r->n_obj[1]++];
    o[0] = (uint8_t)(((x << 2) & 0xFC) | (shell & 0x03));
    o[1] = (uint8_t)(((y << 2) & 0xFC) | ((shell >> 2) & 0x03));
    o[2] = (uint8_t)(0xF0 | ((shell >> 4) & 0x0F));
    return 0;
}

static void remove_shell(Room *r, int shell)
{
    int k = 0;
    for (int i = 0; i < r->n_obj[1]; i++)
        if (object_id(r->obj[1][i]) != shell) memcpy(r->obj[1][k++], r->obj[1][i], 3);
    r->n_obj[1] = k;
}

/* DungeonObjectDataTable::Length: counts a door of layers 1 and 2 as 3 bytes and leaves out the closing 0xFFFF, so
   the next room starts there (overlapping or leaving a gap) exactly as the Enemizer lays them out. */
static int room_length(const Room *r)
{
    int n = 2;
    for (int l = 0; l < 2; l++)
        n += r->n_obj[l] * 3 + (r->n_door[l] > 0 ? 2 : 0) + r->n_door[l] * 3 + 2;
    return n + r->n_obj[2] * 3 + 2 + r->n_door[2] * 2 + 2;
}

static int write_room(uint8_t *rom, int room, const Room *r, int at)
{
    int p = OBJECT_POINTERS + room * 3;
    int bank = (at >> 16) & 0xFF, hi = (at >> 8) & 0xFF;   /* Utilities.PCToSnesAddress (2 MB build) */
    bank = (bank * 2) & 0xFF;
    if (hi >= 0x80) bank = (bank + 1) & 0xFF;
    else hi += 0x80;
    rom[p] = (uint8_t)at;
    rom[p + 1] = (uint8_t)hi;
    rom[p + 2] = (uint8_t)bank;

    int w = at;
    if (w + room_length(r) + 4 > ROM_SIZE) return -1;
    rom[w++] = r->header[0];
    rom[w++] = r->header[1];
    for (int l = 0; l < 3; l++) {
        for (int i = 0; i < r->n_obj[l]; i++)
            for (int k = 0; k < 3; k++) rom[w++] = r->obj[l][i][k];
        if (l == 2 || r->n_door[l] > 0) {
            rom[w++] = 0xF0;
            rom[w++] = 0xFF;
        }
        for (int i = 0; i < r->n_door[l]; i++) {
            rom[w++] = r->door[l][i][0];
            rom[w++] = r->door[l][i][1];
        }
        rom[w++] = 0xFF;
        rom[w++] = 0xFF;
    }
    rom[w++] = 0xFF;
    rom[w++] = 0xFF;
    return at + room_length(r);
}

static void write_bytes(uint8_t *rom, int at, const uint8_t *b, int n)
{
    memcpy(rom + at, b, (size_t)n);
}

/* OptionFlags::ToByteArray for Enemizer::writeOptionsFile with only boss shuffle on (type 0 simple, 1 full,
   2 random). */
static void option_flags(uint8_t out[0x50], int boss_type)
{
    static const uint8_t flags[] = {
        0, 3, 0, 0, 1, 0, 1, 0, 0, 10,            /* enemies off (type Chaos), health default, zero damage, spawn rate */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* every absorbable type */
        0, 1, 0, 0, 0, 300 & 0xFF, 0, 0, 200,     /* boss madness, randomize bosses, type, boss health and damage */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* behaviour, palettes, modes, spoilers, link palette, pots, music */
        1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 3,       /* bootleg magic, debug, custom bosses, andy, heart beep 2, ... bees 3 */
        1, 196, 0, 4, 0, 1, 1,                    /* the debug switches alttpr sends */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             /* OHKO ... Agahnim bounce balls */
    };
    memset(out, 0, 0x50);
    memcpy(out, flags, sizeof flags);
    out[26] = (uint8_t)boss_type;
}

int z3_enemize(uint8_t *rom, const Z3World *w, uint32_t enemizer_seed)
{
    static Room rooms[13];
    int boss[13], room_of[13], n_rooms = 0;
    for (int d = 0; d < 13; d++) {
        const EnDungeon *dg = &DUNGEONS[d];
        boss[d] = enemizer_boss(dg->gt_level < 0 ? w->boss_of[dg->region] : w->gt_boss[dg->gt_level]);
        if (boss[d] < 0) return -1;
    }
    int seed = (int)(enemizer_seed % 1000000000u);

    rom[0x7FD7] = 0x0C;   /* ExpandRom, which also writes Version.CurrentVersion */
    static const uint8_t version[8] = "6.0.32";
    write_bytes(rom, Z3_EN_ENEMIZER_INFO_TABLE + 12, version, 8);
    char en[16];
    uint8_t info[12] = { 0 };
    snprintf(en, sizeof en, "EN%d", seed);
    memcpy(info, en, strlen(en) < 12 ? strlen(en) : 12);
    write_bytes(rom, Z3_EN_ENEMIZER_INFO_TABLE, info, 12);
    uint8_t flags[0x50];
    int type = w->cfg.enemizer_bossShuffle == Z3_ENEMIZER_BOSSSHUFFLE_FULL ? 1
             : w->cfg.enemizer_bossShuffle == Z3_ENEMIZER_BOSSSHUFFLE_RANDOM ? 2 : 0;
    option_flags(flags, type);
    write_bytes(rom, Z3_EN_ENEMIZER_INFO_TABLE + 20, flags, 0x50);

    for (size_t i = 0; i < Z3_EN_BASE_PATCH_LEN;) {
        const uint8_t *p = Z3_EN_BASE_PATCH + i;
        int at = p[0] | p[1] << 8 | p[2] << 16, n = p[3] | p[4] << 8;
        if (at + n > ROM_SIZE) return -1;
        write_bytes(rom, at, p + 5, n);
        i += 5 + (size_t)n;
    }

    rom[Z3_EN_ENEMIZERFLAGS + 3] = 0;   /* RandomizeSprites (no sprite on hit) */
    rom[Z3_EN_ENEMIZERFLAGS + 1] = 1;   /* CloseBlindDoor */

    /* DungeonObjectDataPointerCollection reads every room before the bosses are written. */
    for (int d = 0; d < 13; d++) {
        const EnDungeon *dg = &DUNGEONS[d];
        bool touched = (boss[d] == T_TRINEXX && dg->room != ROOM_TRINEXX)
                    || (boss[d] == T_KHOLDSTARE && dg->room != ROOM_KHOLDSTARE)
                    || (boss[d] != T_TRINEXX && dg->room == ROOM_TRINEXX)
                    || (boss[d] != T_KHOLDSTARE && dg->room == ROOM_KHOLDSTARE);
        room_of[d] = -1;
        if (touched) {
            if (read_room(rom, dg->room, &rooms[n_rooms])) return -1;
            room_of[d] = n_rooms++;
        }
    }

    for (int d = 0; d < 13; d++) {
        const EnDungeon *dg = &DUNGEONS[d];
        const EnBoss *b = &BOSSES[boss[d]];
        int header = Z3_EN_ROOM_HEADER_TABLE + dg->room * 14;
        Room *r = room_of[d] >= 0 ? &rooms[room_of[d]] : NULL;
        rom[dg->sprite_ptr] = b->pointer[0];
        rom[dg->sprite_ptr + 1] = b->pointer[1];
        rom[header + 3] = b->graphics;
        if (boss[d] == T_TRINEXX && dg->room != ROOM_TRINEXX) {
            if (add_shell(r, dg->shell_x, dg->shell_y - 2, dg->clear_layer2, SHELL_TRINEXX)) return -1;
            rom[header] = 0x60;
            rom[header + 4] = 0x04;
        }
        if (boss[d] == T_KHOLDSTARE && dg->room != ROOM_KHOLDSTARE) {
            if (add_shell(r, dg->shell_x, dg->shell_y, dg->clear_layer2, SHELL_KHOLDSTARE)) return -1;
            rom[header] = 0xE0;
            rom[header + 4] = 0x01;
        }
        if (boss[d] != T_TRINEXX && dg->room == ROOM_TRINEXX) remove_shell(r, SHELL_TRINEXX);
        if (boss[d] != T_KHOLDSTARE && dg->room == ROOM_KHOLDSTARE) remove_shell(r, SHELL_KHOLDSTARE);
        if (dg->gt != GT_NONE) {   /* new sprite lists in the sprite pointer table's unused space */
            int at = dg->gt == GT1 ? 0x4D87E : 0x4D8B6;
            rom[dg->sprite_ptr] = (uint8_t)at;
            rom[dg->sprite_ptr + 1] = (uint8_t)(at >> 8);
            rom[at++] = 0x00;
            write_bytes(rom, at, b->sprites, b->n_sprites);
            at += b->n_sprites;
            int n = dg->gt == GT1 && boss[d] == T_ARRGHUS ? 6 : dg->n_extra;   /* Arrghus: only two fairies */
            write_bytes(rom, at, dg->extra, n);
            at += n;
            rom[at] = 0xFF;
        }
    }

    /* WriteChangesToRom: modified rooms by room id, one after the other. */
    int at = Z3_EN_MODIFIED_ROOM_OBJECT_TABLE;
    for (int room = 0; room < 320; room++)
        for (int d = 0; d < 13; d++)
            if (DUNGEONS[d].room == room && room_of[d] >= 0) {
                at = write_room(rom, room, &rooms[room_of[d]], at);
                if (at < 0) return -1;
            }
    for (int i = 0; i < 15; i++) rom[0xEA081 + i] = NOP;   /* RemoveBlindSpawnCode */
    rom[0x4DE81] = 0x00;                                     /* RemoveMaidenFromThievesTown */

    if (rom[0x301FC] == 0xDA)   /* Retro's rupee arrows: the bush table follows */
        for (int i = 0; i < 22; i++)
            if (rom[Z3_EN_SPRITE_BUSH_SPAWN_ITEM_TABLE + i] == 0xE1) rom[Z3_EN_SPRITE_BUSH_SPAWN_ITEM_TABLE + i] = 0xDA;

    NetRandom nr;
    net_random_init(&nr, seed);
    uint8_t eyes = (uint8_t)net_random_next(&nr, 0, 8);   /* BootlegMagic: Moldorm's eye count */
    rom[0x0EDBB3] = eyes;
    rom[0x1B0102] = eyes;
    rom[Z3_EN_ENEMIZERFLAGS + 4] = 0;   /* AgahnimBounceBalls */
    for (int i = 0; i < 4; i++) rom[0x04B37E + i] = NOP;   /* Trinexx's ice floor */
    rom[0x180033] = 0x80;   /* SetHeartBeepSpeed(Quarter): alttpr sends 2 */
    return 0;
}
