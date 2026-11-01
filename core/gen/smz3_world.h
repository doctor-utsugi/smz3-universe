/* The SMZ3 world in C: what is in logic and where the items are. Hand-written; the data is in smz3_world_gen.{h,c}. */
#ifndef SMZ3_WORLD_H
#define SMZ3_WORLD_H

#include "smz3_logic.h"
#include "smz3_itemcats.h"

enum { LTYPE_Regular, LTYPE_HeraStandingKey, LTYPE_Pedestal, LTYPE_Ether, LTYPE_Bombos, LTYPE_NotInDungeon, LTYPE_Visible, LTYPE_Chozo, LTYPE_Hidden };
enum { RK_Z3, RK_SM };
enum { BOSSSET_SM = 1 };

#define SMZ_MAX_REGIONS 64

struct SmzWorld {
    SmzCfg cfg;
    bool zelda_keysanity, metroid_keysanity;
    int ganon_crystals, gt_crystals, tourian_bosses;
    uint8_t item_at[512];                 /* item type placed at each location (ITEM_Nothing when empty) */
    uint8_t reward_of[SMZ_MAX_REGIONS];   /* reward type each region gives */
    uint8_t boss_of[SMZ_MAX_REGIONS];     /* boss type of each region */
    uint8_t prereq[SMZ_MAX_REGIONS];      /* medallion required by Misery Mire / Turtle Rock */
};

typedef struct {
    int id;
    uint32_t rom;
    int type, vanilla, region;
    int mem_addr, mem_flag;
    int weight;
} SmzLocDef;

typedef struct {
    const char *name;
    int kind, default_reward, default_boss, default_prereq;
    int has_reward;
    int shuffled;   /* 1: always, 2: depends on the Metroid boss token option */
    int weight, overworld;
} SmzRegionDef;

extern const SmzLocDef SMZ_LOCS[];
extern const int SMZ_LOC_COUNT;
extern const SmzRegionDef SMZ_REGIONS[];

/* A world with the default rewards and bosses and nothing placed, like the tracker builds it. */
void smz_world_init(SmzWorld *w);

/* Is the location in logic with these items: region entry plus its own requirement. */
bool loc_allow(const SmzWorld *w, int loc, int item, const SmzProg *p);
bool loc_always_allow(const SmzWorld *w, int loc, int item, const SmzProg *p);
bool region_has_item(int region, int item);

static inline bool item_is_dungeon(int t) { return (SMZ_ITEM_CATS[t] & (ICAT_SmallKey | ICAT_BigKey | ICAT_Compass | ICAT_Map)) != 0; }

bool loc_available(const SmzWorld *w, int loc, const SmzProg *p);
bool loc_access(const SmzWorld *w, int loc, const SmzProg *p, bool req);
bool can_aquire_all(const SmzWorld *w, const SmzProg *p, bool req, int reward);
int can_defeat_boss_count(const SmzWorld *w, const SmzProg *p, bool req, int bossset);
bool region_can_retrieve_reward(const SmzWorld *w, int region, const SmzProg *p, bool tracking);
bool region_can_beat_boss(const SmzWorld *w, int region, const SmzProg *p, bool tracking);

#endif
