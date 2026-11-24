/* The standalone Super Metroid world in C (port of tewtal's Randomizer.SuperMetroid). Hand-written; the region logic
 * and tables are generated into sm_world_gen.{h,c} by tools/port_world_sm.py. */
#ifndef SM_WORLD_H
#define SM_WORLD_H

#include <stdbool.h>
#include <stdint.h>

enum { SM_LOGIC_Casual, SM_LOGIC_Tournament };
enum { SM_PLACEMENT_Full, SM_PLACEMENT_Split };
enum { SM_LTYPE_Visible, SM_LTYPE_Chozo, SM_LTYPE_Hidden };
enum { SM_CLASS_Minor, SM_CLASS_Major };

#define SM_ITEM_SLOTS 32

/* What the C# passes around as List<Item>: only the count of each ItemType matters to the logic. */
typedef struct { uint8_t n[SM_ITEM_SLOTS]; } SmProg;

typedef struct {
    int id;
    const char *name;
    uint8_t type, cls, region;
    uint32_t addr;   /* PC offset of the item PLM */
} SmLocDef;

typedef struct { const char *name, *area; } SmRegionDef;

extern const SmLocDef SM_LOCS[];
extern const SmRegionDef SM_REGIONS[];

static inline bool sm_HasEnergyReserves(const SmProg *p, int amount) { return p->n[0] + p->n[20] >= amount; }   /* ETank + ReserveTank */

/* Location.Available: region entry plus the location's own requirement. idx is the SM_LOCS index. */
bool sm_loc_available(int idx, int logic, const SmProg *p);
/* SM_LOCS index of a location id, or -1. */
int sm_loc_index(int id);

#endif
