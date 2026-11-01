#include <stdlib.h>
#include <string.h>
#include "smz3_dungeonmaps.h"

static const int PACK_EA[] = {0x189610, 0x189857, 0x189b24, 0x17cd55, 0x17e842, 0x17eb38, 0x16c13d};   /* packs 97 98 99 80 86 87 51 (US addresses) */
static int pack_shift;   /* the Japanese v1.0 ROM keeps the compressed graphics 0x706 bytes lower and the two floor tables 0x10 higher */
static int table_shift;
static const int PACK_SETS[4][4] = {{0, 1, 2, 3}, {0, 4, 5, 3}, {0, 4, 2, 3}, {0, 4, 6, 3}};
static const int DUNGEON_SET[14] = {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 2, 1, 3, 1};
static const int FLOORS[14] = {0x21, 0x23, 0x20, 0x21, 0x70, 0x12, 0x11, 0x12, 0x02, 0x17, 0x60, 0x12, 0x13, 0x71};
static const int SIZES[14] = {75, 125, 50, 75, 175, 75, 50, 75, 50, 200, 150, 75, 100, 200};

typedef struct { const uint8_t *d; size_t len; } Rom;

static size_t rom_off(int ea) { return (size_t)((ea >> 16) & 0x7f) * 0x8000 + (size_t)(ea & 0x7fff); }
static int rom_byte(const Rom *r, int ea) { size_t o = rom_off(ea); return o < r->len ? r->d[o] : 0; }
static int rom_word(const Rom *r, int ea) { return rom_byte(r, ea) | rom_byte(r, ea + 1) << 8; }
static void rom_bytes(const Rom *r, int ea, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = (uint8_t)rom_byte(r, ea);
        ea++;
        if (!(ea & 0x8000)) ea += 0x8000;
    }
}

/* The game's graphics compression. Returns the size written (at most cap). */
static int decompress(const Rom *r, int ea, uint8_t *out, int cap)
{
    int n_out = 0;
    #define TAKE1() (ea_b = (uint8_t)rom_byte(r, ea), ea++, (!(ea & 0x8000) ? (ea += 0x8000) : 0), ea_b)
    uint8_t ea_b;
    for (;;) {
        int cmd = TAKE1(), n;
        if (cmd == 0xff) return n_out;
        if ((cmd & 0xe0) != 0xe0) { n = (cmd & 0x1f) + 1; cmd &= 0xe0; }
        else { n = TAKE1() + ((cmd & 3) << 8) + 1; cmd = (cmd << 3) & 0xe0; }
        if (n_out + n > cap) return n_out;
        if (cmd == 0) { for (int i = 0; i < n; i++) out[n_out++] = TAKE1(); }
        else if (cmd & 0x80) {
            int lo = TAKE1(), hi = TAKE1(), o = lo | hi << 8;
            for (int i = 0; i < n; i++) { out[n_out] = o < n_out ? out[o] : 0; n_out++; o++; }
        } else if (!(cmd & 0x40)) { int v = TAKE1(); for (int i = 0; i < n; i++) out[n_out++] = (uint8_t)v; }
        else if (!(cmd & 0x20)) { int lo = TAKE1(), hi = TAKE1(); for (int i = 0; i < n; i++) out[n_out++] = (uint8_t)(i % 2 == 0 ? lo : hi); }
        else { int v = TAKE1(); for (int i = 0; i < n; i++) out[n_out++] = (uint8_t)(v++ & 0xff); }
    }
    #undef TAKE1
}

int smz_z3_decompress(const uint8_t *z3, size_t len, int ea, uint8_t *out, int cap)
{
    Rom r = {z3, len};
    return decompress(&r, ea, out, cap);
}

