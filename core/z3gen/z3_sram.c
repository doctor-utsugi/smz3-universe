/* Port of InitialSram.php (alttp_php_randomizer, sporchia, MIT). */
#include <string.h>
#include "z3_sram.h"

#define B (s->b)
#define I(x) Z3_ITEM_##x

static int imin(int a, int b) { return a < b ? a : b; }
static int popcount8(int v) { int n = 0; for (; v; v >>= 1) n += v & 1; return n; }

void z3_sram_init(Z3Sram *s)
{
    memset(s, 0, sizeof *s);
    B[Z3_SRAM_ROOM_DATA + 0x20D] = 0xF0;
    B[Z3_SRAM_ROOM_DATA + 0x20F] = 0xF0;
    B[0x379] = 0x68;
    B[0x401] = 0xFF;
    B[0x402] = 0xFF;
}

void z3_sram_swordless_curtains(Z3Sram *s)
{
    z3_sram_set(s, Z3_SRAM_ROOM_DATA + 0x61, 0x80);
    z3_sram_set(s, Z3_SRAM_ROOM_DATA + 0x93, 0x80);
}

void z3_sram_starting_timer(Z3Sram *s, int seconds)
{
    uint32_t v = (uint32_t)seconds * 60;
    for (int i = 0; i < 4; i++) B[0x454 + i] = (uint8_t)(v >> (8 * i));
}

static void bottle(Z3Sram *s, int v)
{
    if (B[0x34F] < 4) {
        B[0x35C + B[0x34F]] = (uint8_t)v;
        B[0x34F]++;
    }
}

static void add_capped(Z3Sram *s, int at, int n, int cap) { B[at] = (uint8_t)imin(B[at] + n, cap); }

