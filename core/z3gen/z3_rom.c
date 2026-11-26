/* Port of World::writeToRom, setEscapeFills, writePrizePacksToRom and the Rom.php setters they call
   (alttp_php_randomizer, sporchia, MIT): every world state and logic, no enemizer, timer or multiworld.
   Texts and credits: World::$texts/$credits (z3_texts_randomize) plus what the special locations' writeItem sets,
   written by Rom::writeCredits/writeText (z3_text.c). */
#include <string.h>
#include "z3_rom.h"
#include "z3_romdata_gen.h"
#include "z3_sram.h"

#define TWENTY_RUPEES2 0x47   /* item.overflow.replacement.* default, TwentyRupees2 */

/* The rom.* and item.overflow.* keys World::__construct derives from the pool, functionality and dungeon items. */
typedef struct {
    int of_sword, of_shield, of_armor, of_bottle, of_bow, of_bossheart, of_poh;
    int cape[3], powder_fairy, fill_health, fill_magic, stun_items;
    bool byrna_invuln, catch_fairies, catch_bees, silvers_only_ganon, no_fairy_drops;
    bool map_on_pickup, dungeon_count_pickup;
    bool vanilla_keys, vanilla_big_keys, vanilla_maps, vanilla_compasses;   /* rom.vanilla*: glitched logic */
    int free_item_text, free_item_menu;
} RomCfg;

static void rom_cfg(const Z3Cfg *c, RomCfg *o)
{
    *o = (RomCfg){ 4, 3, 2, 4, 2, 10, 24, { 0x04, 0x08, 0x10 }, 0xE3, 0xA0, 0x80, 0x03,
                   true, true, true, false, false, false, false, false, false, false, false, 0, 0 };
    switch (c->dungeonItems) {   /* falls through like the PHP switch */
    case Z3_DUNGEONITEMS_FULL: o->free_item_text |= 0x18; o->free_item_menu |= 0x02; /* fall through */
    case Z3_DUNGEONITEMS_MCS: o->free_item_text |= 0x11; o->free_item_menu |= 0x01; /* fall through */
    case Z3_DUNGEONITEMS_MC:
        o->map_on_pickup = o->dungeon_count_pickup = true;
        o->free_item_text |= 0x16; o->free_item_menu |= 0x0C;
        break;
    default: break;
    }
    if (c->logic == Z3_LOGIC_HYBRIDMAJORGLITCHES || c->logic == Z3_LOGIC_MAJORGLITCHES || c->logic == Z3_LOGIC_NOLOGIC
        || c->canOneFrameClipUW)
    {
        o->free_item_menu |= 0x10;
        o->vanilla_keys = !c->region_wildKeys;
        o->vanilla_big_keys = !c->region_wildBigKeys;
        o->vanilla_maps = !c->region_wildMaps;
        o->vanilla_compasses = !c->region_wildCompasses;
    }
    switch (c->item_pool) {
    case Z3_ITEM_POOL_SUPEREXPERT: o->of_sword = 2; o->of_armor = 0; o->of_shield = 0; o->of_bow = 1; o->of_bossheart = 0; o->of_poh = 0; break;
    case Z3_ITEM_POOL_EXPERT: o->of_sword = 2; o->of_armor = 0; o->of_shield = 1; o->of_bow = 1; o->of_bossheart = 2; o->of_poh = 8; break;
    case Z3_ITEM_POOL_HARD: o->of_sword = 3; o->of_armor = 0; o->of_shield = 2; o->of_bow = 1; o->of_bossheart = 6; o->of_poh = 16; break;
    case Z3_ITEM_POOL_CROWD_CONTROL: o->of_sword = 4; o->of_armor = 1; o->of_shield = 0; o->of_bow = 1; o->of_bossheart = 1; o->of_poh = 20; break;
    default: break;
    }
    switch (c->item_functionality) {
    case Z3_ITEM_FUNCTIONALITY_SUPEREXPERT:
        o->cape[0] = 0x01; o->cape[1] = 0x02; o->cape[2] = 0x02; o->powder_fairy = 0x79;
        o->fill_health = 0x00; o->fill_magic = 0x00; o->stun_items = 0x00;
        break;
    case Z3_ITEM_FUNCTIONALITY_EXPERT:
        o->cape[0] = 0x02; o->cape[1] = 0x04; o->cape[2] = 0x08; o->powder_fairy = 0xD8;
        o->fill_health = 0x20; o->fill_magic = 0x20; o->stun_items = 0x00;
        break;
    case Z3_ITEM_FUNCTIONALITY_HARD:
        o->cape[0] = 0x02; o->cape[1] = 0x04; o->cape[2] = 0x08; o->powder_fairy = 0xD8;
        o->fill_health = 0x38; o->fill_magic = 0x40; o->stun_items = 0x02;
        break;
    default: break;
    }
    if (c->item_functionality != Z3_ITEM_FUNCTIONALITY_NORMAL) {
        o->byrna_invuln = o->catch_fairies = false;
        o->silvers_only_ganon = o->no_fairy_drops = true;
    }
    if (c->mode_weapons == Z3_MODE_WEAPONS_SWORDLESS) o->of_bow = 2;
}

