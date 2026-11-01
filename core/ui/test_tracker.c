#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "smz3_tracker.h"

static uint8_t wram[0x20000], sram[0x100000];

static void frame(void) { wram[0x1A]++; }

int main(void)
{
    SmzTracker t;
    smz_tracker_reset(&t, 5000, 0, 0, 0);
    sram[0x33FE] = 0x00;                    /* ALttP is the current game */
    wram[0x10] = 7;                         /* in game */

    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    assert(t.started && t.game == SMZ3_GAME_ZELDA);
    assert(t.pending_ms == 0);              /* the first read has no previous frame */
    assert(t.n_recent_checks == 0 && t.n_recent_items == 0);

    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    assert(t.pending_ms == 500 && t.elapsed_ms == 5500);

    smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);   /* frame did not move: paused */
    assert(t.pending_ms == 500);

    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 4);
    assert(t.pending_ms == 500 + 2000);     /* fast forward counts by the factor */

    /* a chest is opened and the boots are picked up */
    const smz3_location *l = &smz3_locations[smz3_zelda_room_location(0xE0)];   /* a chest room */
    unsigned off = 0xF000 + (l->zelda_misc ? 0x280 + l->addr : l->addr * 2);
    { unsigned m = l->zelda_misc ? l->flag : 1u << l->flag; wram[off] |= m & 0xFF; if (!l->zelda_misc) wram[off + 1] |= m >> 8; }
    wram[0xF355] = 1;
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    assert(t.n_recent_checks == 1 && t.cleared[l - smz3_locations]);
    assert(t.n_recent_items == 1);
    int boots = -1;
    for (unsigned i = 0; i < smz3_inventory_count; i++) if (!strcmp(smz3_inventory_items[i].name, "Boots")) boots = (int)i;
    assert(t.recent_items[0] == boots);

    /* the game saves: SRAM changes while playing */
    assert(!t.save_changed);
    sram[0x100] ^= 0xFF;
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    assert(t.save_changed && t.save_ms == 5000 + t.pending_ms - 500);
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    assert(!t.save_changed);

    /* crystals and keysanity items */
    wram[0xF37A] = 0x03;                    /* bits 0 and 1: crystals 6 and 1 */
    assert(smz_crystal_mask(wram, sizeof wram, sram, sizeof sram, 1) == ((1 << 5) | 1));
    wram[0xF366] = 0x04;                    /* big key of Ganon's Tower */
    wram[0xF4E0 + 3] = 2;                   /* two Desert Palace small keys */
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    int items[SMZ_TRK_MAX_ITEMS];
    int n0 = smz_tracker_logic_items(&t, wram, sizeof wram, sram, sizeof sram, 0, items, SMZ_TRK_MAX_ITEMS);
    int n1 = smz_tracker_logic_items(&t, wram, sizeof wram, sram, sizeof sram, 1, items, SMZ_TRK_MAX_ITEMS);
    assert(n1 == n0 + 3);
    assert(n0 >= 1);

    /* a transition frame that reads as not started keeps the last live state for two seconds */
    wram[0x10] = 0;
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1);
    assert(t.started);
    for (int i = 0; i < 4; i++) { frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 500, 1); }
    assert(!t.started && t.elapsed_ms == 0);

    smz_tracker_commit(&t);
    assert(t.pending_ms == 0 && t.base_ms > 5000);

    /* the end of the run: Link reaches the Triforce room once Super Metroid is done */
    smz_tracker_reset(&t, 0, 0, 0, 0);
    wram[0x10] = 0x19;                      /* Triforce room module, Super Metroid not done yet */
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(t.game == SMZ3_GAME_ZELDA);
    sram[0x3402] = 1;                       /* Mother Brain is dead */
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(t.game == SMZ3_GAME_CREDITS && t.pending_ms == 200);   /* the read that sees the end still counts */
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(t.pending_ms == 200);            /* the ending scene is not counted */

    /* or Samus enters her ship once both bosses are down */
    smz_tracker_reset(&t, 0, 0, 0, 0);
    memset(wram, 0, sizeof wram); memset(sram, 0, sizeof sram);
    sram[0x33FE] = 0xFF; wram[0x998] = 8;   /* Super Metroid, in game */
    sram[0x3402] = 1;
    wram[0xFB2] = 0x4F; wram[0xFB3] = 0xAA;
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(t.game == SMZ3_GAME_METROID);    /* Ganon is still alive */
    sram[0x3506] = 1;
    wram[0xFB2] = 0;
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(t.game == SMZ3_GAME_METROID);    /* both done, still running to the ship */
    wram[0xFB2] = 0x4F;
    frame(); smz_tracker_tick(&t, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(t.game == SMZ3_GAME_CREDITS);
    sram[0x33FE] = 0x11;                    /* the credits still read as the end */
    assert(smz3_run_finished(wram, sizeof wram, sram, sizeof sram));
    printf("tracker ok\n");
    return 0;
}
