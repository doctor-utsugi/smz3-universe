#ifndef SMZ3_STATE_H
#define SMZ3_STATE_H

#include <stddef.h>
#include <stdint.h>

/* Memory layout follows the SMZ3 Cas' tracker autotracker. wram is the 128 KB SNES work RAM
 * (snes9x 0x7E0000); sram is the cartridge save RAM starting at snes9x 0xA06000. */

typedef enum {
    SMZ3_GAME_NONE = 0,
    SMZ3_GAME_ZELDA,
    SMZ3_GAME_METROID,
    SMZ3_GAME_CREDITS
} smz3_game;

typedef struct {
    const char *id;
    const char *name;
    const char *region;
    smz3_game game;
    int zelda_misc;     /* the flag lives in the 0x7EF280 block instead of the 0x7EF000 room table */
    unsigned addr;
    unsigned flag;
} smz3_location;

extern const smz3_location smz3_locations[];
extern const unsigned smz3_location_count;

typedef struct {
    smz3_game game;
    int started;
    int finished;       /* see smz3_run_finished */
    unsigned zelda_total, zelda_cleared;
    unsigned metroid_total, metroid_cleared;
} smz3_summary;

smz3_game smz3_current_game(const uint8_t *sram, size_t sram_len);
int smz3_started(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len);
int smz3_run_finished(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len);

/* Index of the first ALttP chest location in the given room (WRAM 0xA0), or -1: lets the map place Link indoors. */
int smz3_zelda_room_location(unsigned room);

/* 1 cleared, 0 not cleared, -1 the needed memory is not available. */
int smz3_location_cleared(const smz3_location *loc, smz3_game current,
                          const uint8_t *wram, size_t wram_len,
                          const uint8_t *sram, size_t sram_len);

void smz3_summarize(const uint8_t *wram, size_t wram_len,
                    const uint8_t *sram, size_t sram_len, smz3_summary *out);

/* Inventory. Each entry resolves to a level: 0 not owned, >0 owned or a count/level, -1 unknown
 * (Super Metroid RAM is only readable while Super Metroid is the current game). */
typedef enum {
    SMZ3_INV_BYTE,          /* raw byte value */
    SMZ3_INV_ONE,           /* byte != 0 */
    SMZ3_INV_FLAG,          /* byte & flag */
    SMZ3_INV_BOTTLES,       /* number of the four bottle slots in use */
    SMZ3_INV_HEARTS,        /* max health byte / 8 */
    SMZ3_INV_WORD,          /* little-endian word */
    SMZ3_INV_ENERGY_TANKS   /* (max energy - 99) / 100 */
} smz3_inv_kind;

typedef struct {
    const char *name;
    const char *icon;       /* file name in SMZ3CasSprites Sprites/Items */
    smz3_game game;
    smz3_inv_kind kind;
    unsigned addr;          /* offset into the 0x7EF000 block (Zelda) or the 0x7E0000 bank (Metroid) */
    unsigned flag;
} smz3_inv_item;

extern const smz3_inv_item smz3_inventory_items[];
extern const unsigned smz3_inventory_count;

void smz3_inventory(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len,
                    int *levels);

#endif
