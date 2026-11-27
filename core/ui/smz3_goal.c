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
        {"smbosses:", GOAL_SMBOSSES, 10}, {"zdungeon:", GOAL_ZDUNGEON, 16}, {"smarea:", GOAL_SMAREA, 10},
        {"kills:", GOAL_KILLS, 10}, {"rupees:", GOAL_RUPEES, 10}};
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

static uint32_t counted(const SmzGoal *g, const SmzCounters *c)
{
    if (!c) return 0;
    return g->kind == GOAL_KILLS ? c->n[SMC_KILLS] : g->kind == GOAL_RUPEES ? c->n[SMC_RUPEES] : 0;
}

int smz_goal_counted(const SmzGoal *g, const SmzCounters *c)
{
    return (g->kind == GOAL_KILLS || g->kind == GOAL_RUPEES) && g->value > 0 && counted(g, c) >= (uint32_t)g->value;
}

int smz_goal_progress(const SmzGoal *g, const SmzCounters *c, int done)
{
    if (done) return 100;
    if ((g->kind != GOAL_KILLS && g->kind != GOAL_RUPEES) || g->value <= 0) return 0;
    uint32_t n = counted(g, c);
    return n >= (uint32_t)g->value ? 99 : (int)(n * 100 / (uint32_t)g->value);
}

/* Zelda: a boss's room, its dungeon ($040C) and Agahnim's; Super Metroid: each boss's area ($079F) and room ($079B). */
static const struct { int room, dungeon; } ZPLACE[] = {
    {0xC8, 0x04}, {0x33, 0x06}, {0x07, 0x14}, {0x5A, 0x0C}, {0x06, 0x0A}, {0x29, 0x10}, {0xAC, 0x16}, {0xDE, 0x12}, {0x90, 0x0E}, {0xA4, 0x18}};
static const struct { int area, room; } SMPLACE[4] = {{1, 0xA59F}, {3, 0xCD13}, {4, 0xDA60}, {2, 0xB32E}};

int smz_goal_progress_live(const SmzGoal *g, const SmzCounters *c, const uint8_t *wram, size_t wl, const uint8_t *sram, size_t sl,
                           int items, int done)
{
    if (done) return 100;
    if (g->kind == GOAL_KILLS || g->kind == GOAL_RUPEES) return smz_goal_progress(g, c, 0);
    smz3_game game = smz3_current_game(sram, sl);
    if (game == SMZ3_GAME_NONE || !smz3_started(wram, wl, sram, sl)) return 0;
    if (g->kind == GOAL_CRYSTALS || g->kind == GOAL_SMBOSSES) {
        int n = g->kind == GOAL_CRYSTALS ? popcount((unsigned)smz_crystal_mask(wram, wl, sram, sl, (int)game))
                                         : popcount((unsigned)smz_boss_mask(wram, wl, sram, sl, (int)game) & 15);
        return g->value > 0 ? (n >= g->value ? 99 : n * 100 / g->value) : 0;
    }
    int pc = items * 8 > 50 ? 50 : items * 8;
    int in_zelda = game == SMZ3_GAME_ZELDA && wl > 0x40C && wram[0x1B];   /* indoors */
    int zroom = in_zelda && wl > 0xA1 ? wram[0xA0] | wram[0xA1] << 8 : -1, zdun = in_zelda ? wram[0x40C] : -1;
    int smarea = game == SMZ3_GAME_METROID && wl > 0x79F ? wram[0x79F] : -1, smroom = smarea >= 0 ? wram[0x79B] | wram[0x79C] << 8 : -1;
    int here = 0, boss = 0;
    switch (g->kind) {
    case GOAL_ZBOSS:
        for (unsigned i = 0; i < sizeof ZPLACE / sizeof ZPLACE[0]; i++)
            if (ZPLACE[i].room == g->value) { here = zdun == ZPLACE[i].dungeon; boss = zroom == g->value; }
        break;
    case GOAL_AGA: here = zdun == 0x08; boss = zroom == 0x20; break;
    case GOAL_SMBOSS: if (g->value >= 0 && g->value < 4) { here = smarea == SMPLACE[g->value].area; boss = smroom == SMPLACE[g->value].room; } break;
    default: break;   /* a dungeon or an area is done on arrival; an item has no place to read */
    }
    if (boss) return 85;
    if (here) return 70 > pc ? 70 : pc;
    return pc;
}