static void tiles_for(const Rom *r, int set, uint8_t tiles[256][8][8])
{
    uint8_t buf[4096];
    for (int k = 0; k < 4; k++) {
        memset(buf, 0, sizeof buf);
        decompress(r, PACK_EA[PACK_SETS[set][k]] + pack_shift, buf, sizeof buf);
        for (int t = 0; t < 64; t++) {
            const uint8_t *src = buf + t * 24;
            for (int row = 0; row < 8; row++)
                for (int x = 0; x < 8; x++) {
                    int bit = 7 - x;
                    tiles[k * 64 + t][row][x] = (uint8_t)(((src[row * 2] >> bit) & 1) | (((src[row * 2 + 1] >> bit) & 1) << 1) | (((src[16 + row] >> bit) & 1) << 2));
                }
        }
    }
}

static void put(uint8_t *img, int x, int y, int w15)
{
    uint8_t *p = img + ((size_t)y * SMZ_DM_ATLAS_W + x) * 4;
    p[0] = (uint8_t)((w15 & 31) * 255 / 31); p[1] = (uint8_t)(((w15 >> 5) & 31) * 255 / 31); p[2] = (uint8_t)(((w15 >> 10) & 31) * 255 / 31); p[3] = 255;
}

/* Tile positions of a room's chests (the type 3 objects 0x19 and 0x31 of its object stream). */
static int room_chests(const Rom *r, int room, uint8_t xy[SMZ_DM_CHESTS][2])
{
    int ptr = rom_byte(r, 0x1F8000 + room * 3) | rom_byte(r, 0x1F8000 + room * 3 + 1) << 8 | rom_byte(r, 0x1F8000 + room * 3 + 2) << 16;
    int p = ptr + 2, layer = 0, n = 0;
    while (layer < 3) {
        int w = rom_byte(r, p) | rom_byte(r, p + 1) << 8;
        if (w == 0xFFFF) { layer++; p += 2; continue; }
        if (w == 0xFFF0) break;
        int b1 = rom_byte(r, p), b2 = rom_byte(r, p + 1), b3 = rom_byte(r, p + 2);
        p += 3;
        if ((b1 & 0xfc) != 0xfc && b3 >= 0xf8) {
            int idx = (b3 & 7) << 4 | (b2 & 3) << 2 | (b1 & 3);
            if ((idx == 0x19 || idx == 0x31) && n < SMZ_DM_CHESTS) { xy[n][0] = (uint8_t)(b1 >> 2); xy[n][1] = (uint8_t)(b2 >> 2); n++; }
        }
        if (p > 0x3FFFFF) break;
    }
    return n;
}

