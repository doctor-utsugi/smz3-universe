"""Hand-written C for the parts of the world logic the translator cannot handle (they depend on where items were placed)."""
BODIES = {
    "IcePalace_CanNotWasteKeys": """bool IcePalace_CanNotWasteKeys(const SmzWorld *w, const SmzProg *p, int a, int b)
{
    return !P_BigKeyIP(p) || w->item_at[a] == ITEM_BigKeyIP || w->item_at[b] == ITEM_BigKeyIP;
}
""",
    "GanonsTower_RandomizerRoomRoom__LeftSide": """bool GanonsTower_RandomizerRoomRoom__LeftSide(const SmzWorld *w, const SmzProg *p, int except)
{
    static const int room[] = { LOC_GanonsTowerRandomizerRoomTopLeft, LOC_GanonsTowerRandomizerRoomTopRight,
                                LOC_GanonsTowerRandomizerRoomBottomLeft, LOC_GanonsTowerRandomizerRoomBottomRight };
    int bigkey = 0;
    for (int i = 0; i < 4; i++) if (room[i] != except && w->item_at[room[i]] == ITEM_BigKeyGT) bigkey = 1;
    return P_Hammer(p) && P_Hookshot(p) && P_KeyGT(p) >= (bigkey ? 3 : 4);
}
""",
    "GanonsTower_CompassRoomRoom__RightSide": """bool GanonsTower_CompassRoomRoom__RightSide(const SmzWorld *w, const SmzProg *p, int except)
{
    static const int room[] = { LOC_GanonsTowerCompassRoomTopLeft, LOC_GanonsTowerCompassRoomTopRight,
                                LOC_GanonsTowerCompassRoomBottomLeft, LOC_GanonsTowerCompassRoomBottomRight };
    int bigkey = 0;
    for (int i = 0; i < 4; i++) if (room[i] != except && w->item_at[room[i]] == ITEM_BigKeyGT) bigkey = 1;
    return P_Somaria(p) && P_FireRod(p) && P_KeyGT(p) >= (bigkey ? 3 : 4);
}
""",
}
ACCESS = {
    "LOC_GanonsTowerMapChest": "P_Hammer(p) && (P_Hookshot(p) || P_Boots(p)) && P_KeyGT(p) >= ((w->item_at[LOC_GanonsTowerMapChest] == ITEM_BigKeyGT || w->item_at[LOC_GanonsTowerMapChest] == ITEM_KeyGT) ? 3 : 4)",
    "LOC_GanonsTowerFiresnakeRoom": """P_Hammer(p) && P_Hookshot(p) && P_KeyGT(p) >= ((w->item_at[LOC_GanonsTowerRandomizerRoomTopRight] == ITEM_BigKeyGT || w->item_at[LOC_GanonsTowerRandomizerRoomTopLeft] == ITEM_BigKeyGT || w->item_at[LOC_GanonsTowerRandomizerRoomBottomLeft] == ITEM_BigKeyGT || w->item_at[LOC_GanonsTowerRandomizerRoomBottomRight] == ITEM_BigKeyGT || w->item_at[LOC_GanonsTowerFiresnakeRoom] == ITEM_KeyGT) ? 2 : 3)""",
}

BODIES["can_enter_GanonsTower"] = """bool can_enter_GanonsTower(const SmzWorld *w, const SmzProg *p, bool req)
{
    int canEnterDDMEast = can_enter_DarkWorldDeathMountainEast(w, p, req);
    int haveEnoughCrystals = P_CrystalCount(p) >= w->gt_crystals;
    int gtOpenBeforeGanon = w->gt_crystals < w->ganon_crystals;
    int canBeatMetroid = can_defeat_boss_count(w, p, req, BOSSSET_SM) >= w->tourian_bosses;
    return CanNavigateDarkWorld(p, &w->cfg) && canEnterDDMEast && haveEnoughCrystals && (gtOpenBeforeGanon || canBeatMetroid);
}
"""

PROTOS = [
    "bool IcePalace_CanNotWasteKeys(const SmzWorld *w, const SmzProg *p, int a, int b);",
    "bool GanonsTower_RandomizerRoomRoom__LeftSide(const SmzWorld *w, const SmzProg *p, int except);",
    "bool GanonsTower_CompassRoomRoom__RightSide(const SmzWorld *w, const SmzProg *p, int except);",
    "int can_defeat_boss_count(const SmzWorld *w, const SmzProg *p, bool req, int bossset);",
    "bool can_aquire_all(const SmzWorld *w, const SmzProg *p, bool req, int reward);",
    "bool loc_available(const SmzWorld *w, int loc, const SmzProg *p);",
    "bool loc_access(const SmzWorld *w, int loc, const SmzProg *p, bool req);",
    "bool loc_allow(const SmzWorld *w, int loc, int item, const SmzProg *p);",
    "bool loc_always_allow(const SmzWorld *w, int loc, int item, const SmzProg *p);",
]
