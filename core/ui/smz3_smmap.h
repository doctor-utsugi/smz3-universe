/* Super Metroid's pause map as Map Rando builds it (map_area.asm and friends), from the ROM and the running game's
 * explored, revealed and item bits, so every host draws the same picture. */
#ifndef SMZ3_SMMAP_H
#define SMZ3_SMMAP_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_SMMAP_W 512   /* an area: 64 x 32 tiles of 8 pixels */
#define SMZ_SMMAP_H 256
#define SMZ_SMMAP_AREAS 6

typedef struct {
    int area;                 /* the area picked by hand, -1 Samus's own */
    int shown;                /* the area in px, -1 none yet */
    int samus_area, samus_x, samus_y;   /* her area and map tile (-1 outside the game) */
    int x0, y0, x1, y1;       /* the drawn tiles' box in pixels (x1 <= x0: nothing drawn yet) */
    uint32_t key;             /* what the picture was built from */
    uint32_t px[SMZ_SMMAP_W * SMZ_SMMAP_H];   /* 0xAARRGGBB, alpha 0 where the grid shows through */
    uint32_t grid[64];        /* the pause screen's background tile */
} SmzSmMap;

/* 1 when rom has Map Rando's pause map layout. */
int smz_smmap_rom_ok(const uint8_t *rom, size_t rom_len);
void smz_smmap_init(SmzSmMap *m);
/* Rebuilds the picture when the game's map changed; 1 when it did. wram is the 128 KB work RAM, sram the cart RAM (32 KB). */
int smz_smmap_update(SmzSmMap *m, const uint8_t *rom, size_t rom_len, const uint8_t *wram, size_t wram_len,
                     const uint8_t *sram, size_t sram_len);
/* Picks the next (dir 1) or previous (-1) area the player has been in, as Select and X do on the pause screen; 0 goes back
 * to Samus's area. */
void smz_smmap_step(SmzSmMap *m, const uint8_t *sram, size_t sram_len, int dir);
const char *smz_smmap_area_name(int area);
/* The view that shows the whole area at the largest whole zoom that fits w x h (at least 1), centred on Samus when it does not. */
void smz_smmap_home(const SmzSmMap *m, int w, int h, float *cx, float *cy, float *zoom);
/* Draws the map into out (w x h, 0xAARRGGBB with alpha 255, stride in pixels), map pixel (cx, cy) at the centre, zoom screen
 * pixels per map pixel, Samus's tile in the pause screen's white corners. */
void smz_smmap_draw(const SmzSmMap *m, uint32_t *out, int w, int h, int stride, float cx, float cy, float zoom);

#endif
