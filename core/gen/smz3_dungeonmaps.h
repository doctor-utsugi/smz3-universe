/* The game's own dungeon maps (the screen the pause menu shows), drawn from the user's A Link to the Past ROM at setup: port of
 * tools/zelda-dungeon-maps.py. The ROM is headerless; nothing of it is stored in the repository. */
#ifndef SMZ3_DUNGEONMAPS_H
#define SMZ3_DUNGEONMAPS_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_DM_DUNGEONS 14
#define SMZ_DM_FLOORS 8
#define SMZ_DM_CELL 80          /* one floor: 5 x 5 rooms of 16 pixels */
#define SMZ_DM_ROOM 16
#define SMZ_DM_ATLAS_W (SMZ_DM_FLOORS * SMZ_DM_CELL)
#define SMZ_DM_ATLAS_H (SMZ_DM_DUNGEONS * SMZ_DM_CELL)
#define SMZ_DM_ROOMS 300
#define SMZ_DM_CHESTS 6

typedef struct {
    uint8_t below[SMZ_DM_DUNGEONS], above[SMZ_DM_DUNGEONS];      /* floors under and over the ground floor */
    uint8_t grid[SMZ_DM_DUNGEONS][SMZ_DM_FLOORS][25];            /* room number per cell, 0xF for an empty cell */
    uint8_t chest_n[SMZ_DM_ROOMS];                               /* chests of a room (at most SMZ_DM_CHESTS kept) */
    uint8_t chest_xy[SMZ_DM_ROOMS][SMZ_DM_CHESTS][2];            /* their tile in the room's 64 x 64 tiles */
} SmzDungeonMeta;

/* full and hint are SMZ_DM_ATLAS_W * SMZ_DM_ATLAS_H RGBA pixels each: every room fully drawn, and the dimmed outline the map item reveals. */
int smz_dungeon_maps(const uint8_t *z3, size_t len, SmzDungeonMeta *meta, uint8_t *full, uint8_t *hint);

/* The game's graphics decompression, from the long address ea; returns the size written (at most cap). */
int smz_z3_decompress(const uint8_t *z3, size_t len, int ea, uint8_t *out, int cap);

#endif
