/* Hand-written part of the world: rewards, bosses and the dispatch around the generated region logic. */
#include <string.h>
#include "smz3_world.h"
#include "smz3_world_gen.h"

static int region_of_loc[512];
static int region_of_loc_ready;

static void ready(void)
{
    if (region_of_loc_ready) return;
    for (int i = 0; i < SMZ_LOC_COUNT; i++) region_of_loc[SMZ_LOCS[i].id] = SMZ_LOCS[i].region;
    region_of_loc_ready = 1;
}

void smz_world_init(SmzWorld *w)
{
    memset(w, 0, sizeof *w);
    w->ganon_crystals = 7;
    w->gt_crystals = 7;
    w->tourian_bosses = 4;
    for (int r = 0; r < REG_COUNT; r++) {
        w->reward_of[r] = (uint8_t)SMZ_REGIONS[r].default_reward;
        w->boss_of[r] = (uint8_t)SMZ_REGIONS[r].default_boss;
        w->prereq[r] = (uint8_t)SMZ_REGIONS[r].default_prereq;
    }
}

bool loc_available(const SmzWorld *w, int loc, const SmzProg *p)
{
    ready();
    return region_can_enter(w, region_of_loc[loc], p, true) && loc_access(w, loc, p, true);
}

/* The location holding each dungeon's boss (and reward) when boss and item share a location. */
static int boss_location(int region)
{
    switch (region) {
    case REG_EasternPalace: return LOC_EasternPalaceArmosKnights;
    case REG_DesertPalace: return LOC_DesertPalaceLanmolas;
    case REG_TowerOfHera: return LOC_TowerOfHeraMoldorm;
    case REG_PalaceOfDarkness: return LOC_PalaceOfDarknessHelmasaurKing;
    case REG_SwampPalace: return LOC_SwampPalaceArrghus;
    case REG_SkullWoods: return LOC_SkullWoodsMothula;
    case REG_ThievesTown: return LOC_ThievesTownBlind;
    case REG_IcePalace: return LOC_IcePalaceKholdstare;
    case REG_MiseryMire: return LOC_MiseryMireVitreous;
    case REG_TurtleRock: return LOC_TurtleRockTrinexx;
    case REG_HyruleCastle: return LOC_HyruleCastleZeldasCell;
    default: return -1;
    }
}

bool region_can_beat_boss(const SmzWorld *w, int r, const SmzProg *p, bool tracking)
{
    (void)tracking;
    switch (r) {
    case REG_KraidsLair: return can_enter_KraidsLair(w, p, true) && P_CardBrinstarBoss(p);
    case REG_InnerMaridia: return can_enter_InnerMaridia(w, p, true) && InnerMaridia_CanDefeatDraygon(w, p, true);
    case REG_LowerNorfairEast:
        return can_enter_LowerNorfairEast(w, p, true) && LowerNorfairEast_CanExit(w, p) && P_CardLowerNorfairBoss(p) &&
               CanUsePowerBombs(p, &w->cfg) && P_Super(p);
    case REG_WreckedShip: return can_enter_WreckedShip(w, p, true) && WreckedShip_CanUnlockShip(w, p);
    case REG_CastleTower: return region_can_retrieve_reward(w, r, p, tracking);
    case REG_GanonsTower: return loc_available(w, LOC_GanonsTowerMoldormChest, p) && P_MasterSword(p) && p->items[ITEM_SilverArrows] > 0;
    default: {
        int loc = boss_location(r);
        return loc >= 0 && loc_available(w, loc, p);
    }
    }
}

bool region_can_retrieve_reward(const SmzWorld *w, int r, const SmzProg *p, bool tracking)
{
    switch (r) {
    case REG_KraidsLair: case REG_InnerMaridia: case REG_LowerNorfairEast: return region_can_beat_boss(w, r, p, tracking);
    case REG_WreckedShip: return can_enter_WreckedShip(w, p, true) && WreckedShip_CanUnlockShip(w, p);
    case REG_CastleTower: return can_enter_CastleTower(w, p, true) && P_Lamp(p) && P_KeyCT(p) >= 2 && P_Sword(p);
    default: return region_can_beat_boss(w, r, p, tracking);
    }
}

bool can_aquire_all(const SmzWorld *w, const SmzProg *p, bool req, int reward)
{
    for (int r = 0; r < REG_COUNT; r++)
        if (SMZ_REGIONS[r].has_reward && w->reward_of[r] == reward && !region_can_retrieve_reward(w, r, p, req)) return false;
    return true;
}

static bool can_aquire(const SmzWorld *w, const SmzProg *p, bool req, int reward)
{
    for (int r = 0; r < REG_COUNT; r++)
        if (SMZ_REGIONS[r].has_reward && w->reward_of[r] == reward) return region_can_retrieve_reward(w, r, p, req);
    return false;
}

bool CheckAgahnim(const SmzWorld *w, const SmzProg *p, bool requireRewards)
{
    return P_Agahnim(p) || (!requireRewards && can_aquire(w, p, requireRewards, REWARD_Agahnim));
}

int can_defeat_boss_count(const SmzWorld *w, const SmzProg *p, bool req, int bossset)
{
    int n = 0;
    (void)bossset;   /* only BOSSSET_SM exists: the four Super Metroid bosses */
    for (int r = 0; r < REG_COUNT; r++) {
        int b = w->boss_of[r];
        if ((b == BOSS_Kraid || b == BOSS_Phantoon || b == BOSS_Draygon || b == BOSS_Ridley) && region_can_beat_boss(w, r, p, req)) n++;
    }
    return n;
}
