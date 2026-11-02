#include "smz3_state.h"

#define WRAM_ZELDA_MODULE 0x0010
#define WRAM_METROID_STATE 0x0998
#define SRAM_GAME 0x33FE
#define WRAM_ZELDA 0xF000        /* 0x7EF000 */
#define WRAM_ZELDA_MISC 0xF280   /* 0x7EF280 */
#define WRAM_METROID 0xD870      /* 0x7ED870 */
#define SRAM_ZELDA 0x0000        /* 0xA06000 */
#define SRAM_METROID 0x20C0      /* 0xA160C0 */

smz3_game smz3_current_game(const uint8_t *sram, size_t sram_len)
{
    if (sram_len <= SRAM_GAME) return SMZ3_GAME_NONE;
    switch (sram[SRAM_GAME]) {
    case 0x00: return SMZ3_GAME_ZELDA;
    case 0xFF: return SMZ3_GAME_METROID;
    case 0x11: return SMZ3_GAME_CREDITS;
    default: return SMZ3_GAME_NONE;
    }
}

int smz3_zelda_room_location(unsigned room)
{
    for (unsigned i = 0; i < smz3_location_count; i++) {
        const smz3_location *l = &smz3_locations[i];
        if (l->game == SMZ3_GAME_ZELDA && !l->zelda_misc && l->addr == room) return (int)i;
    }
    return -1;
}

/* The run is over at the moment control is lost, as the SMZ3 tracker counts it: Link entering the
 * Triforce room (ALttP module 0x19) or Samus entering her ship (pose word 0x0FB2 = 0xAA4F) once
 * both final bosses are down, or the credits as a fallback. The patch sets the done flags at
 * Mother Brain's death (0xA17402) and at the Triforce room (0xA17506). */
int smz3_run_finished(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len)
{
    smz3_game g = smz3_current_game(sram, sram_len);
    if (g == SMZ3_GAME_CREDITS) return 1;
    if (sram_len <= 0x3506 || !sram[0x3402]) return 0;
    if (g == SMZ3_GAME_ZELDA) return wram_len > 0x10 && wram[0x10] == 0x19;
    if (g == SMZ3_GAME_METROID) return sram[0x3506] && wram_len > 0xFB3 && (wram[0xFB2] | wram[0xFB3] << 8) == 0xAA4F;
    return 0;
}

/* Still the reset fill (Snes9x sets work RAM to 0x55): Super Metroid's item bits before the game data is loaded. */
static int metroid_items_unset(const uint8_t *wram, size_t wram_len)
{
    if (wram_len < WRAM_METROID + 0x40) return 1;
    for (int i = 0; i < 0x40; i++) if (wram[WRAM_METROID + i] != 0x55) return 0;
    return 1;
}

/* Past the title and file select: ALttP module 6+ (file loaded), Super Metroid game state 6+ (game data loading) once its
 * item bits are in. A value past the last module or state is the work RAM before the game set it: Snes9x fills it with
 * 0x55 at reset, and the SMZ3 boot code marks Super Metroid in the save RAM within the first frame. Read during the load,
 * that fill would count half of Super Metroid's checks for a few frames. */
int smz3_started(const uint8_t *wram, size_t wram_len, const uint8_t *sram, size_t sram_len)
{
    switch (smz3_current_game(sram, sram_len)) {
    case SMZ3_GAME_ZELDA: return wram_len > WRAM_ZELDA_MODULE && wram[WRAM_ZELDA_MODULE] >= 6 && wram[WRAM_ZELDA_MODULE] < 0x20;
    case SMZ3_GAME_METROID:
        return wram_len > WRAM_METROID_STATE + 1 && wram[WRAM_METROID_STATE] >= 6 && wram[WRAM_METROID_STATE] < 0x30 && !wram[WRAM_METROID_STATE + 1]
            && !metroid_items_unset(wram, wram_len);
    case SMZ3_GAME_CREDITS: return 1;
    default: return 0;
    }
}

int smz3_location_cleared(const smz3_location *loc, smz3_game current,
                          const uint8_t *wram, size_t wram_len,
                          const uint8_t *sram, size_t sram_len)
{
    const uint8_t *base;
    size_t len;

    if (loc->game == SMZ3_GAME_ZELDA) {
        size_t off = loc->zelda_misc ? 0x280 + loc->addr : (size_t)loc->addr * 2;
        size_t span = loc->zelda_misc ? 1 : 2;
        if (current == SMZ3_GAME_ZELDA) {
            base = wram + WRAM_ZELDA; len = wram_len > WRAM_ZELDA ? wram_len - WRAM_ZELDA : 0;
        } else {
            base = sram + SRAM_ZELDA; len = sram_len;
        }
        if (off + span > len) return -1;
        unsigned value = base[off];
        if (span == 2) value |= (unsigned)base[off + 1] << 8;
        /* Chest checks store the bit number (4..11) of the room flag word, misc checks store the mask. */
        return (value & (loc->zelda_misc ? loc->flag : 1u << loc->flag)) != 0;
    }

    if (current == SMZ3_GAME_METROID) {
        base = wram + WRAM_METROID; len = wram_len > WRAM_METROID ? wram_len - WRAM_METROID : 0;
    } else {
        base = sram + SRAM_METROID; len = sram_len > SRAM_METROID ? sram_len - SRAM_METROID : 0;
    }
    if (loc->addr >= len) return -1;
    return (base[loc->addr] & loc->flag) != 0;
}

void smz3_summarize(const uint8_t *wram, size_t wram_len,
                    const uint8_t *sram, size_t sram_len, smz3_summary *out)
{
    *out = (smz3_summary){0};
    out->game = smz3_current_game(sram, sram_len);
    out->started = smz3_started(wram, wram_len, sram, sram_len);
    out->finished = smz3_run_finished(wram, wram_len, sram, sram_len);
    /* the game save on its own (no WRAM): both games' checks are in the save RAM */
    int save_only = !wram_len && sram_len;
    smz3_game from = save_only ? SMZ3_GAME_NONE : out->game;
    if ((!out->started || out->game == SMZ3_GAME_NONE) && !save_only) {
        for (unsigned i = 0; i < smz3_location_count; i++) {
            if (smz3_locations[i].game == SMZ3_GAME_ZELDA) out->zelda_total++;
            else out->metroid_total++;
        }
        return;
    }

    for (unsigned i = 0; i < smz3_location_count; i++) {
        const smz3_location *loc = &smz3_locations[i];
        int cleared = smz3_location_cleared(loc, from, wram, wram_len, sram, sram_len);
        if (loc->game == SMZ3_GAME_ZELDA) {
            out->zelda_total++;
            out->zelda_cleared += cleared > 0;
        } else {
            out->metroid_total++;
            out->metroid_cleared += cleared > 0;
        }
    }
}
