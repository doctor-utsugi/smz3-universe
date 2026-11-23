#include <string.h>
#include "smz3_romderived.h"

static size_t sm_off(int bank, int addr) { return (size_t)(bank - 0x80) * 0x8000 + (size_t)(addr & 0x7FFF); }
static int u16(const uint8_t *rom, int bank, int addr) { size_t o = sm_off(bank, addr); return rom[o] | rom[o + 1] << 8; }

int smz_sm_rooms(const uint8_t *sm, size_t sm_len, int *out, int max_rooms)
{
    if (sm_len < 0x200000) return 0;
    int seen[SMZ_SM_ROOMS_MAX], n = 0;
    int queue[4096], qn = 0;
    queue[qn++] = 0x91F8;
    while (qn > 0) {
        int ptr = queue[--qn];
        int known = 0;
        for (int i = 0; i < n; i++) if (seen[i] == ptr) { known = 1; break; }
        if (known || n >= max_rooms) continue;
        size_t o = sm_off(0x8F, ptr);
        const uint8_t *h = sm + o;
        seen[n] = ptr;
        out[n * 6 + 0] = ptr; out[n * 6 + 1] = h[1]; out[n * 6 + 2] = h[2]; out[n * 6 + 3] = h[3]; out[n * 6 + 4] = h[4]; out[n * 6 + 5] = h[5];
        n++;
        int dl = h[9] | h[10] << 8;
        for (int p = dl; ; p += 2) {
            int door = u16(sm, 0x8F, p);
            if (door < 0x8000 || n > 400) break;
            /* a door pointer list ends where the next word is no longer a bank-83 pointer */
            if (door < 0x8800 || door > 0xAC00) break;
            int dest = u16(sm, 0x83, door);
            if (dest >= 0x8000 && dest < 0xFFFF) {
                const uint8_t *d = sm + sm_off(0x8F, dest);
                int dup = 0;
                for (int i = 0; i < n; i++) if (seen[i] == dest) { dup = 1; break; }
                if (d[1] <= 7 && d[4] && d[5] && d[4] <= 20 && d[5] <= 20 && !dup && qn < 4096) queue[qn++] = dest;
            }
            if (p + 2 - dl > 60) break;
        }
    }
    return n;
}

/* ---- Original sprite previews ---- */
typedef struct { int off, flip, x, y; } Piece;

static void colour(int w, uint8_t *rgb) { rgb[0] = (w & 31) * 8; rgb[1] = ((w >> 5) & 31) * 8; rgb[2] = ((w >> 10) & 31) * 8; }

static void draw(const uint8_t *rom, const uint8_t pal[16][3], const Piece *pieces, int count, int w, int h, uint8_t *out)
{
    static int8_t img[64][64];
    memset(img, 0, sizeof img);
    for (int i = 0; i < count; i++) {
        const uint8_t *b = rom + pieces[i].off;
        int t[64];
        for (int y = 0; y < 8; y++) {
            int p0 = b[y * 2], p1 = b[y * 2 + 1], p2 = b[16 + y * 2], p3 = b[17 + y * 2];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                t[y * 8 + x] = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
            }
        }
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int v = t[((pieces[i].flip & 2) ? 7 - y : y) * 8 + ((pieces[i].flip & 1) ? 7 - x : x)];
                int px = x + pieces[i].x, py = y + pieces[i].y;
                if (v && px >= 0 && px < 64 && py >= 0 && py < 64 && !img[py][px]) img[py][px] = (int8_t)v;   /* an earlier piece keeps its pixels */
            }
    }
    out[0] = (uint8_t)w; out[1] = (uint8_t)h;
    uint8_t *o = out + 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v = (x < 64 && y < 64) ? img[y][x] : 0;
            if (v) { *o++ = pal[v][0]; *o++ = pal[v][1]; *o++ = pal[v][2]; *o++ = 255; } else { *o++ = 0; *o++ = 0; *o++ = 0; *o++ = 0; }
        }
}

/* Link standing and facing down: a 16x16 head (tiles 2, 3, 18, 19 of the sheet at 0x80000) and the body tiles. */
static const int LINK_T[][4] = {{2, 0, 0, 0}, {3, 0, 8, 0}, {18, 0, 0, 8}, {19, 0, 8, 8}, {54, 0, 0, 16}, {54, 1, 8, 16}, {38, 0, 0, 8}, {39, 0, 8, 8},
                                {317, 0, 1, 16}, {46, 3, 8, 17}};
/* Samus (power suit) standing: tile offsets into the JU ROM and their positions, 32x53. */
static const int SAMUS_T[][4] = {{998048, 0, 13, 12}, {954464, 0, 13, 20}, {997920, 0, 13, 4}, {998112, 0, 5, 12}, {998080, 0, 21, 12}, {912224, 0, 8, 28},
                                 {997984, 0, 5, 4}, {954592, 0, 5, 44}, {997952, 0, 21, 4}, {954560, 0, 1, 20}, {954496, 0, 21, 20}, {912128, 0, 16, 44},
                                 {911872, 0, 16, 36}, {911872, 1, 9, 36}, {954720, 0, 13, 28}, {911968, 0, 8, 20}, {954752, 0, 21, 28}, {912192, 0, 0, 28},
                                 {912160, 0, 24, 44}, {912096, 0, 10, 44}};

int smz_original_previews(const uint8_t *sm, size_t sm_len, const uint8_t *z3, size_t z3_len, uint8_t *link_pv, uint8_t *samus_pv)
{
    if (sm_len < 0x200000 || z3_len < 0x100000) return 0;
    uint8_t zpal[16][3] = {{0}}, spal[16][3] = {{0}};
    for (int i = 0; i < 15; i++) colour(z3[0xDD308 + i * 2] | z3[0xDD309 + i * 2] << 8, zpal[i + 1]);
    for (int i = 1; i < 16; i++) colour(sm[0xD9400 + i * 2] | sm[0xD9401 + i * 2] << 8, spal[i]);
    Piece lp[10], sp[20];
    for (int i = 0; i < 10; i++) lp[i] = (Piece){0x80000 + LINK_T[i][0] * 32, LINK_T[i][1], LINK_T[i][2], LINK_T[i][3]};
    for (int i = 0; i < 20; i++) sp[i] = (Piece){SAMUS_T[i][0], SAMUS_T[i][1], SAMUS_T[i][2], SAMUS_T[i][3]};
    draw(z3, zpal, lp, 10, 16, 24, link_pv);
    draw(sm, spal, sp, 20, 32, 53, samus_pv);
    return 1;
}
