/* The combined SMZ3 map as a pannable, zoomable view with check dots, boss marks and the player. */
#ifndef PC_MAP_H
#define PC_MAP_H

#include <SDL.h>
#include "smz3_discovery.h"
#include "smz3_tracker.h"

typedef struct { float cx, cy, zoom; int follow; int highlight; } MapView;   /* highlight: the small map rings the checks that can be taken now */

void map_view_init(MapView *v);
/* dx, dy in map pixels per call; dz multiplies the zoom. Any pan turns follow off. */
void map_view_move(MapView *v, float dx, float dy, float dz);
void map_draw(SDL_Renderer *r, MapView *v, SDL_Rect area, const SmzTracker *t, const SmzDiscovery *d, int discovered_only,
              const uint8_t *in_logic, const uint8_t *wram, size_t wram_len, Uint32 ticks);

/* A filled disc (ring = 0) or a ring (1), for the dungeon pictures as well. */
void map_legend(SDL_Renderer *r, float x, float y, float h, int dungeon, int logic);
void map_circle(SDL_Renderer *r, float x, float y, float rad, SDL_Color c, int ring);

/* The inventory icon (file name as in smz3_inventory_items) as a texture, NULL when there is none. */
SDL_Texture *map_item_icon(SDL_Renderer *r, const char *icon);

#endif
