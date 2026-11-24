/* The combined SMZ3 map as a pannable, zoomable view with check dots, boss marks and the player. */
#ifndef PC_MAP_H
#define PC_MAP_H

#include <SDL.h>
#include "smz3_discovery.h"
#include "smz3_tracker.h"

/* highlight: the small map rings the checks that can be taken now; gx, gy, gz: the game's own map (Map Rando) as last drawn,
 * gz_set the zoom picked by hand (0 none). */
typedef struct { float cx, cy, zoom; int follow; int highlight; float gx, gy, gz, gz_set; } MapView;

void map_view_init(MapView *v);
/* dx, dy in map pixels per call; dz multiplies the zoom. Any pan turns follow off. */
void map_view_move(MapView *v, float dx, float dy, float dz);
/* The whole map in the area, centred. */
void map_view_fit(MapView *v, SDL_Rect area);
/* Pending checks drawn from now on: NULL shows all of them, else those with visible[i] set (the run's helps; dual-screen mode). */
void map_set_visible(const uint8_t *visible);
const uint8_t *map_visible(void);
void map_draw(SDL_Renderer *r, MapView *v, SDL_Rect area, const SmzTracker *t, const SmzDiscovery *d, int discovered_only,
              const uint8_t *in_logic, const uint8_t *wram, size_t wram_len, Uint32 ticks);

/* The run's ROM, kept when it has the game's own pause map (Map Rando), which then replaces the tracker's map. */
void map_game_rom(const uint8_t *rom, size_t len);
int map_game_on(void);
/* Shows the next (1) or previous (-1) area the player has been in, 0 Samus's again. */
void map_game_step(int dir);
const char *map_game_area(void);

/* An ALttP seed hash ("Bow Hookshot ...") as the five icons of its file select screen, size pixels each from (x, y); returns
 * the width drawn, 0 when hash is not one (then nothing is drawn). */
float map_hash_icons(SDL_Renderer *r, float x, float y, float size, const char *hash);

/* A filled disc (ring = 0) or a ring (1), for the dungeon pictures as well. */
void map_legend(SDL_Renderer *r, float x, float y, float h, int dungeon, int logic);
void map_circle(SDL_Renderer *r, float x, float y, float rad, SDL_Color c, int ring);

/* The inventory icon (file name as in smz3_inventory_items) as a texture, NULL when there is none. */
SDL_Texture *map_item_icon(SDL_Renderer *r, const char *icon);
/* The drawn part of an embedded icon (the boss tokens sit in a corner of their canvas); icon must stay valid. */
SDL_Rect map_icon_art(const char *icon);

#endif
