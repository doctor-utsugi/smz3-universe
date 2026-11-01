#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_counters.h"

static const char *const KEYS[SMC_COUNT] = {
    "rupees", "moblin", "soldier", "octorok", "stalfos", "hinox", "lynel", "keese", "rope",
    "armos", "lanmolas", "moldorm", "agahnim", "helmasaur", "arrghus", "mothula", "blind", "kholdstare", "vitreous", "trinexx",
    "metroid", "pirate", "zoomer", "sidehopper",
    "kraid", "phantoon", "draygon", "ridley", "sporespawn", "crocomire", "botwoon", "goldentorizo", "bombtorizo",
    "swaps", "items", "runs",
};

const char *smz_counter_key(int i) { return i >= 0 && i < SMC_COUNT ? KEYS[i] : ""; }

/* ALttP sprite types ($0E20) per enemy counter */
static int zelda_enemy(unsigned type)
{
    if (type == 0x12) return SMC_MOBLIN;
    if (type >= 0x41 && type <= 0x4B) return SMC_SOLDIER;
    if (type == 0x08 || type == 0x0A) return SMC_OCTOROK;
    if (type == 0xA7 || type == 0x85) return SMC_STALFOS;
    if (type == 0x11) return SMC_HINOX;
    if (type == 0xD0) return SMC_LYNEL;
    if (type == 0x6F) return SMC_KEESE;
    if (type == 0x6E) return SMC_ROPE;
    return -1;
}

/* Super Metroid enemy ids ($0F78) per enemy counter */
static int metroid_enemy(unsigned id)
{
    if (id == 0xDD7F) return SMC_METROID;
    if (id >= 0xF353 && id <= 0xF593 && (id & 0x3F) == 0x13) return SMC_PIRATE;
    if (id == 0xDCFF) return SMC_ZOOMER;
    if (id == 0xD93F) return SMC_SIDEHOPPER;
    return -1;
}

/* ALttP boss rooms: the room's save data ($7EF000 + room * 2) has 0x0800 once its boss is beaten */
static const struct { int counter, room; } ZBOSS[] = {
    {SMC_ARMOS, 0xC8}, {SMC_LANMOLAS, 0x33}, {SMC_MOLDORM, 0x07}, {SMC_AGAHNIM, 0x20}, {SMC_HELMASAUR, 0x5A}, {SMC_ARRGHUS, 0x06},
    {SMC_MOTHULA, 0x29}, {SMC_BLIND, 0xAC}, {SMC_KHOLDSTARE, 0xDE}, {SMC_VITREOUS, 0x90}, {SMC_TRINEXX, 0xA4},
};
/* Super Metroid boss bits: area byte at $7ED828 (save RAM $2078) and its flag */
static const struct { int counter, area, bit; } MBOSS[] = {
    {SMC_KRAID, 1, 1}, {SMC_SPORE_SPAWN, 1, 2}, {SMC_RIDLEY, 2, 1}, {SMC_CROCOMIRE, 2, 2}, {SMC_GOLDEN_TORIZO, 2, 4},
    {SMC_PHANTOON, 3, 1}, {SMC_DRAYGON, 4, 1}, {SMC_BOTWOON, 4, 2}, {SMC_BOMB_TORIZO, 0, 4},
};

static unsigned at(const uint8_t *p, size_t len, size_t i) { return i < len ? p[i] : 0; }
static unsigned at16(const uint8_t *p, size_t len, size_t i) { return at(p, len, i) | at(p, len, i + 1) << 8; }

void smz_counters_reset(SmzCounters *c) { memset(c, 0, sizeof *c); }

static uint64_t bosses_beaten(int game, const uint8_t *wram, size_t wl, const uint8_t *sram, size_t sl)
{
    uint64_t m = 0;
    for (size_t i = 0; i < sizeof ZBOSS / sizeof ZBOSS[0]; i++) {
        size_t o = (size_t)ZBOSS[i].room * 2 + 1;   /* the live copy while ALttP runs, the parked one otherwise */
        unsigned v = game == 1 ? at(wram, wl, 0xF000 + o) : at(sram, sl, o);
        if (v & 0x08) m |= 1ull << ZBOSS[i].counter;
    }
    for (size_t i = 0; i < sizeof MBOSS / sizeof MBOSS[0]; i++) {
        unsigned v = game == 2 ? at(wram, wl, 0xD828 + MBOSS[i].area) : at(sram, sl, 0x2078 + MBOSS[i].area);
        if (v & MBOSS[i].bit) m |= 1ull << MBOSS[i].counter;
    }
    return m;
}

