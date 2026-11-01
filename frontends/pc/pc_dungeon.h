/* The game's own dungeon maps in the tracker's map tab: the floor picture with Link, the unopened checks and the boss. The pictures are
 * drawn from the user's ALttP ROM at setup (smz3_dungeonmaps.c) and kept in the data folder. */
#ifndef PC_DUNGEON_H
#define PC_DUNGEON_H

#include <SDL.h>
#include "smz3_tracker.h"

typedef struct { int d, idx, cell, lx, ly; } DungeonLive;    /* dungeon, floor index, room cell on the 5 x 5 grid (-1 unknown), spot in the room (0..15) */
typedef struct { int world; int picked, d, idx; } DungeonView;   /* world: showing the world map; picked: floor chosen by hand */

/* True when the pictures are installed (setup has read the ROM). */
int dungeon_available(SDL_Renderer *r);
/* Link's place from the Zelda work RAM, 0 when he is not inside a dungeon. */
int dungeon_live(const uint8_t *wram, size_t len, DungeonLive *out);
int dungeon_showing(const DungeonView *v, const DungeonLive *live);
void dungeon_view_init(DungeonView *v);
/* Navigation: dungeon_step moves between dungeons (+1/-1) and dungeon_floor between floors; both mark the floor as picked. */
void dungeon_step(DungeonView *v, const DungeonLive *live, int delta);
void dungeon_floor(DungeonView *v, const DungeonLive *live, int delta);
void dungeon_here(DungeonView *v);
/* Switches between the dungeon pictures and the world map (picks a dungeon when Link is in none). */
void dungeon_toggle(DungeonView *v, const DungeonLive *live);
/* The run marks the keys under pots and on enemies (ShowFixedKeys). */
void dungeon_set_fixed_keys(int on);
void dungeon_draw(SDL_Renderer *r, DungeonView *v, SDL_Rect area, const SmzTracker *t, const uint8_t *in_logic, const DungeonLive *live,
                  const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, int show_all, Uint32 ticks);

#endif
