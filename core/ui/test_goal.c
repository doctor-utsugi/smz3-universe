#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "smz3_goal.h"
#include "smz3_tracker.h"

static uint8_t wram[0x20000], sram[0x8000];

static void zelda(void) { memset(wram, 0, sizeof wram); memset(sram, 0, sizeof sram); sram[0x33FE] = 0x00; wram[0x10] = 9; }   /* ALttP, in game */
static void metroid(void) { memset(wram, 0, sizeof wram); memset(sram, 0, sizeof sram); sram[0x33FE] = 0xFF; wram[0x998] = 8; }
static int done(const char *text)
{
    SmzGoal g;
    assert(smz_goal_parse(text, &g) == 0);
    return smz_goal_done(&g, wram, sizeof wram, sram, sizeof sram);
}
static int find_loc(smz3_game game)
{
    for (unsigned i = 0; i < smz3_location_count; i++) if (smz3_locations[i].game == game && !smz3_locations[i].zelda_misc) return (int)i;
    return -1;
}

int main(void)
{
    char t[32];
    SmzGoal g;

    /* parsing */
    assert(smz_goal_parse("aga", &g) == 0 && g.kind == GOAL_AGA);
    assert(smz_goal_parse("zboss:c8", &g) == 0 && g.kind == GOAL_ZBOSS && g.value == 0xC8);
    assert(smz_goal_parse("zdungeon:1a", &g) == 0 && g.value == 0x1A);
    assert(smz_goal_parse("crystals:3", &g) == 0 && g.value == 3);
    assert(smz_goal_parse("loc:", &g) < 0 && g.kind == GOAL_NONE);
    assert(smz_goal_parse("loc:99999", &g) < 0);
    assert(smz_goal_parse("zboss:zz", &g) < 0);
    assert(smz_goal_parse("smboss:1x", &g) < 0);
    assert(smz_goal_parse("dance", &g) < 0 && smz_goal_parse(NULL, &g) < 0);
    assert(smz_goal_from_settings("{\"seed\":\"1\",\"challenge\":true,\"goal\":\"smarea:3\",\"logic\":{}}", &g) == 0 && g.kind == GOAL_SMAREA && g.value == 3);
    assert(smz_goal_from_settings("{\"seed\":\"1\"}", &g) < 0 && g.kind == GOAL_NONE);

    /* an ALttP location: live in ALttP, from the save RAM in Super Metroid */
    int zl = find_loc(SMZ3_GAME_ZELDA);
    const smz3_location *l = &smz3_locations[zl];
    unsigned zm = 1u << l->flag;
    snprintf(t, sizeof t, "loc:%d", zl);
    zelda();
    assert(!done(t));
    wram[0xF000 + l->addr * 2] |= zm & 0xFF; wram[0xF000 + l->addr * 2 + 1] |= zm >> 8;
    assert(done(t));
    metroid();
    sram[l->addr * 2] |= zm & 0xFF; sram[l->addr * 2 + 1] |= zm >> 8;
    assert(done(t));

    /* a Super Metroid location: live in Super Metroid, from the save RAM in ALttP */
    int ml = find_loc(SMZ3_GAME_METROID);
    l = &smz3_locations[ml];
    snprintf(t, sizeof t, "loc:%d", ml);
    metroid();
    assert(!done(t));
    wram[0xD870 + l->addr] |= l->flag;
    assert(done(t));
    zelda();
    sram[0x20C0 + l->addr] |= l->flag;
    assert(done(t));

    /* the Armos Knights (room 0xC8, bit 11) and Agahnim */
    zelda();
    wram[0xF000 + 0xC8 * 2] = 0xFF;          /* other room bits do not count */
    assert(!done("zboss:c8"));
    wram[0xF000 + 0xC8 * 2 + 1] = 0x08;
    assert(done("zboss:c8") && !done("zboss:33"));
    metroid();
    sram[0xC8 * 2 + 1] = 0x08;
    assert(done("zboss:c8"));
    zelda();
    wram[0xF3C5] = 2;
    assert(!done("aga"));
    wram[0xF3C5] = 3;
    assert(done("aga"));

    /* Phantoon (area 3) and two of the four bosses, in both games */
    metroid();
    wram[0xD828 + 3] = 1;
    assert(done("smboss:1") && !done("smboss:0") && !done("smbosses:2"));
    wram[0xD828 + 1] = 1;                     /* Kraid */
    assert(done("smbosses:2") && !done("smbosses:3"));
    zelda();
    sram[0x2078 + 4] = 1;                     /* Draygon, read from the save RAM */
    assert(done("smboss:2"));

    /* crystals */
    zelda();
    wram[0xF37A] = 0x05;
    assert(done("crystals:2") && !done("crystals:3"));
    metroid();
    sram[0x37A] = 0x7F;
    assert(done("crystals:7"));

    /* being inside a dungeon or an area */
    zelda();
    wram[0x10] = 7; wram[0x40C] = 0x1A;
    assert(!done("zdungeon:1a"));             /* outdoors */
    wram[0x1B] = 1;
    assert(done("zdungeon:1a") && !done("zdungeon:18"));
    metroid();
    wram[0x79F] = 3;
    assert(done("smarea:3") && !done("smarea:4"));
    wram[0x998] = 0x0B;                       /* a door transition */
    assert(!done("smarea:3"));

    /* nothing counts before a file has started */
    zelda();
    wram[0x10] = 2; wram[0xF3C5] = 3;
    assert(!done("aga"));

    /* the tracker ends the run at the goal and counts no time after it */
    SmzTracker tr;
    smz_tracker_reset(&tr, 0, 0, 0, 0);
    assert(smz_goal_from_settings("{\"challenge\":true,\"goal\":\"smboss:3\"}", &tr.goal) == 0);
    metroid();
    wram[0x5B6] = 1; smz_tracker_tick(&tr, wram, sizeof wram, sram, sizeof sram, 100, 1);
    wram[0x5B6] = 2; smz_tracker_tick(&tr, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(tr.game == SMZ3_GAME_METROID && tr.pending_ms == 100);
    wram[0xD828 + 2] = 1;                     /* Ridley */
    wram[0x5B6] = 3; smz_tracker_tick(&tr, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(tr.game == SMZ3_GAME_CREDITS && tr.pending_ms == 200);
    wram[0xD828 + 2] = 0;                     /* the goal stays met */
    wram[0x5B6] = 4; smz_tracker_tick(&tr, wram, sizeof wram, sram, sizeof sram, 100, 1);
    assert(tr.game == SMZ3_GAME_CREDITS && tr.pending_ms == 200);

    printf("goal ok\n");
    return 0;
}
