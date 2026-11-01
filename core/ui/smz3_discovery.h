/* What the player has seen in a run and where they are on the combined map, from the game RAM. Super Metroid is tracked by room,
 * ALttP by overworld screen and dungeon. Positions and rectangles are in pixels of the half-size combined map (1623 x 958). */
#ifndef SMZ3_DISCOVERY_H
#define SMZ3_DISCOVERY_H

#include <stddef.h>
#include <stdint.h>

typedef struct { int ptr, area, mx, my, w, h; } SmzSmRoom;      /* a Super Metroid room by its pointer: area, map tile and size */
typedef struct { const char *name; float box[4]; } SmzZRegion;  /* bounding box of a Zelda region's markers */
typedef struct { int index, x, y; } SmzMarker;                  /* a location (index into smz3_locations) on the half-size map */
typedef struct {
    const SmzSmRoom *rooms; int n_rooms;
    const SmzZRegion *regions; int n_regions;
    const SmzMarker *markers; int n_markers;
} SmzMapData;

typedef struct { float x, y, w, h; } SmzRect;

#define SMZ_DISC_MAX_ROOMS 512

typedef struct {
    const SmzMapData *data;
    int rooms[SMZ_DISC_MAX_ROOMS], n_rooms;
    uint8_t screens[128];          /* bit per overworld screen: 0..63 light world, 64..127 dark world */
    uint32_t dungeons;             /* bit per dungeon name in SMZ_DISC_DUNGEONS */
    int has_position; float x, y;
    int has_overworld; float ow_x, ow_y;
    int area;                      /* current Super Metroid area, -1 unknown */
    int dirty;
    int filter_room, filter_count; /* the room filter: same room in two samples while in normal gameplay */
    float pedestal_x, pedestal_y, zora_x, zora_y; int has_pedestal, has_zora;
} SmzDiscovery;

void smz_disc_init(SmzDiscovery *d, const SmzMapData *data);
/* Text form: "rooms 1 2 3" / "screens 0 1" / "dungeons Name;Name" lines. */
void smz_disc_load(SmzDiscovery *d, const char *text);
int smz_disc_save(const SmzDiscovery *d, char *out, size_t cap);
/* game 1 ALttP, 2 Super Metroid. */
void smz_disc_update(SmzDiscovery *d, const uint8_t *wram, size_t wram_len, int game);
int smz_disc_count(const SmzDiscovery *d);
/* The rectangles of the map that count as discovered. Returns how many were written. */
int smz_disc_rects(const SmzDiscovery *d, SmzRect *out, int cap);
/* The whole Super Metroid area Samus is in, or both ALttP worlds; 0 when unknown. */
int smz_disc_area_box(const SmzDiscovery *d, int game, SmzRect *out);
const float *smz_disc_zelda_box(const SmzDiscovery *d, const char *region);
int smz_disc_metroid_room_centre(const SmzDiscovery *d, int ptr, float *x, float *y);
/* Boss markers: the dungeon bosses (letter '?') and the four Super Metroid bosses (K, P, D, R), at their map positions. */
typedef struct { float x, y; char letter; int done; } SmzBossMark;
int smz_disc_boss_marks(const SmzDiscovery *d, const uint8_t *wram, size_t wram_len, SmzBossMark *out, int cap);
/* Marker of a location, 0 when it has none. */
const SmzMarker *smz_disc_marker(const SmzDiscovery *d, int index);

#endif