/* One case of setStartingEquipment's switch; rupees and capacities accumulate in r[0..2]. */
static void equip(Z3Sram *s, int item, bool rupee_bow, int *r)
{
    switch (item) {
    case I(L1Sword): B[0x359] = 1; B[0x417] = 1; break;
    case I(L1SwordAndShield): B[0x359] = 1; B[0x35A] = 1; B[0x417] = 1; B[0x422] = 1; break;
    case I(L2Sword): case I(MasterSword): B[0x359] = 2; B[0x417] = 2; break;
    case I(L3Sword): B[0x359] = 3; B[0x417] = 3; break;
    case I(L4Sword): B[0x359] = 4; B[0x417] = 4; break;
    case I(BlueShield): B[0x35A] = 1; B[0x422] = 1; break;
    case I(RedShield): B[0x35A] = 2; B[0x422] = 2; break;
    case I(MirrorShield): B[0x35A] = 3; B[0x422] = 3; break;
    case I(FireRod): B[0x345] = 1; break;
    case I(IceRod): B[0x346] = 1; break;
    case I(Hammer): B[0x34B] = 1; break;
    case I(Hookshot): B[0x342] = 1; break;
    case I(Bow): B[0x340] = 1; if (!rupee_bow) B[0x38E] |= 0x80; break;
    case I(BowAndArrows): B[0x340] = 2; B[0x38E] |= 0x80; if (rupee_bow) B[0x377] = 1; break;
    case I(SilverArrowUpgrade): B[0x38E] |= 0x40; if (rupee_bow) B[0x377] = 1; break;
    case I(BowAndSilverArrows):
        B[0x340] = 4; B[0x38E] |= 0x40;
        if (rupee_bow) B[0x377] = 1; else B[0x38E] |= 0x80;
        break;
    /* PHP's case 'Progressivebow' never matches 'ProgressiveBow', so a starting progressive bow does nothing. */
    case I(Boomerang): B[0x341] = 1; B[0x38C] |= 0x80; break;
    case I(RedBoomerang): B[0x341] = 2; B[0x38C] |= 0x40; break;
    case I(Mushroom): B[0x344] = 1; B[0x38C] |= 0x28; break;
    case I(Powder): B[0x344] = 2; B[0x38C] |= 0x10; break;
    case I(Bombos): B[0x347] = 1; break;
    case I(Ether): B[0x348] = 1; break;
    case I(Quake): B[0x349] = 1; break;
    case I(Lamp): B[0x34A] = 1; break;
    case I(Shovel): B[0x34C] = 1; B[0x38C] |= 0x04; break;
    case I(OcarinaInactive): B[0x34C] = 2; B[0x38C] |= 0x02; break;
    case I(OcarinaActive): B[0x34C] = 3; B[0x38C] |= 0x01; break;
    case I(CaneOfSomaria): B[0x350] = 1; break;
    case I(Bottle): bottle(s, 0x02); break;
    case I(BottleWithRedPotion): bottle(s, 0x03); break;
    case I(BottleWithGreenPotion): bottle(s, 0x04); break;
    case I(BottleWithBluePotion): bottle(s, 0x05); break;
    case I(BottleWithBee): bottle(s, 0x07); break;
    case I(BottleWithFairy): bottle(s, 0x06); break;
    case I(BottleWithGoldBee): bottle(s, 0x08); break;
    case I(CaneOfByrna): B[0x351] = 1; break;
    case I(Cape): B[0x352] = 1; break;
    case I(MagicMirror): B[0x353] = 2; break;
    case I(PowerGlove): B[0x354] = 1; break;
    case I(TitansMitt): B[0x354] = 2; break;
    case I(BookOfMudora): B[0x34E] = 1; break;
    case I(Flippers): B[0x356] = 1; B[0x379] |= 0x02; break;
    case I(MoonPearl): B[0x357] = 1; break;
    case I(BugCatchingNet): B[0x34D] = 1; break;
    case I(BlueMail): B[0x35B] = 1; B[0x46E] = 1; break;
    case I(RedMail): B[0x35B] = 2; B[0x46E] = 2; break;
    case I(Bomb): add_capped(s, 0x343, 1, 99); B[0x38D] |= 0x02; break;
    case I(ThreeBombs): add_capped(s, 0x343, 3, 99); B[0x38D] |= 0x02; break;
    case I(TenBombs): add_capped(s, 0x343, 10, 99); B[0x38D] |= 0x02; break;
    case I(OneRupee): r[0] += 1; break;
    case I(FiveRupees): r[0] += 5; break;
    case I(TwentyRupees): case I(TwentyRupees2): r[0] += 20; break;
    case I(FiftyRupees): r[0] += 50; break;
    case I(OneHundredRupees): r[0] += 100; break;
    case I(ThreeHundredRupees): r[0] += 300; break;
    case I(PendantOfCourage): B[0x374] |= 0x04; add_capped(s, 0x429, 1, 3); break;
    case I(PendantOfWisdom): B[0x374] |= 0x01; add_capped(s, 0x429, 1, 3); break;
    case I(PendantOfPower): B[0x374] |= 0x02; add_capped(s, 0x429, 1, 3); break;
    case I(HeartContainerNoAnimation): case I(BossHeartContainer): case I(HeartContainer):
        add_capped(s, 0x36C, 0x08, 0xA0); add_capped(s, 0x36D, 0x08, 0xA0); break;
    case I(PieceOfHeart):
        B[0x36B]++;
        if (B[0x36B] >= 4) {
            add_capped(s, 0x36C, 0x08 * (B[0x36B] / 4), 0xA0);
            B[0x36B] %= 4;
        }
        break;
    case I(Heart): add_capped(s, 0x36D, 0x08, 0xA0); break;
    case I(Arrow): add_capped(s, 0x377, 1, 99); break;
    case I(TenArrows): add_capped(s, 0x377, 10, 99); break;
    case I(SmallMagic): add_capped(s, 0x36E, 0x10, 0x80); break;
    case I(PegasusBoots): B[0x355] = 1; B[0x379] |= 0x04; break;
    case I(BombUpgrade5): r[1] += 5; break;
    case I(BombUpgrade10): r[1] += 10; break;
    case I(ArrowUpgrade5): r[2] += 5; break;
    case I(ArrowUpgrade10): r[2] += 10; break;
    case I(HalfMagic): B[0x37B] = 1; break;
    case I(QuarterMagic): B[0x37B] = 2; break;
    case I(ProgressiveSword): add_capped(s, 0x359, 1, 4); break;
    case I(ProgressiveShield): add_capped(s, 0x35A, 1, 3); break;
    case I(ProgressiveArmor): add_capped(s, 0x35B, 1, 2); break;
    case I(ProgressiveGlove): add_capped(s, 0x354, 1, 2); break;
    case I(MapLW): B[0x368] |= 0x01; break;
    case I(MapDW): B[0x368] |= 0x02; break;
    case I(MapA2): B[0x368] |= 0x04; break;
    case I(MapD7): B[0x368] |= 0x08; break;
    case I(MapD4): B[0x368] |= 0x10; break;
    case I(MapP3): B[0x368] |= 0x20; break;
    case I(MapD5): B[0x368] |= 0x40; break;
    case I(MapD3): B[0x368] |= 0x80; break;
    case I(MapD6): B[0x369] |= 0x01; break;
    case I(MapD1): B[0x369] |= 0x02; break;
    case I(MapD2): B[0x369] |= 0x04; break;
    case I(MapA1): B[0x369] |= 0x08; break;
    case I(MapP2): B[0x369] |= 0x10; break;
    case I(MapP1): B[0x369] |= 0x20; break;
    case I(MapH1): case I(MapH2): B[0x369] |= 0xC0; break;
    case I(CompassA2): B[0x364] |= 0x04; break;
    case I(CompassD7): B[0x364] |= 0x08; break;
    case I(CompassD4): B[0x364] |= 0x10; break;
    case I(CompassP3): B[0x364] |= 0x20; break;
    case I(CompassD5): B[0x364] |= 0x40; break;
    case I(CompassD3): B[0x364] |= 0x80; break;
    case I(CompassD6): B[0x365] |= 0x01; break;
    case I(CompassD1): B[0x365] |= 0x02; break;
    case I(CompassD2): B[0x365] |= 0x04; break;
    case I(CompassA1): B[0x365] |= 0x08; break;
    case I(CompassP2): B[0x365] |= 0x10; break;
    case I(CompassP1): B[0x365] |= 0x20; break;
    case I(CompassH1): case I(CompassH2): B[0x365] |= 0xC0; break;
    case I(BigKeyA2): B[0x366] |= 0x04; break;
    case I(BigKeyD7): B[0x366] |= 0x08; break;
    case I(BigKeyD4): B[0x366] |= 0x10; break;
    case I(BigKeyP3): B[0x366] |= 0x20; break;
    case I(BigKeyD5): B[0x366] |= 0x40; break;
    case I(BigKeyD3): B[0x366] |= 0x80; break;
    case I(BigKeyD6): B[0x367] |= 0x01; break;
    case I(BigKeyD1): B[0x367] |= 0x02; break;
    case I(BigKeyD2): B[0x367] |= 0x04; break;
    case I(BigKeyA1): B[0x367] |= 0x08; break;
    case I(BigKeyP2): B[0x367] |= 0x10; break;
    case I(BigKeyP1): B[0x367] |= 0x20; break;
    case I(BigKeyH1): case I(BigKeyH2): B[0x367] |= 0xC0; break;
    case I(KeyH1): case I(KeyH2): B[0x37C]++; B[0x37D]++; break;
    case I(KeyP1): B[0x37E]++; break;
    case I(KeyP2): B[0x37F]++; break;
    case I(KeyA1): B[0x380]++; break;
    case I(KeyD2): B[0x381]++; break;
    case I(KeyD1): B[0x382]++; break;
    case I(KeyD6): B[0x383]++; break;
    case I(KeyD3): B[0x384]++; break;
    case I(KeyD5): B[0x385]++; break;
    case I(KeyP3): B[0x386]++; break;
    case I(KeyD4): B[0x387]++; break;
    case I(KeyD7): B[0x388]++; break;
    case I(KeyA2): B[0x389]++; break;
    case I(Crystal1): B[0x37A] |= 0x02; break;
    case I(Crystal2): B[0x37A] |= 0x10; break;
    case I(Crystal3): B[0x37A] |= 0x40; break;
    case I(Crystal4): B[0x37A] |= 0x20; break;
    case I(Crystal5): B[0x37A] |= 0x04; break;
    case I(Crystal6): B[0x37A] |= 0x01; break;
    case I(Crystal7): B[0x37A] |= 0x08; break;
    default: break;
    }
}