void smz_goal_text(const SmzGoal *g, char *out, size_t cap)
{
    static const struct { int room; const char *name; } ZB[] = {
        {0xC8, "the Armos Knights"}, {0x33, "the Lanmolas"}, {0x07, "Moldorm"}, {0x5A, "Helmasaur King"}, {0x06, "Arrghus"},
        {0x29, "Mothula"}, {0xAC, "Blind"}, {0xDE, "Kholdstare"}, {0x90, "Vitreous"}, {0xA4, "Trinexx"}};
    static const char *const SB[4] = {"Kraid", "Phantoon", "Draygon", "Ridley"};
    static const char *const AREA[6] = {"Crateria", "Brinstar", "Norfair", "the Wrecked Ship", "Maridia", "Tourian"};
    const char *zb = "a boss";
    switch (g->kind) {
    case GOAL_KILLS: if (g->value == 1) snprintf(out, cap, "Defeat an enemy"); else snprintf(out, cap, "Defeat %d enemies", g->value); return;
    case GOAL_RUPEES: snprintf(out, cap, "Collect %d rupees", g->value); return;
    case GOAL_ZBOSS:
        for (unsigned i = 0; i < sizeof ZB / sizeof ZB[0]; i++) if (ZB[i].room == g->value) zb = ZB[i].name;
        snprintf(out, cap, "Defeat %s", zb);
        return;
    case GOAL_AGA: snprintf(out, cap, "Defeat Agahnim"); return;
    case GOAL_SMBOSS: snprintf(out, cap, "Defeat %s", g->value >= 0 && g->value < 4 ? SB[g->value] : "a boss"); return;
    case GOAL_CRYSTALS: if (g->value == 1) snprintf(out, cap, "Get a crystal"); else snprintf(out, cap, "Collect %d crystals", g->value); return;
    case GOAL_SMBOSSES: snprintf(out, cap, "Defeat %d Super Metroid bosses", g->value); return;
    case GOAL_SMAREA: snprintf(out, cap, "Reach %s", g->value >= 0 && g->value < 6 ? AREA[g->value] : "the area"); return;
    case GOAL_ZDUNGEON: snprintf(out, cap, "Reach the dungeon"); return;
    case GOAL_LOC: snprintf(out, cap, "Find the goal item"); return;
    default: snprintf(out, cap, "?"); return;
    }
}

int smz_goal_boss(const char *text, const char **name, const char **sprite, const char **place)
{
    static const struct { const char *name, *sprite, *place; } B[] = {
        {"Kraid", "boss_kraid", "Brinstar in Super Metroid"}, {"Phantoon", "boss_phantoon", "the Wrecked Ship in Super Metroid"},
        {"Draygon", "boss_draygon", "Maridia in Super Metroid"}, {"Ridley", "boss_ridley", "Norfair in Super Metroid"},
        {"Mother Brain", "boss_motherbrain", "Tourian in Super Metroid"}, {"Agahnim", "boss_agahnim", "Castle Tower in Zelda"},
        {"the Armos Knights", "boss_armos", "the Eastern Palace in Zelda"}, {"the Lanmolas", "", "the Desert Palace in Zelda"},
        {"Moldorm", "boss_moldorm", "the Tower of Hera in Zelda"}, {"Helmasaur King", "boss_helmasaur", "the Palace of Darkness in Zelda"},
        {"Arrghus", "boss_arrghus", "the Swamp Palace in Zelda"}, {"Mothula", "boss_mothula", "Skull Woods in Zelda"},
        {"Blind", "boss_blind", "Thieves' Town in Zelda"}, {"Kholdstare", "boss_kholdstare", "the Ice Palace in Zelda"},
        {"Vitreous", "boss_vitreous", "Misery Mire in Zelda"}, {"Trinexx", "boss_trinexx", "Turtle Rock in Zelda"}};
    if (!text || strncmp(text, "Defeat ", 7)) return 0;
    for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++)
        if (!strcmp(text + 7, B[i].name)) {
            if (name) *name = B[i].name;
            if (sprite) *sprite = B[i].sprite;
            if (place) *place = B[i].place;
            return 1;
        }
    return 0;
}
