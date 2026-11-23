#ifndef SMZ3_SHORTCUTS_H
#define SMZ3_SHORTCUTS_H
/* Item shortcuts: equip an ALttP item on Y, turn a Super Metroid beam on or off or pick its HUD weapon without the game's menu. The hosts read
 * and write the live memory with these and the ROM's hooks (smz_rom_shortcut_hooks) redraw the HUD or the beam. */
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SC_BOW, SC_BLUE_BOOMERANG, SC_RED_BOOMERANG, SC_HOOKSHOT, SC_BOMBS, SC_MUSHROOM, SC_POWDER, SC_FIRE_ROD, SC_ICE_ROD,
    SC_BOMBOS, SC_ETHER, SC_QUAKE, SC_LAMP, SC_HAMMER, SC_SHOVEL, SC_FLUTE, SC_NET, SC_BOOK, SC_BOTTLE1, SC_BOTTLE2,
    SC_BOTTLE3, SC_BOTTLE4, SC_SOMARIA, SC_BYRNA, SC_CAPE, SC_MIRROR,
    SC_WAVE, SC_ICE, SC_SPAZER, SC_PLASMA,
    SC_MISSILES, SC_SUPERS, SC_POWER_BOMBS,
    SC_COUNT
} SmzShortcutId;

#define SC_ZELDA_COUNT SC_WAVE      /* ids below are ALttP items, the rest Super Metroid's */
#define SC_WEAPON_FIRST SC_MISSILES  /* from here on, the HUD's weapons; before it, the beams */

typedef struct {
    const char *name, *icon;   /* icon: the tracker's file name under items/, "" when there is none (show the name) */
} SmzShortcut;

extern const SmzShortcut smz_shortcuts[SC_COUNT];

/* A write the host makes to the work RAM (offset from $7E0000). */
typedef struct { uint32_t offset; uint8_t value; } SmzPoke;

/* 1 when the game is in normal play, where a shortcut may act: ALttP on foot in a dungeon or outside, Super Metroid in
 * play. A shortcut of the other game never acts. */
int smz_shortcut_ready(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len);
/* 1 when the player has it in the game being played now. */
int smz_shortcut_owned(int id, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len);
/* 1 when it is the item on Y now, the beam is on or the weapon is the HUD's pick. */
int smz_shortcut_active(int id, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len);
/* The writes that equip the item, toggle the beam or pick the weapon (again: back to the beam), or 0 when it cannot act now (not owned, not in play). */
int smz_shortcut_pokes(int id, const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len, SmzPoke *out, int cap);

#endif
