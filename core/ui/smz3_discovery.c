#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_discovery.h"
#include "smz3_state.h"

#define EAST_CRATERIA 46
#define NORMAL_GAMEPLAY 8

/* where each Super Metroid area's map tile grid starts on the full-size map: Crateria, Brinstar, Norfair, Wrecked Ship, Maridia, Tourian */
static const int SM_ORIGINS[6][2] = {{998, 80}, {902, 656}, {1894, 1296}, {2086, -240}, {1798, 656}, {900, 84}};
#define SM_AREAS 6

static const char *const DUNGEON_NAMES[] = {
    "Hyrule Castle", "Eastern Palace", "Desert Palace", "Castle Tower", "Swamp Palace", "Palace of Darkness", "Misery Mire",
    "Skull Woods", "Ice Palace", "Tower of Hera", "Thieves' Town", "Turtle Rock", "Ganon's Tower",
};
#define N_DUNGEONS (int)(sizeof DUNGEON_NAMES / sizeof DUNGEON_NAMES[0])

/* $040C -> dungeon name index, -1 for none */
static int dungeon_of(int id)
{
    switch (id) {
    case 0x00: case 0x02: return 0; case 0x04: return 1; case 0x06: return 2; case 0x08: return 3; case 0x0A: return 4;
    case 0x0C: return 5; case 0x0E: return 6; case 0x10: return 7; case 0x12: return 8; case 0x14: return 9;
    case 0x16: return 10; case 0x18: return 11; case 0x1A: return 12; default: return -1;
    }
}

static const SmzSmRoom *find_room(const SmzDiscovery *d, int ptr)
{
    for (int i = 0; i < d->data->n_rooms; i++) if (d->data->rooms[i].ptr == ptr) return &d->data->rooms[i];
    return NULL;
}

static int room_col(const SmzSmRoom *r) { return r->mx + (r->area == 0 && r->mx >= EAST_CRATERIA ? 7 : 0); }

const float *smz_disc_zelda_box(const SmzDiscovery *d, const char *region)
{
    for (int i = 0; i < d->data->n_regions; i++) if (!strcmp(d->data->regions[i].name, region)) return d->data->regions[i].box;
    return NULL;
}

const SmzMarker *smz_disc_marker(const SmzDiscovery *d, int index)
{
    for (int i = 0; i < d->data->n_markers; i++) if (d->data->markers[i].index == index) return &d->data->markers[i];
    return NULL;
}

void smz_disc_init(SmzDiscovery *d, const SmzMapData *data)
{
    memset(d, 0, sizeof *d);
    d->data = data;
    d->area = -1;
    d->filter_room = -1;
    /* the special overworld areas (Master Sword grove, Zora's domain) sit on the marker of their item */
    for (unsigned i = 0; i < smz3_location_count; i++) {
        const char *name = smz3_locations[i].name;
        const SmzMarker *m = smz_disc_marker(d, (int)i);
        if (!m) continue;
        if (strstr(name, "Pedestal")) { d->pedestal_x = (float)m->x; d->pedestal_y = (float)m->y; d->has_pedestal = 1; }
        else if (strstr(name, "Zora") && strstr(name, "Ledge")) { d->zora_x = (float)m->x; d->zora_y = (float)m->y; d->has_zora = 1; }
    }
}

static void add_room(SmzDiscovery *d, int ptr)
{
    for (int i = 0; i < d->n_rooms; i++) if (d->rooms[i] == ptr) return;
    if (d->n_rooms < SMZ_DISC_MAX_ROOMS) { d->rooms[d->n_rooms++] = ptr; d->dirty = 1; }
}

static void add_screen(SmzDiscovery *d, int s)
{
    if (s < 0 || s > 127) return;
    if (!(d->screens[s >> 3] & (1 << (s & 7)))) { d->screens[s >> 3] |= (uint8_t)(1 << (s & 7)); d->dirty = 1; }
}

static void add_dungeon(SmzDiscovery *d, int i)
{
    if (!(d->dungeons & (1u << i))) { d->dungeons |= 1u << i; d->dirty = 1; }
}

void smz_disc_load(SmzDiscovery *d, const char *text)
{
    d->n_rooms = 0; memset(d->screens, 0, sizeof d->screens); d->dungeons = 0;
    char *copy = strdup(text ? text : "");
    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        if (!strncmp(line, "rooms ", 6)) { for (char *p = line + 6, *e; (void)(e = NULL), *p; ) { long v = strtol(p, &e, 10); if (e == p) break; if (d->n_rooms < SMZ_DISC_MAX_ROOMS) d->rooms[d->n_rooms++] = (int)v; p = e; } }
        else if (!strncmp(line, "screens ", 8)) { for (char *p = line + 8, *e; *p; ) { long v = strtol(p, &e, 10); if (e == p) break; if (v >= 0 && v < 128) d->screens[v >> 3] |= (uint8_t)(1 << (v & 7)); p = e; } }
        else if (!strncmp(line, "dungeons ", 9)) {
            for (int i = 0; i < N_DUNGEONS; i++) if (strstr(line + 9, DUNGEON_NAMES[i])) d->dungeons |= 1u << i;
        }
    }
    free(copy);
    d->dirty = 0;
}

