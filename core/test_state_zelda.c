/* Zelda check decoding on a synthetic memory image: gcc -Icore core/test_state_zelda.c core/smz3_state.c core/smz3_locations.c core/smz3_inventory.c */
#include <stdio.h>
#include <string.h>
#include "smz3_state.h"

static int fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fail = 1; } } while (0)

static unsigned cleared(const uint8_t *wram, const uint8_t *sram, smz3_game g, const char *name)
{
    for (unsigned i = 0; i < smz3_location_count; i++) {
        const smz3_location *l = &smz3_locations[i];
        if (!strcmp(l->name, name) && l->game == g) return (unsigned)smz3_location_cleared(l, g, wram, 0x20000, sram, 0x8000);
    }
    return 99;
}

int main(void)
{
    static uint8_t wram[0x20000], sram[0x8000];
    wram[0x10] = 9;
    /* Link's House chest as seen on the Thor: room 0x104 word at 0x7EF208 went 0x0002 -> 0x0012 (chest bit 4). */
    wram[0xF208] = 0x02;
    CHECK(cleared(wram, sram, SMZ3_GAME_ZELDA, "Link's House") == 0);
    wram[0xF208] = 0x12;
    CHECK(cleared(wram, sram, SMZ3_GAME_ZELDA, "Link's House") == 1);
    smz3_summary s;
    smz3_summarize(wram, sizeof wram, sram, sizeof sram, &s);
    CHECK(s.game == SMZ3_GAME_ZELDA && s.started && s.zelda_cleared == 1);
    /* Same flag read from the SRAM mirror while Super Metroid is the current game. */
    memset(wram, 0, sizeof wram); wram[0x998] = 8; sram[0x33FE] = 0xFF; sram[0x208] = 0x12;
    smz3_summarize(wram, sizeof wram, sram, sizeof sram, &s);
    CHECK(s.game == SMZ3_GAME_METROID && s.zelda_cleared == 1);
    /* Indoor position: room 0x104 is Link's House. */
    int li = smz3_zelda_room_location(0x104);
    CHECK(li >= 0 && !strcmp(smz3_locations[li].name, "Link's House"));
    CHECK(smz3_zelda_room_location(0xFFFF) == -1);
    printf(fail ? "FAILED\n" : "ok\n");
    return fail;
}
