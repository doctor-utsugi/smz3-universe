/* Hand-written part of the Super Metroid world: location lookups around the generated region logic. */
#include "sm_world_gen.h"

int sm_loc_index(int id)
{
    for (int i = 0; i < SM_LOC_COUNT; i++)
        if (SM_LOCS[i].id == id) return i;
    return -1;
}

bool sm_loc_available(int idx, int logic, const SmProg *p)
{
    return sm_region_can_enter(SM_LOCS[idx].region, logic, p) && sm_loc_access(SM_LOCS[idx].id, logic, p);
}
