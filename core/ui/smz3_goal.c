#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_goal.h"
#include "smz3_state.h"
#include "smz3_tracker.h"

int smz_goal_parse(const char *text, SmzGoal *g)
{
    static const struct { const char *name; SmzGoalKind kind; int base; } K[] = {
        {"loc:", GOAL_LOC, 10}, {"zboss:", GOAL_ZBOSS, 16}, {"smboss:", GOAL_SMBOSS, 10}, {"crystals:", GOAL_CRYSTALS, 10},
        {"smbosses:", GOAL_SMBOSSES, 10}, {"zdungeon:", GOAL_ZDUNGEON, 16}, {"smarea:", GOAL_SMAREA, 10}};
    g->kind = GOAL_NONE; g->value = 0;
    if (!text) return -1;
    if (!strcmp(text, "aga")) { g->kind = GOAL_AGA; return 0; }
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        size_t n = strlen(K[i].name);
        if (strncmp(text, K[i].name, n) || !text[n]) continue;
        char *end;
        long v = strtol(text + n, &end, K[i].base);
        if (*end || v < 0 || v > 0xFFFF) return -1;
        if (K[i].kind == GOAL_LOC && v >= (long)smz3_location_count) return -1;
        g->kind = K[i].kind; g->value = (int)v;
        return 0;
    }
    return -1;
}

int smz_goal_from_settings(const char *json, SmzGoal *g)
{
    g->kind = GOAL_NONE; g->value = 0;
    const char *p = json ? strstr(json, "\"goal\":\"") : NULL;
    if (!p) return -1;
    p += 8;
    const char *q = strchr(p, '"');
    if (!q || q - p > 31) return -1;
    char text[32];
    snprintf(text, sizeof text, "%.*s", (int)(q - p), p);
    return smz_goal_parse(text, g);
}

static int popcount(unsigned v) { int n = 0; for (; v; v &= v - 1) n++; return n; }

int smz_goal_done(const SmzGoal *g, const uint8_t *wram, size_t wl, const uint8_t *sram, size_t sl)
{
    smz3_game game = smz3_current_game(sram, sl);
    if (game == SMZ3_GAME_NONE || !smz3_started(wram, wl, sram, sl)) return 0;
    int in_zelda = game == SMZ3_GAME_ZELDA;
    /* ALttP's save block is live at $7EF000 while it runs and kept at the start of the save RAM otherwise */
    const uint8_t *z = in_zelda ? (wl > 0xF000 ? wram + 0xF000 : NULL) : sram;
    size_t zl = in_zelda ? (wl > 0xF000 ? wl - 0xF000 : 0) : sl;
    switch (g->kind) {
    case GOAL_LOC: return smz3_location_cleared(&smz3_locations[g->value], game, wram, wl, sram, sl) > 0;
    case GOAL_ZBOSS: { size_t a = (size_t)g->value * 2 + 1; return z && a < zl && (z[a] & 0x08); }   /* bit 11 of the room word */
    case GOAL_AGA: return z && zl > 0x3C5 && z[0x3C5] >= 3;
    case GOAL_SMBOSS: return (smz_boss_mask(wram, wl, sram, sl, (int)game) >> g->value) & 1;
    case GOAL_CRYSTALS: return popcount((unsigned)smz_crystal_mask(wram, wl, sram, sl, (int)game)) >= g->value;
    case GOAL_SMBOSSES: return popcount((unsigned)smz_boss_mask(wram, wl, sram, sl, (int)game) & 15) >= g->value;
    case GOAL_ZDUNGEON: return in_zelda && wl > 0x40C && wram[0x10] == 7 && wram[0x1B] && wram[0x40C] == g->value;   /* module 7, indoors */
    case GOAL_SMAREA: return game == SMZ3_GAME_METROID && wl > 0x79F && wram[0x79F] == g->value && wram[0x998] == 8;   /* in play, not a transition */
    default: return 0;
    }
}