static void w8(uint8_t *rom, int32_t at, int v) { rom[at] = (uint8_t)v; }
static void w16(uint8_t *rom, int32_t at, int v) { rom[at] = (uint8_t)v; rom[at + 1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *rom, int32_t at, uint32_t v) { for (int i = 0; i < 4; i++) rom[at + i] = (uint8_t)(v >> (8 * i)); }
static void wbytes(uint8_t *rom, int32_t at, const uint8_t *b, size_t n) { memcpy(rom + at, b, n); }
#define WB(at, ...) do { static const uint8_t b_[] = { __VA_ARGS__ }; wbytes(rom, at, b_, sizeof b_); } while (0)

/* ------------------------------------------------------------------ locations */

static bool is_prize(int loc) { return strncmp(Z3_LOCS[loc].kind, "Prize", 5) == 0; }

/* The texts the special locations' writeItem sets (Rom::setCredit/setText), by the item's target. */
static void write_item_text(Z3Seed *s, int loc, int item, bool generic)
{
    static const struct { const char *kind; int text, credit, credit_lt; } special[] = {
        { "Pedestal", Z3_TXT_mastersword_pedestal_translated, Z3_CREDIT_pedestal, Z3_LT_PEDESTAL_CREDITS },
        { "Drop_Ether", Z3_TXT_tablet_ether_book, -1, -1 },
        { "Drop_Bombos", Z3_TXT_tablet_bombos_book, -1, -1 },
        { "Npc_Uncle", -1, Z3_CREDIT_house, Z3_LT_UNCLE_CREDITS },
        { "Npc_Zora", -1, Z3_CREDIT_zora, Z3_LT_ZORA_CREDITS },
        { "Npc_Witch", -1, Z3_CREDIT_witch, Z3_LT_WITCH_CREDITS },
        { "Npc_BugCatchingKid", -1, Z3_CREDIT_kakariko2, Z3_LT_KID_CREDITS },
        { "Dig_HauntedGrove", -1, Z3_CREDIT_grove, Z3_LT_GROVE_CREDITS },
    };
    static const int text_lt[] = { Z3_LT_PEDESTAL, Z3_LT_ETHER, Z3_LT_BOMBOS };
    for (int i = 0; i < (int)(sizeof special / sizeof *special); i++) {
        if (strcmp(Z3_LOCS[loc].kind, special[i].kind)) continue;
        const char *t;
        if (special[i].credit >= 0 && (t = Z3_LOC_TEXT[special[i].credit_lt][item])) z3_texts_credit(&s->tx, special[i].credit, t);
        if (special[i].text >= 0 && (t = Z3_LOC_TEXT[text_lt[i]][generic ? Z3_ITEM_KeyGK : item]))   /* not the credits */
            z3_texts_set(&s->tx, special[i].text, t);
    }
}

/* Location::writeItem (and Prize's music, HeraBasement's key fanfare, the special locations' texts). */
static void write_item(uint8_t *rom, Z3Seed *s, const RomCfg *rc, Z3Rng *r, int loc)
{
    int item = s->w.item_at[loc];
    int bytes_of = item == Z3_ITEM_UncleSword && s->uncle_target != Z3_ITEM_NONE ? s->uncle_target : item;
    bool generic = s->w.cfg.rom_genericKeys && Z3_ITEMS[item].cls == Z3_IC_Key;   /* every key writes as KeyGK */
    write_item_text(s, loc, bytes_of, generic);
    /* glitched logic: a dungeon's own items write as the vanilla Key, BigKey, Map, Compass */
    int ic = Z3_ITEMS[item].cls, reg = Z3_LOCS[loc].region;
    bool escape_spot = loc == Z3_LOC_Secret_Passage || loc == Z3_LOC_Links_Uncle;
    if (rc->vanilla_keys && ic == Z3_IC_Key && z3_region_item(reg, item) && (!escape_spot || item != Z3_ITEM_KeyH2))
        bytes_of = Z3_ITEM_Key;
    if (rc->vanilla_big_keys && ic == Z3_IC_BigKey && z3_region_item(reg, item)) bytes_of = Z3_ITEM_BigKey;
    if (rc->vanilla_maps && ic == Z3_IC_Map && z3_region_item(reg, item) && (!escape_spot || item != Z3_ITEM_MapH2))
        bytes_of = Z3_ITEM_Map;
    if (rc->vanilla_compasses && ic == Z3_IC_Compass && z3_region_item(reg, item)) bytes_of = Z3_ITEM_Compass;
    if (generic) bytes_of = Z3_ITEM_KeyGK;
    if (loc == s->bow_alt_loc) bytes_of = Z3_ITEM_ProgressiveBowAlternate;   /* new Item\Bow('ProgressiveBow', [0x65]) */
    const Z3ItemRom *ib = &Z3_ITEM_ROM[bytes_of];
    const Z3LocRom *lr = &Z3_LOC_ROM[loc];
    for (int k = 0; k < lr->n; k++)
        if (lr->addr[k] >= 0 && k < ib->n && ib->b[k] >= 0) w8(rom, lr->addr[k], ib->b[k]);
    for (int k = 0; lr->ext_key && k < 3; k++)
        if (lr->ext[k] >= 0) w8(rom, lr->ext[k], lr->ext_key == 't' ? ib->t[k] : ib->m[k]);
    if (is_prize(loc)) {
        const Z3RegionRom *rg = &Z3_REGION_ROM[Z3_LOCS[loc].region];
        if (rg->n_music) {
            int music;
            if (rc->map_on_pickup) {   /* Arr::first(fy_shuffle([0x11, 0x16])) */
                int m[2] = { 0x11, 0x16 };
                z3_rng_shuffle(r, m, 2);
                music = m[0];
            } else {
                music = Z3_ITEMS[item].cls == Z3_IC_Pendant ? 0x11 : 0x16;
            }
            for (int i = 0; i < rg->n_music; i++) w8(rom, rg->music[i], music);
        }
    }
    if (strcmp(Z3_LOCS[loc].kind, "Standing_HeraBasement") == 0)
        w8(rom, 0x4E3BB, item == Z3_ITEM_Key || item == Z3_ITEM_KeyP3 ? 0xE4 : 0xEB);
}

static int total_item_count(const Z3World *w)
{
    int n = 0;
    for (int l = 0; l < Z3_LOC_COUNT; l++) {
        const char *k = Z3_LOCS[l].kind;
        if (!z3_loc_exists(w, l) || is_prize(l) || !strcmp(k, "Fountain") || !strcmp(k, "Medallion") || !strcmp(k, "Trade"))
            continue;
        n++;
    }
    return n;
}

static int region_of_item(const Z3World *w, int item)
{
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (w->item_at[l] == item && z3_loc_exists(w, l)) return Z3_LOCS[l].region;
    return -1;
}

/* ------------------------------------------------------------------ World::setEscapeFills */

static void escape_refills(uint8_t *rom, int flags, int rupees, const int uncle[3], const int zelda[3], const int mantle[3])
{
    w8(rom, 0x18004E, flags);
    w16(rom, 0x180183, rupees);
    for (int i = 0; i < 3; i++) {
        w8(rom, 0x180185 + i, uncle[i]);
        w8(rom, 0x180188 + i, zelda[i]);
        w8(rom, 0x18018B + i, mantle[i]);
    }
}

static void set_escape_fills(uint8_t *rom, const Z3Seed *s)
{
    Z3World w = s->w;   /* a copy: canKillEscapeThings is checked with ignoreCanKillEscapeThings off */
    Z3Items it;
    memset(&it, 0, sizeof it);
    w.col = NULL;
    z3_items_add(&it, w.item_at[Z3_LOC_Links_Uncle], 1);
    w.cfg.ignoreCanKillEscapeThings = 0;
    if (!z3_canKillEscapeThings(&w, &it))
        for (int i = 0; i < Z3_ITEM_COUNT; i++) z3_items_add(&it, i, w.pre.n[i]);
    w.cfg.ignoreCanKillEscapeThings = s->w.cfg.ignoreCanKillEscapeThings;
    static const int zero[3] = { 0, 0, 0 };
    if (z3_hasSword(&w, &it, 1) || z3_has(&it, Z3_ITEM_Hammer, 1)) {
        escape_refills(rom, 0x00, 300, zero, zero, zero);
    } else if (z3_has(&it, Z3_ITEM_FireRod, 1) || z3_has(&it, Z3_ITEM_CaneOfSomaria, 1)
               || (z3_has(&it, Z3_ITEM_CaneOfByrna, 1) && w.cfg.enemizer_enemyHealth == Z3_ENEMIZER_ENEMYHEALTH_DEFAULT)) {
        static const int u[3] = { 0x80, 0, 0 }, z[3] = { 0x20, 0, 0 };
        escape_refills(rom, 0x04, 300, u, z, z);
    } else if (z3_canShootArrows(&w, &it, 1)) {
        static const int u[3] = { 0, 0, 70 }, z[3] = { 0, 0, 10 };
        escape_refills(rom, 0x01, 300, u, z, z);
    } else if (z3_has(&it, Z3_ITEM_TenBombs, 1) || w.cfg.logic != Z3_LOGIC_NOLOGIC) {
        static const int u[3] = { 0, 50, 0 }, z[3] = { 0, 3, 0 };
        escape_refills(rom, 0x02, 300, u, z, z);
    }
    /* rom.EscapeAssist defaults off: setEscapeAssist is never called. */
}

/* ------------------------------------------------------------------ World::writePrizePacksToRom */

static void write_prize_packs(uint8_t *rom, const Z3Seed *s, const RomCfg *rc, Z3Rng *r)
{
    /* getDropsPool: config/item.php 'drop' in key order (BeeGood and Bee have count 0), fy_shuffled */
    static const uint8_t kind[] = { 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3 };
    static const int count[] = { 13, 9, 7, 6, 7, 1, 2, 6, 3, 5, 3, 1 };
    int pool[63], n = 0;
    for (int k = 0; k < 12; k++)
        for (int i = 0; i < count[k]; i++) pool[n++] = kind[k];
    z3_rng_shuffle(r, pool, n);
    uint8_t drops[63];   /* packs 0..6, then the empty slots pull (3), crab (2), stun (1), fish (1) from the pool */
    for (int p = 0; p < 7; p++) memcpy(drops + 8 * p, s->packs[p], 8);
    for (int i = 0; i < 7; i++) drops[56 + i] = (uint8_t)pool[i];
    if (rc->no_fairy_drops)
        for (int i = 0; i < 63; i++) {
            if (drops[i] == 0xE0) drops[i] = 0xDF;
            if (drops[i] == 0xE3) drops[i] = 0xD8;
        }
    if (s->w.cfg.rom_rupeeBow) {   /* arrows drop as rupees, and setOverworldDigPrizes */
        for (int i = 0; i < 63; i++) {
            if (drops[i] == 0xE1) drops[i] = 0xDA;
            if (drops[i] == 0xE2) drops[i] = 0xDB;
        }
        static const uint8_t dig[] = { 0xB2, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD9, 0xD9, 0xD9, 0xD9, 0xD9,
            0xDA, 0xDA, 0xDA, 0xDA, 0xDA, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDD, 0xDD, 0xDD,
            0xDD, 0xDD, 0xDE, 0xDE, 0xDE, 0xDE, 0xDE, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xDA,
            0xDA, 0xDA, 0xDA, 0xDA, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0xE3, 0xE3, 0xE3, 0xE3, 0xE3 };
        wbytes(rom, 0x180100, dig, sizeof dig);
    }
    wbytes(rom, 0x37A78, drops, 56);
    w8(rom, 0xEFBD4, drops[56]); w8(rom, 0xEFBD5, drops[57]); w8(rom, 0xEFBD6, drops[58]);   /* setPullTreePrizes */
    w8(rom, 0x329C8, drops[59]); w8(rom, 0x329C4, drops[60]);                               /* setRupeeCrabPrizes */
    w8(rom, 0x37993, drops[61]);                                                            /* setStunnedSpritePrize */
    w8(rom, 0xE82CC, drops[62]);                                                            /* setFishSavePrize */
}

/* ------------------------------------------------------------------ Rom::setupCustomShops */

static void setup_shops(uint8_t *rom, const Z3World *w)
{
    uint8_t shop_data[8 * Z3_SHOP_COUNT], items_data[8 * 3 * Z3_SHOP_COUNT + 8];
    int ns = 0, ni = 0, active = 0, id = 0, sram = 0;
    for (int s = 0; s < Z3_SHOP_COUNT; s++) active += w->shop[s].active;
    for (int s = 0; s < Z3_SHOP_COUNT; s++) {
        if (!w->shop[s].active) continue;
        const Z3ShopRom *sr = &Z3_SHOP_ROM[s];
        int sid = id == active - 1 ? 0xFF : id, inv = 0;
        for (int k = 0; k < 3; k++) inv += w->shop[s].item[k] != Z3_ITEM_NONE;
        if (sr->write_addr >= 0) w8(rom, sr->write_addr, sr->write_val);   /* writeExtraData */
        uint8_t b[8] = { (uint8_t)sid, (uint8_t)sr->room, (uint8_t)(sr->room >> 8), sr->door, 0x00,
                         (uint8_t)((sr->config & 0xFC) + inv), w->shop[s].keeper, (uint8_t)sram };
        memcpy(shop_data + ns, b, 8);
        ns += 8;
        sram += sr->kind == Z3_SHOP_KIND_TAKEANY ? 1 : inv;   /* PHP throws past 36; never reached here */
        for (int k = 0; k < 3; k++) {
            int item = w->shop[s].item[k];
            if (item == Z3_ITEM_NONE) continue;
            int price = w->shop[s].price[k];
            uint8_t e[8] = { (uint8_t)sid, (uint8_t)Z3_ITEM_ROM[item].b[0], (uint8_t)price, (uint8_t)(price >> 8),
                             w->shop[s].max[k], 0xFF, 0x00, 0x00 };
            memcpy(items_data + ni, e, 8);
            ni += 8;
        }
        id++;
    }
    memset(items_data + ni, 0xFF, 8);
    wbytes(rom, 0x184800, shop_data, (size_t)ns);
    wbytes(rom, 0x184900, items_data, (size_t)ni + 8);
}

/* ------------------------------------------------------------------ Rom setters with fixed data */

static void set_wishing_well_chests(uint8_t *rom)   /* setWishingWellChests(true) */
{
    static const uint8_t room276[] = {
        0xE1, 0x00, 0x10, 0xAC, 0x0D, 0x29, 0x9A, 0x01, 0x58, 0x98, 0x0F, 0x11, 0xB1, 0x61, 0xFC, 0x4D, 0x81, 0x20,
        0xDA, 0x02, 0x2C, 0xE1, 0x61, 0xFC, 0xBE, 0x81, 0x61, 0xB1, 0x62, 0x54, 0xDA, 0x02, 0xFD, 0x8D, 0x83, 0x44,
        0xE1, 0x62, 0xFD, 0x1E, 0x83, 0x31, 0xA5, 0xC9, 0x31, 0xA4, 0x69, 0x31, 0xC0, 0x22, 0x4D, 0xA4, 0x69, 0x31,
        0xC6, 0xF9, 0x49, 0xC6, 0xF9, 0x10, 0x99, 0xA0, 0x5C, 0x99, 0xA2, 0x0B, 0x98, 0x60, 0x0D, 0xE8, 0xC0, 0x55,
        0xE8, 0xC0, 0x0B, 0x92, 0x00, 0x73, 0x98, 0x60, 0xFE, 0x69, 0x00, 0x98, 0xA3, 0x61, 0xFE, 0x6B, 0x81, 0xFE,
        0xAB, 0x85, 0xA9, 0xC8, 0x61, 0xFE, 0xAE, 0x81, 0xFF, 0x69, 0x02, 0xD8, 0xA3, 0x62, 0xFF, 0x6B, 0x83, 0xFF,
        0x2B, 0x87, 0xC9, 0xC8, 0x62, 0xFF, 0x2E, 0x83, 0xFE, 0xA1, 0x00, 0xAA, 0x21, 0x61, 0xFE, 0xA6, 0x81, 0xFF,
        0x21, 0x02, 0xCA, 0x21, 0x62, 0xFF, 0x26, 0x83, 0xBB, 0x23, 0xFA, 0xC3, 0x23, 0xFA, 0xBB, 0x2B, 0xFA, 0xC3,
        0x2B, 0xFA, 0xBB, 0x53, 0xFA, 0xC3, 0x53, 0xFA, 0xB0, 0xA0, 0x3D, 0x98, 0x13, 0xC0, 0x98, 0x52, 0xC0, 0x88,
        0x00, 0x60, 0x90, 0x00, 0x60, 0xE8, 0x00, 0x60, 0xF0, 0x00, 0x60, 0x98, 0xCA, 0xC0, 0xD8, 0xCA, 0xC0, 0xD8,
        0x13, 0xC0, 0xD8, 0x52, 0xC0, 0xFC, 0xBD, 0x85, 0xFD, 0x1D, 0x87, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xFF, 0x80,
        0x2E, 0x61, 0x0E, 0x81, 0x0E, 0xFF, 0xFF,
    };
    WB(0xE9AE, 0x14, 0x01);
    WB(0xE9CF, 0x14, 0x01);
    wbytes(rom, 0x1F714, room276, sizeof room276);
}

static void remove_uncles_shield(uint8_t *rom)
{
    WB(0x6D253, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x0E);
    WB(0x6D25B, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x0E);
    WB(0x6D283, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x0E);
    WB(0x6D28B, 0x00, 0x00, 0xf7, 0xff, 0x00, 0x0E);
    WB(0x6D2CB, 0x00, 0x00, 0xf6, 0xff, 0x02, 0x0E);
    WB(0x6D2FB, 0x00, 0x00, 0xf7, 0xff, 0x02, 0x0E);
    WB(0x6D313, 0x00, 0x00, 0xe4, 0xff, 0x08, 0x0E);
}

static void remove_uncles_sword(uint8_t *rom)
{
    WB(0x6D263, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x0E);
    WB(0x6D26B, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x0E);
    WB(0x6D293, 0x00, 0x00, 0xf6, 0xff, 0x00, 0x0E);
    WB(0x6D29B, 0x00, 0x00, 0xf7, 0xff, 0x00, 0x0E);
    WB(0x6D2B3, 0x00, 0x00, 0xf6, 0xff, 0x02, 0x0E);
    WB(0x6D2BB, 0x00, 0x00, 0xf6, 0xff, 0x02, 0x0E);
    WB(0x6D2E3, 0x00, 0x00, 0xf7, 0xff, 0x02, 0x0E);
    WB(0x6D2EB, 0x00, 0x00, 0xf7, 0xff, 0x02, 0x0E);
    WB(0x6D31B, 0x00, 0x00, 0xe4, 0xff, 0x08, 0x0E);
    WB(0x6D323, 0x00, 0x00, 0xe4, 0xff, 0x08, 0x0E);
}

/* ------------------------------------------------------------------ World::writeToRom */

int z3_rom_write(uint8_t *rom, Z3Seed *s, const Z3GenSettings *g, Z3Rng *r)
{
    const Z3Cfg *c = &s->w.cfg;
    RomCfg rc;
    Z3Sram sram;
    rom_cfg(c, &rc);
    z3_sram_init(&sram);

    for (int reg = 0; reg < Z3_REG_COUNT; reg++) {   /* each region: placed locations, then the empty ones as Nothing */
        for (int l = 0; l < Z3_LOC_COUNT; l++)
            if (Z3_LOCS[l].region == reg && z3_loc_exists(&s->w, l) && s->w.item_at[l] != Z3_ITEM_NONE)
                write_item(rom, s, &rc, r, l);
        for (int l = 0; l < Z3_LOC_COUNT; l++)
            if (Z3_LOCS[l].region == reg && z3_loc_exists(&s->w, l) && s->w.item_at[l] == Z3_ITEM_NONE) {
                s->w.item_at[l] = Z3_ITEM_Nothing;
                write_item(rom, s, &rc, r, l);
            }
    }
    if (c->mode_state == Z3_MODE_STATE_STANDARD) set_escape_fills(rom, s);

    w16(rom, 0x180167, c->item_Goal_Required);   /* setGoalRequiredCount */
    w16(rom, 0x180165, 0x280E);                  /* setGoalIcon('triforce') */

    WB(0x18016B, 0x04, 0x02, 0x01);              /* setCaneOfByrnaSpikeCaveUsage() */
    WB(0x18016E, 0x04, 0x08, 0x10);              /* setCapeSpikeCaveUsage() */
    w8(rom, 0x180195, 0x08);                     /* setByrnaCaveSpikeDamage */
    WB(0x45C42, 0x04, 0x02, 0x01);               /* Byrna magic per cycle */
    for (int i = 0; i < 3; i++) w8(rom, 0x3ADA7 + i, rc.cape[i]);   /* setCapeRegularMagicUsage */
    w8(rom, 0x18004F, rc.byrna_invuln);
    w8(rom, 0x36DD0, rc.powder_fairy);
    w8(rom, 0x180084, rc.fill_health);
    w8(rom, 0x180085, rc.fill_magic);
    w8(rom, 0x34FD6, rc.catch_fairies ? 0xF0 : 0x80);
    w8(rom, 0xF5D73, rc.catch_bees ? 0xF0 : 0x80);
    w8(rom, 0xF5F10, rc.catch_bees ? 0xF0 : 0x80);
    w8(rom, 0x180180, rc.stun_items);
    w8(rom, 0x180181, rc.silvers_only_ganon);
    w16(rom, 0x180036, 0);                       /* setRupoorValue(item.value.Rupoor ?: 0) */
    w8(rom, 0x180086, 0x00);                     /* setGanonAgahnimRng('table') */
    w8(rom, 0x18019A, c->crystals_tower < 0 ? 0 : c->crystals_tower > 7 ? 7 : c->crystals_tower);
    w8(rom, 0x1801A6, c->crystals_ganon < 0 ? 0 : c->crystals_ganon > 7 ? 7 : c->crystals_ganon);

    w8(rom, 0x180172, c->rom_genericKeys ? 0x01 : 0x00);   /* setGenericKeys */
    setup_shops(rom, &s->w);
    bool rb = c->rom_rupeeBow;                   /* setRupeeArrow */
    w8(rom, 0x30052, rb ? 0xDB : 0xE2); w8(rom, 0x301FC, rb ? 0xDA : 0xE1);
    if (rb) { WB(0xECB4E, 0xA9, 0x00, 0xEA, 0xEA); WB(0xF0D96, 0xA9, 0x00, 0xEA, 0xEA); }
    else { WB(0xECB4E, 0xAF, 0x77, 0xF3, 0x7E); WB(0xF0D96, 0xAF, 0x77, 0xF3, 0x7E); }
    w8(rom, 0x180175, rb ? 0x01 : 0x00); w16(rom, 0x180176, rb ? 0x0A : 0); w16(rom, 0x180178, rb ? 0x32 : 0);
    if (rb) WB(0xEDA5, 0x35, 0x41);
    else WB(0xEDA5, 0x43, 0x44);
    set_wishing_well_chests(rom);
    w8(rom, 0x348DB, 0x2A); w8(rom, 0x348EB, 0x05);       /* setWishingWellUpgrade(false) */
    WB(0x01F810, 0x1A, 0x1E, 0x01, 0x1A, 0x1E, 0x01);     /* setHyliaFairyShop(true) */
    w8(rom, 0x18017E, 0x01);                              /* setRestrictFairyPonds(true) */
    w8(rom, 0x180090, rc.of_sword); w8(rom, 0x180091, TWENTY_RUPEES2);
    w8(rom, 0x180092, rc.of_shield); w8(rom, 0x180093, TWENTY_RUPEES2);
    w8(rom, 0x180094, rc.of_armor); w8(rom, 0x180095, TWENTY_RUPEES2);
    w8(rom, 0x180096, rc.of_bottle); w8(rom, 0x180097, TWENTY_RUPEES2);
    w8(rom, 0x180098, rc.of_bow); w8(rom, 0x180099, TWENTY_RUPEES2);
    w8(rom, 0x180182, 0x01);                              /* setSilversEquip('collection') */
    {
        const uint8_t sub[] = { 0x12, 0x01, 0x35, 0xFF, 0x51, 0x06, 0x52, 0xFF, 0x53, 0x06, 0x54, 0xFF,
                                0x58, 0x01, (uint8_t)(rb ? 0x36 : 0x43), 0xFF, 0x3E, (uint8_t)rc.of_bossheart, TWENTY_RUPEES2, 0xFF,
                                0x17, (uint8_t)rc.of_poh, TWENTY_RUPEES2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        wbytes(rom, 0x184000, sub, sizeof sub);           /* setSubstitutions */
    }

    int invincible;   /* setGanonInvincible */
    switch (c->goal) {
    case Z3_GOAL_TRIFORCE_HUNT: w8(rom, 0x180194, 0x01); invincible = 0x01; break;   /* enableTriforceTurnIn */
    case Z3_GOAL_PEDESTAL: invincible = 0x01; break;
    case Z3_GOAL_DUNGEONS: invincible = 0x02; break;
    case Z3_GOAL_GANONHUNT: z3_sram_open_pyramid(&sram); invincible = 0x05; break;
    case Z3_GOAL_FAST_GANON: z3_sram_open_pyramid(&sram); invincible = 0x04; break;
    case Z3_GOAL_COMPLETIONIST: invincible = 0x0B; break;
    default: invincible = 0x04; break;
    }
    w8(rom, 0x1801A8, invincible);

    if (rc.map_on_pickup) {
        int green = region_of_item(&s->w, Z3_ITEM_PendantOfCourage);
        int c5 = region_of_item(&s->w, Z3_ITEM_Crystal5), c6 = region_of_item(&s->w, Z3_ITEM_Crystal6);
        w16(rom, 0x18017A, green < 0 ? 0 : Z3_REGION_ROM[green].map_reveal);
        w16(rom, 0x18017C, (c5 < 0 ? 0 : Z3_REGION_ROM[c5].map_reveal) | (c6 < 0 ? 0 : Z3_REGION_ROM[c6].map_reveal));
    }
    w8(rom, 0x18003B, rc.map_on_pickup);                  /* setMapMode */
    w8(rom, 0x18003C, rc.dungeon_count_pickup ? 0x01 : 0x00);   /* setCompassMode */
    WB(0x187000, 0x08, 0x08, 0x06, 0x06, 0x02, 0x0A, 0x0E, 0x08, 0x08, 0x08, 0x06, 0x08, 0x0C, 0x1B, 0x00, 0x00);
    w8(rom, 0x18016A, rc.free_item_text);
    w8(rom, 0x180045, rc.free_item_menu);
    int digs = z3_rng_int(r, 1, 30);                      /* setDiggingGameRng */
    w8(rom, 0x180020, digs);
    w8(rom, 0xEFD95, digs);
    for (int i = 0; i < 1024; i++)                        /* writeRNGBlock: pack('C', 256) keeps the low byte, 0 */
        w8(rom, 0x178000 + i, z3_rng_int(r, 0, 0x100) & 0xFF);
    write_prize_packs(rom, s, &rc, r);

    if (c->region_swordsInPool) WB(0x1FC16, 0xB1, 0xC6, 0xF9, 0xC9, 0xC6, 0xF9);   /* setPyramidFairyChests */
    else WB(0x1FC16, 0xA8, 0xB8, 0x3D, 0xD0, 0xB8, 0x3D);
    w8(rom, 0x180029, c->region_swordsInPool ? 0x01 : 0x00);   /* setSmithyQuickItemGive */

    w8(rom, 0x180174, 0x00);                              /* setGameState: setFixFakeWorld(false) */
    if (c->mode_state == Z3_MODE_STATE_STANDARD) {
        w8(rom, 0x180038, 0x01);                          /* setSewersLampCone(true); progress values 0 OR nothing */
    } else {                                              /* setOpenMode (Inverted's starts with it) */
        w8(rom, 0x180038, 0x00);
        z3_sram_open_castle_gate(&sram);
        z3_sram_progress_indicator(&sram, 0x02);
        z3_sram_progress_flags(&sram, 0x14);
        z3_sram_starting_entrance(&sram, 0x01);
    }
    if (c->mode_state == Z3_MODE_STATE_INVERTED) {        /* setInvertedMode */
        for (int i = 0; i < Z3_INV_WRITES_LEN; i++)
            wbytes(rom, Z3_INV_WRITES[i].addr, Z3_INV_DATA + Z3_INV_WRITES[i].off, Z3_INV_WRITES[i].n);
        w8(rom, 0x180174, 0x01);                          /* setFixFakeWorld(true) */
        for (int i = 0; i < Z3_INV_TEXTS_LEN; i++)        /* after World::$texts, so these win */
            z3_texts_set_pause(&s->tx, Z3_INV_TEXTS[i].key, Z3_INV_TEXTS[i].s, Z3_INV_TEXTS[i].pause);
    }
    bool swordless = c->mode_weapons == Z3_MODE_WEAPONS_SWORDLESS;   /* setSwordlessMode */
    w8(rom, 0x18003F, swordless);
    w8(rom, 0x180041, swordless);
    w8(rom, 0x180044, swordless);                         /* setHammerTablet */
    w8(rom, 0x18005D, 0x00);                              /* setHammerBarrier(false) */
    if (swordless) z3_sram_swordless_curtains(&sram);
    if (c->mode_state != Z3_MODE_STATE_INVERTED)          /* setLockAgahnimDoorInEscape; Inverted wrote 0x02 */
        w8(rom, 0x180169, c->logic == Z3_LOGIC_NOGLITCHES);

    int uncle = s->w.item_at[Z3_LOC_Links_Uncle];
    if (Z3_ITEMS[uncle].cls != Z3_IC_Sword) remove_uncles_sword(rom);   /* the UncleSword alias is no Item\Sword */
    if (Z3_ITEMS[uncle].cls != Z3_IC_Shield || uncle != Z3_ITEM_L1SwordAndShield) remove_uncles_shield(rom);

    z3_sram_starting_equipment(&sram, &s->w.pre, s->uncle_target, c);
    w8(rom, 0x186FFF, 0x02);                              /* setBallNChainDungeon */
    WB(0x180080, 50, 50, 70, 70);                         /* setCapacityUpgradeFills */
    WB(0x180190, 0x00, 0x00, 0x00);                       /* setClockMode('off') */
    w32(rom, 0x180204, 0);                                /* setBlueClock(0) */
    w32(rom, 0x180200, 0);                                /* setRedClock(0) */
    w32(rom, 0x180208, 0);                                /* setGreenClock(0) */
    z3_sram_starting_timer(&sram, 0);

    bool major = c->logic == Z3_LOGIC_HYBRIDMAJORGLITCHES || c->logic == Z3_LOGIC_MAJORGLITCHES || c->logic == Z3_LOGIC_NOLOGIC;
    if (major || c->logic == Z3_LOGIC_OVERWORLDGLITCHES) {
        if (major) w8(rom, 0x1800A1, 0x00);               /* setSwampWaterLevel(false) */
        w8(rom, 0x1800A2, 0x00);                          /* setPreAgahnimDarkWorldDeathInDungeon(false) */
        w8(rom, 0x180042, 0x01);                          /* setSaveAndQuitFromBossRoom(true) */
        w8(rom, 0x1800A3, 0x00);                          /* setWorldOnAgahnimDeath(false) */
        w8(rom, 0x180210, major ? 0x01 : 0x02);           /* setRandomizerSeedType */
        w8(rom, 0x180212, major ? 0x60 : 0x40);           /* setWarningFlags */
        w8(rom, 0x180358, 0x01);                          /* setAllowAccidentalMajorGlitch(true) */
        w8(rom, 0x1800A4, 0x00);                          /* setSQEGFix(false) */
        w8(rom, 0x159A8, 0x02);                           /* setZeldaMirrorFix(false) */
    } else {
        w8(rom, 0x180042, 0x01);                          /* No Glitches: setSaveAndQuitFromBossRoom(true) */
        w8(rom, 0x1800A3, 0x01);                          /* setWorldOnAgahnimDeath(true) */
        w8(rom, 0x180358, 0x00);                          /* setAllowAccidentalMajorGlitch(false) */
        w8(rom, 0x1800A4, 0x01);                          /* setSQEGFix(true) */
        w8(rom, 0x159A8, 0x04);                           /* setZeldaMirrorFix(true) */
    }

    bool triforce_hud = c->goal == Z3_GOAL_TRIFORCE_HUNT || c->goal == Z3_GOAL_GANONHUNT || c->item_Goal_Required > 0;
    w8(rom, 0x180039, triforce_hud ? 0 : c->goal == Z3_GOAL_COMPLETIONIST);   /* enableHudItemCounter */
    /* crystals.tower === 0: prepSeed keeps a chosen count as the request's string, so only a random 0 is an int. */
    if (g->crystals_tower < 0 && c->crystals_tower == 0) z3_sram_open_ganons_tower(&sram);
    w8(rom, 0x180211, 0x04);                              /* setGameType('item') */
    w8(rom, 0x18008E, 0x00);                              /* setPseudoBoots(false) */
    w8(rom, 0x187032, 0x01);                              /* enableFastRom(true) */

    static uint8_t credits[0x1000];                       /* writeCredits */
    uint16_t ptrs[Z3_CREDIT_COUNT + 1];
    int n = z3_credits_data(&s->tx, credits, sizeof credits, ptrs);
    wbytes(rom, 0x181500, credits, (size_t)n);
    for (int i = 0; i <= Z3_CREDIT_COUNT; i++) w16(rom, 0x76CC0 + 2 * i, ptrs[i]);
    n = z3_text_table(&s->tx, rom + 0xE0000);             /* writeText */
    if (n < 0) return -1;                                 /* Text::getByteArray: "Too BIG" */

    z3_sram_write(&sram, rom);                            /* writeInitialSram */
    w16(rom, 0x180196, total_item_count(&s->w));          /* setTotalItemCount */
    return 0;
}

/* ------------------------------------------------------------------ outside writeToRom */

/* Rom::rummageTable calls Tournament::apply, an empty stub in the public repo, so only the flag is written. */
void z3_rom_set_tournament(uint8_t *rom, bool on)
{
    w8(rom, 0x180213, on ? 0x01 : 0x00);
    w8(rom, 0x180214, on ? 0x00 : 0x01);
}

void z3_rom_set_seed_string(uint8_t *rom, const char *s21)
{
    size_t n = strlen(s21);
    memcpy(rom + 0x7FC0, s21, n < 21 ? n : 21);   /* substr($seed, 0, 21): a shorter string writes fewer bytes */
}

void z3_rom_set_hash(uint8_t *rom, const uint8_t icons[5])
{
    wbytes(rom, 0x180215, icons, 5);
}

void z3_rom_cosmetics(uint8_t *rom, const Z3GenSettings *g)
{
    static const uint8_t beep[5] = { 0x00, 0x10, 0x20, 0x40, 0x80 };     /* off, double, normal, half, quarter */
    static const uint8_t speed[4] = { 0x04, 0x08, 0x10, 0xE8 };          /* slow, normal, fast, instant */
    if (g->heart_beep >= 0 && g->heart_beep < 5) w8(rom, 0x180033, beep[g->heart_beep]);
    if (g->menu_speed >= 0 && g->menu_speed < 4) {
        bool fast = g->menu_speed == 3;
        w8(rom, 0x180048, speed[g->menu_speed]);
        w8(rom, 0x6DD9A, fast ? 0x20 : 0x11);
        w8(rom, 0x6DF2A, fast ? 0x20 : 0x12);
        w8(rom, 0x6E0E9, fast ? 0x20 : 0x12);
    }
    if (g->quickswap >= 0) w8(rom, 0x18004B, g->quickswap ? 0x01 : 0x00);
}

int z3_rom_shortcut_hooks(uint8_t *rom)
{
    /* the frame hook at $00:8056 calls $A1:8000; the new hook goes in the unused end of bank $3F (PC 0x1FFF00) */
    enum { SITE = 0x000056, HOOK = 0x1FFF00 };
    static const uint8_t CALL[4] = {0x22, 0x00, 0x80, 0xA1}, TO_HOOK[4] = {0x22, 0x00, 0xFF, 0xBF};
    /* bit 7 of the Y item ($0202) asks for $0D:DB7F while on foot, as the SMZ3 ROM's hook (smz_rom_shortcut_hooks) */
    static const uint8_t CODE[] = {
        0x08, 0xE2, 0x30,               /* PHP : SEP #$30 */
        0xAF, 0x02, 0x02, 0x7E,         /* LDA $7E0202 */
        0x10, 0x16,                     /* BPL done */
        0x29, 0x7F,                     /* AND #$7F */
        0x8F, 0x02, 0x02, 0x7E,         /* STA $7E0202 */
        0xAF, 0x10, 0x00, 0x7E,         /* LDA $7E0010: the module */
        0xC9, 0x07, 0xF0, 0x04,         /* CMP #$07 : BEQ refresh (dungeon) */
        0xC9, 0x09, 0xD0, 0x04,         /* CMP #$09 : BNE done (overworld) */
        0x22, 0x7F, 0xDB, 0x0D,         /* refresh: JSL $0DDB7F, the HUD's Y item icon and $0303 */
        0x28,                           /* done: PLP */
        0x5C, 0x00, 0x80, 0xA1,         /* JML $A18000: the frame hook this replaces */
    };
    if (memcmp(rom + SITE, CALL, 4)) return -1;
    for (size_t i = 0; i < sizeof CODE; i++) if (rom[HOOK + i] != 0x00) return -1;
    wbytes(rom, HOOK, CODE, sizeof CODE);
    wbytes(rom, SITE, TO_HOOK, 4);
    return 0;
}

/* The web flow never calls it (prepSeed returns a patch; resources/js/rom.js fixes the checksum on save); the
   alttp:randomize console command does, before saving. */
void z3_rom_update_checksum(uint8_t *rom)
{
    uint32_t sum = 0x1FE;
    for (int i = 0; i < Z3_ROM_SIZE; i++)
        if (i < 0x7FDC || i >= 0x7FE0) sum += rom[i];
    uint16_t checksum = (uint16_t)sum, inverse = checksum ^ 0xFFFF;
    w16(rom, 0x7FDC, inverse);
    w16(rom, 0x7FDE, checksum);
}