int smz_disc_save(const SmzDiscovery *d, char *out, size_t cap)
{
    size_t n = 0;
    n += (size_t)snprintf(out + n, cap - n, "rooms");
    for (int i = 0; i < d->n_rooms && n + 12 < cap; i++) n += (size_t)snprintf(out + n, cap - n, " %d", d->rooms[i]);
    n += (size_t)snprintf(out + n, cap - n, "\nscreens");
    for (int s = 0; s < 128 && n + 8 < cap; s++) if (d->screens[s >> 3] & (1 << (s & 7))) n += (size_t)snprintf(out + n, cap - n, " %d", s);
    n += (size_t)snprintf(out + n, cap - n, "\ndungeons ");
    for (int i = 0; i < N_DUNGEONS && n + 24 < cap; i++) if (d->dungeons & (1u << i)) n += (size_t)snprintf(out + n, cap - n, "%s;", DUNGEON_NAMES[i]);
    n += (size_t)snprintf(out + n, cap - n, "\n");
    return n < cap ? (int)n : -1;
}

int smz_disc_count(const SmzDiscovery *d)
{
    int n = d->n_rooms + __builtin_popcount(d->dungeons);
    for (int i = 0; i < 16; i++) n += __builtin_popcount(d->screens[i]);
    return n;
}

static unsigned u16(const uint8_t *w, size_t a) { return w[a] | (w[a + 1] << 8); }

static int accept_room(SmzDiscovery *d, int state, int room)
{
    if (state != NORMAL_GAMEPLAY) { d->filter_room = -1; d->filter_count = 0; return 0; }
    if (room == d->filter_room) d->filter_count++; else { d->filter_room = room; d->filter_count = 1; }
    return d->filter_count >= 2;
}

static void update_metroid(SmzDiscovery *d, const uint8_t *w)
{
    int room = (int)u16(w, 0x79B);
    if (!accept_room(d, w[0x998], room)) return;
    const SmzSmRoom *r = find_room(d, room);
    if (!r) return;
    add_room(d, room);
    d->area = r->area;
    if (r->area < 0 || r->area >= SM_AREAS) return;
    float tx = (float)u16(w, 0x7A1) + (float)u16(w, 0xAF6) / 256.0f, ty = (float)u16(w, 0x7A3) + (float)u16(w, 0xAFA) / 256.0f;
    d->x = (SM_ORIGINS[r->area][0] - 16 + 32 * (tx + room_col(r) - r->mx)) / 2.0f;
    d->y = (SM_ORIGINS[r->area][1] - 16 + 32 * ty) / 2.0f;
    d->has_position = 1;
}

static void update_zelda(SmzDiscovery *d, const uint8_t *w)
{
    int dark = (w[0xF3CA] & 0x40) != 0, indoors = w[0x1B] != 0, dungeon = dungeon_of(w[0x040C]);
    if (indoors && dungeon >= 0) {
        add_dungeon(d, dungeon);
        const float *b = smz_disc_zelda_box(d, DUNGEON_NAMES[dungeon]);
        if (b) { d->x = (b[0] + b[2]) / 2; d->y = (b[1] + b[3]) / 2; d->has_position = 1; }
        return;
    }
    if (indoors) {
        /* houses and caves: the room number is the check address of the chest inside */
        int loc = smz3_zelda_room_location(u16(w, 0xA0));
        const SmzMarker *m = loc >= 0 ? smz_disc_marker(d, loc) : NULL;
        if (m) { d->x = (float)m->x; d->y = (float)m->y; d->has_position = 1; }
        else if (d->has_overworld) { d->x = d->ow_x; d->y = d->ow_y; d->has_position = 1; }
        return;
    }
    int area = w[0x8A];
    if (area >= 0x80) {
        int entry = area == 0x80 ? 0x00 : area == 0x81 ? 0x0F : -1;
        if (entry >= 0) add_screen(d, entry);
        if (area == 0x80 && d->has_pedestal) { d->x = d->pedestal_x; d->y = d->pedestal_y; d->has_position = 1; }
        else if (area == 0x81 && d->has_zora) { d->x = d->zora_x; d->y = d->zora_y; d->has_position = 1; }
        else if (d->has_overworld) { d->x = d->ow_x; d->y = d->ow_y; d->has_position = 1; }
        return;
    }
    int x = (int)u16(w, 0x22), y = (int)u16(w, 0x20);
    if (x > 4095) x = 4095;
    if (y > 4095) y = 4095;
    int base = dark ? 64 : 0;
    add_screen(d, (area & 0x3F) + base);
    add_screen(d, (y / 512) * 8 + x / 512 + base);
    float scale = 958.0f / 4096.0f / 2.0f;
    d->x = d->ow_x = x * scale;
    d->y = d->ow_y = y * scale + (dark ? 479.0f : 0.0f);
    d->has_position = d->has_overworld = 1;
}