int smz_dungeon_maps(const uint8_t *z3, size_t len, SmzDungeonMeta *meta, uint8_t *full, uint8_t *hint)
{
    if (len < 0x100000) return 0;
    Rom r = {z3, len};
    int japan = z3[0x7FD9] == 0;
    pack_shift = japan ? -0x706 : 0;
    table_shift = japan ? 0x10 : 0;
    static const uint8_t KEY[16] = {0x61, 0x0b, 0x61, 0x53, 0x61, 0x8b, 0x62, 0x8b, 0x60, 0x0b, 0x63, 0x0b, 0x60, 0x8b, 0x64, 0x0b};
    int pieces[744], found = 0;
    for (size_t i = 0; i + 744 * 2 <= len; i++)
        if (z3[i] == KEY[0] && !memcmp(z3 + i, KEY, 16)) {
            for (int k = 0; k < 744; k++) pieces[k] = z3[i + k * 2] | z3[i + k * 2 + 1] << 8;
            found = 1;
            break;
        }
    if (!found) return 0;
    int pal[96];
    for (int i = 0; i < 96; i++) pal[i] = rom_word(&r, 0x9BE544 + 2 * i);
    memset(meta, 0, sizeof *meta);
    memset(meta->grid, 0xF, sizeof meta->grid);
    memset(full, 0, (size_t)SMZ_DM_ATLAS_W * SMZ_DM_ATLAS_H * 4);
    memset(hint, 0, (size_t)SMZ_DM_ATLAS_W * SMZ_DM_ATLAS_H * 4);
    for (size_t i = 3; i < (size_t)SMZ_DM_ATLAS_W * SMZ_DM_ATLAS_H * 4; i += 4) full[i] = hint[i] = 255;
    static uint8_t tiles[256][8][8];
    int tiles_set = -1;
    for (int d = 0; d < 14; d++) {
        uint8_t layout[256], shapes[256];
        int ls = SIZES[d], zeros = 0, below = FLOORS[d] & 0xf, above = FLOORS[d] >> 4;
        rom_bytes(&r, 0xA0000 + rom_word(&r, 0x8AF605 + table_shift + d * 2), layout, ls);
        int empty = 1;
        for (int i = 0; i < 25 && i < ls; i++) if (layout[i] != 0xf) empty = 0;
        if (empty && below == 0 && above > 1) { memmove(layout, layout + 25, (size_t)(ls - 25)); ls -= 25; above--; }   /* Castle Tower's table starts with an empty floor under 1F */
        for (int i = 0; i < ls; i++) if (layout[i] == 0xf) zeros++;
        int sc = ls - zeros;
        rom_bytes(&r, 0xA0000 + rom_word(&r, 0x8AFBE4 + table_shift + d * 2), shapes, sc);
        if (DUNGEON_SET[d] != tiles_set) { tiles_set = DUNGEON_SET[d]; tiles_for(&r, tiles_set, tiles); }
        meta->below[d] = (uint8_t)below; meta->above[d] = (uint8_t)above;
        for (int f = 0; f < below + above && f < SMZ_DM_FLOORS; f++) {
            for (int c = 0; c < 25; c++) meta->grid[d][f][c] = f * 25 + c < ls ? layout[f * 25 + c] : 0xF;
            for (int mode = 0; mode < 2; mode++) {
                uint8_t *img = mode ? hint : full;
                for (int cell = 0; cell < 25; cell++) {
                    int v = meta->grid[d][f][cell], yv = 0x51;
                    if (v != 0xf) {
                        int first = 0;
                        while (first < ls && layout[first] != v) first++;
                        int count = 0;
                        for (int i = 0; i < first; i++) if (layout[i] != 0xf) count++;
                        yv = count < sc ? shapes[count] : 0x51;
                    }
                    for (int q = 0; q < 4; q++) {
                        int org = pieces[yv * 4 + q], av;
                        if (mode == 1) {
                            if (org == 0xb00) continue;
                            av = (org & 0x1000) ? ((org & ~0x1c00) | 0xc00) : org + 0x400;
                        } else av = org;
                        int t = (av & 0x3ff) - 0x300, p = (av >> 10) & 7;
                        if (t < 0 || t >= 256 || p < 2) continue;
                        int x0 = f * SMZ_DM_CELL + (cell % 5) * SMZ_DM_ROOM + (q & 1) * 8, y0 = d * SMZ_DM_CELL + (cell / 5) * SMZ_DM_ROOM + (q >> 1) * 8;
                        for (int y = 0; y < 8; y++) {
                            int sy = (av & 0x8000) ? 7 - y : y;
                            for (int x = 0; x < 8; x++) {
                                int sx = (av & 0x4000) ? 7 - x : x, v2 = tiles[t][sy][sx];
                                if (v2) put(img, x0 + x, y0 + y, pal[(p - 2) * 16 + v2]);
                            }
                        }
                    }
                }
            }
        }
    }
    for (int d = 0; d < 14; d++)
        for (int f = 0; f < meta->below[d] + meta->above[d]; f++)
            for (int c = 0; c < 25; c++) {
                int room = meta->grid[d][f][c];
                if (room == 0xF || room >= SMZ_DM_ROOMS || meta->chest_n[room]) continue;
                meta->chest_n[room] = (uint8_t)room_chests(&r, room, meta->chest_xy[room]);
            }
    return 1;
}