void z3_sram_starting_equipment(Z3Sram *s, const Z3Items *items, int uncle_target, const Z3Cfg *cfg)
{
    int r[3] = { 0, 0, 0 };   /* rupees, bomb capacity, arrow capacity */
    bool hearts = false;      /* heartCount(0) < 1: PHP counts every Health item as 1 (its name check never matches) */
    for (int i = 0; i < Z3_ITEM_COUNT; i++)
        if (items->n[i] && Z3_ITEMS[i].cls == Z3_IC_Upgrade_Health) hearts = true;
    if (!hearts) B[0x36C] = B[0x36D] = 0x18;
    for (int i = 0; i < Z3_ITEM_COUNT; i++) {
        int target = i;   /* getTarget()->getRawName(): aliases stand for their target */
        if (i == Z3_ITEM_UncleSword) target = uncle_target == Z3_ITEM_NONE ? Z3_ITEM_ProgressiveSword : uncle_target;
        else if (i == Z3_ITEM_ShopKey) target = Z3_ITEM_KeyGK;
        else if (i == Z3_ITEM_ShopArrow) target = Z3_ITEM_Arrow;
        for (int n = 0; n < items->n[i]; n++) equip(s, target, cfg->rom_rupeeBow, r);
    }
    B[0x362] = B[0x360] = (uint8_t)(r[0] & 0xFF);
    B[0x363] = B[0x361] = (uint8_t)(r[0] >> 8);
    B[0x476] = (uint8_t)popcount8(B[0x37A]);
    B[0x429] = (uint8_t)popcount8(B[0x374]);
    B[0x417] = B[0x359];
    B[0x422] = B[0x35A];
    B[0x46E] = B[0x35B];
    z3_sram_set(s, 0x370, r[1]);
    z3_sram_set(s, 0x371, r[2]);
    if (cfg->mode_weapons == Z3_MODE_WEAPONS_SWORDLESS) {
        B[0x359] = 0xFF;
        B[0x417] = 0x00;
    }
}

void z3_sram_write(const Z3Sram *s, uint8_t *rom)
{
    memcpy(rom + 0x183000, s->b, Z3_SRAM_SIZE);
}