void smz_counters_tick(SmzCounters *c, int game, const uint8_t *wram, size_t wl, const uint8_t *sram, size_t sl, int items)
{
    if (game < 1 || game > 3) { c->have = 0; return; }
    uint64_t beaten = bosses_beaten(game, wram, wl, sram, sl);
    if (!c->have_base) { c->base = beaten; c->have_base = 1; }
    for (int i = SMC_FIRST_BOSS; i < SMC_COUNT; i++) if ((beaten >> i & 1) && !(c->base >> i & 1)) c->n[i] = 1;
    if (items > 0 && (uint32_t)items > c->n[SMC_ITEMS]) c->n[SMC_ITEMS] = (uint32_t)items;
    if (game == 3) c->n[SMC_RUNS] = 1;
    if (c->have && c->game != game && (c->game == 1 || c->game == 2) && (game == 1 || game == 2)) c->n[SMC_SWAPS]++;

    int zmode = game == 1 ? (int)at(wram, wl, 0x10) : 0;
    int zplay = zmode == 0x07 || zmode == 0x09;   /* dungeon or overworld, not a menu, a transition or a cutscene */
    int smstate = game == 2 ? (int)at16(wram, wl, 0x998) : 0, room = game == 2 ? (int)at16(wram, wl, 0x79B) : 0;
    unsigned rupees = at16(wram, wl, 0xF360);
    if (game == 1 && zplay) {
        int same = c->have && c->game == 1 && (c->zmode == 0x07 || c->zmode == 0x09);
        if (same && rupees > c->rupees && rupees - c->rupees <= 300) c->n[SMC_RUPEES] += rupees - c->rupees;
        for (int i = 0; i < 16; i++) {
            unsigned st = at(wram, wl, 0x0DD0 + i), ty = at(wram, wl, 0x0E20 + i);
            int e = zelda_enemy(ty);
            if (same && e >= 0 && st == 0x06 && (c->zstate[i] != 0x06 || c->ztype[i] != ty)) c->n[e]++;   /* just started dying */
            c->zstate[i] = (uint8_t)st; c->ztype[i] = (uint8_t)ty;
        }
    }
    if (game == 2 && smstate == 8) {
        int same = c->have && c->game == 2 && c->smstate == 8 && c->room == room;
        for (int i = 0; i < 32; i++) {
            unsigned id = at16(wram, wl, 0x0F78 + i * 0x40), hp = at16(wram, wl, 0x0F8C + i * 0x40);
            int e = metroid_enemy(c->sid[i]);
            if (same && e >= 0 && c->shp[i] > 0 && (id != c->sid[i] || hp == 0)) c->n[e]++;
            c->sid[i] = (uint16_t)id; c->shp[i] = (uint16_t)hp;
        }
    }
    c->game = game; c->zmode = zmode; c->smstate = smstate; c->room = room; c->rupees = rupees;
    c->have = 1;
}

int smz_counters_text(const SmzCounters *c, char *out, size_t cap)
{
    size_t n = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < SMC_COUNT; i++) {
        if (!c->n[i]) continue;
        int w = snprintf(out + n, cap > n ? cap - n : 0, "%s%s=%u", n ? "," : "", KEYS[i], (unsigned)c->n[i]);
        if (w < 0 || (size_t)w >= cap - n) return -1;
        n += (size_t)w;
    }
    if (c->have_base) {
        int w = snprintf(out + n, cap > n ? cap - n : 0, "%s_base=%llu", n ? "," : "", (unsigned long long)c->base);
        if (w < 0 || (size_t)w >= cap - n) return -1;
        n += (size_t)w;
    }
    return (int)n;
}

void smz_counters_load(SmzCounters *c, const char *text)
{
    smz_counters_reset(c);
    for (const char *p = text; p && *p;) {
        const char *eq = strchr(p, '='), *end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        if (eq && eq < end) {
            size_t kl = (size_t)(eq - p);
            if (kl == 5 && !strncmp(p, "_base", 5)) { c->base = strtoull(eq + 1, NULL, 10); c->have_base = 1; }
            else for (int i = 0; i < SMC_COUNT; i++)
                if (strlen(KEYS[i]) == kl && !strncmp(p, KEYS[i], kl)) { c->n[i] = (uint32_t)strtoul(eq + 1, NULL, 10); break; }
        }
        p = *end ? end + 1 : end;
    }
}