void smz_disc_update(SmzDiscovery *d, const uint8_t *wram, size_t wram_len, int game)
{
    if (wram_len < 0x10000) return;
    if (game == 2) update_metroid(d, wram);
    else if (game == 1) update_zelda(d, wram);
}

static void room_rect(const SmzSmRoom *r, SmzRect *out)
{
    float x = (SM_ORIGINS[r->area][0] - 16 + 32 * room_col(r)) / 2.0f, y = (SM_ORIGINS[r->area][1] - 16 + 32 * r->my) / 2.0f;
    *out = (SmzRect){x, y, 16.0f * r->w, 16.0f * r->h};
}

int smz_disc_rects(const SmzDiscovery *d, SmzRect *out, int cap)
{
    int n = 0;
    for (int i = 0; i < d->n_rooms && n < cap; i++) {
        const SmzSmRoom *r = find_room(d, d->rooms[i]);
        if (r && r->area >= 0 && r->area < SM_AREAS) room_rect(r, &out[n++]);
    }
    float cell = 958.0f / 8 / 2;
    for (int s = 0; s < 128 && n < cap; s++) {
        if (!(d->screens[s >> 3] & (1 << (s & 7)))) continue;
        int col = (s % 64) % 8, row = (s % 64) / 8;
        out[n++] = (SmzRect){col * cell, row * cell + (s >= 64 ? 479.0f : 0.0f), cell, cell};
    }
    for (int i = 0; i < N_DUNGEONS && n < cap; i++) {
        if (!(d->dungeons & (1u << i))) continue;
        const float *b = smz_disc_zelda_box(d, DUNGEON_NAMES[i]);
        if (b) out[n++] = (SmzRect){b[0] - 26, b[1] - 26, b[2] - b[0] + 52, b[3] - b[1] + 52};
    }
    return n;
}

int smz_disc_area_box(const SmzDiscovery *d, int game, SmzRect *out)
{
    if (game == 1) { *out = (SmzRect){0, 0, 480, 958}; return 1; }
    if (d->area < 0 || d->area >= SM_AREAS) return 0;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < d->data->n_rooms; i++) {
        const SmzSmRoom *r = &d->data->rooms[i];
        if (r->area != d->area) continue;
        SmzRect q;
        room_rect(r, &q);
        if (q.x < x0) x0 = q.x; if (q.y < y0) y0 = q.y;
        if (q.x + q.w > x1) x1 = q.x + q.w; if (q.y + q.h > y1) y1 = q.y + q.h;
    }
    if (x1 < x0) return 0;
    *out = (SmzRect){x0 - 24, y0 - 24, x1 - x0 + 48, y1 - y0 + 48};
    return 1;
}

int smz_disc_metroid_room_centre(const SmzDiscovery *d, int ptr, float *x, float *y)
{
    const SmzSmRoom *r = find_room(d, ptr);
    if (!r || r->area < 0 || r->area >= SM_AREAS) return 0;
    SmzRect q;
    room_rect(r, &q);
    *x = q.x + 8.0f * r->w; *y = q.y + 8.0f * r->h;
    return 1;
}

int smz_disc_boss_marks(const SmzDiscovery *d, const uint8_t *wram, size_t wram_len, SmzBossMark *out, int cap)
{
    /* boss room of each dungeon: its "boss defeated" flag is bit 0x0800 of the word at $7EF000 + 2 * room */
    static const struct { const char *region; int room; } Z[] = {
        {"Eastern Palace", 200}, {"Desert Palace", 51}, {"Tower of Hera", 7}, {"Palace of Darkness", 90}, {"Swamp Palace", 6},
        {"Skull Woods", 41}, {"Thieves' Town", 172}, {"Ice Palace", 222}, {"Misery Mire", 144}, {"Turtle Rock", 164}, {"Castle Tower", 32},
    };
    /* room pointer, the area byte at $7ED828 + area, letter */
    static const struct { int room, area; char letter; } M[] = {{0xA59F, 1, 'K'}, {0xCD13, 3, 'P'}, {0xDA60, 4, 'D'}, {0xB32E, 2, 'R'}};
    int n = 0;
    if (wram_len < 0x10000) return 0;
    for (unsigned i = 0; i < sizeof Z / sizeof Z[0] && n < cap; i++) {
        const float *b = smz_disc_zelda_box(d, Z[i].region);
        if (!b) continue;
        out[n++] = (SmzBossMark){b[0] - 12, b[1] - 12, '?', (u16(wram, 0xF000 + 2 * Z[i].room) & 0x0800) != 0};
    }
    for (unsigned i = 0; i < sizeof M / sizeof M[0] && n < cap; i++) {
        float x, y;
        if (!smz_disc_metroid_room_centre(d, M[i].room, &x, &y)) continue;
        out[n++] = (SmzBossMark){x, y, M[i].letter, (wram[0xD828 + M[i].area] & 1) != 0};
    }
    return n;
}
