#include <string.h>
#include "smz3_smmap.h"

#define AREA_PTRS 0x82964A        /* long pointers to each area's 64 x 32 tilemap */
#define AREA_COLORS 0x82F799      /* map_area.asm's area_palettes_explored, then area_palettes_explored_light */
#define PAUSE_PALETTE 0xB6F000
#define PARTIAL_BLACK 0x85AB00
#define MAP_TILES 0xE28000        /* the pause screen's BG1/BG2 4bpp tiles, 0x300 of them */
#define BG2_TILEMAP 0xB6E000
#define ITEM_LISTS 0x83E000       /* per area: list offset (bank $83), then at +12 its length */
#define DOOR_REVEAL 0x85A180
#define SRAM_REVEALED 0x2000
#define SRAM_AREAS 0x2600
#define SRAM_PARTIAL 0x2700

static const char *const AREA_NAMES[SMZ_SMMAP_AREAS] = {"Crateria", "Brinstar", "Norfair", "Wrecked Ship", "Maridia", "Tourian"};

static size_t pc(uint32_t snes) { return (snes >> 1 & 0x3F8000) | (snes & 0x7FFF); }

static unsigned rd16(const uint8_t *rom, size_t len, uint32_t snes)
{
    size_t a = pc(snes);
    return a + 1 < len ? (unsigned)(rom[a] | rom[a + 1] << 8) : 0;
}

static unsigned ram16(const uint8_t *ram, size_t len, size_t a) { return a + 1 < len ? (unsigned)(ram[a] | ram[a + 1] << 8) : 0; }

static uint32_t argb(unsigned c)
{
    unsigned r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
    return 0xFF000000u | (r * 255 / 31) << 16 | (g * 255 / 31) << 8 | b * 255 / 31;
}

static uint32_t area_ptr(const uint8_t *rom, size_t len, int area)
{
    size_t a = pc(AREA_PTRS) + 3 * (size_t)area;
    return a + 2 < len ? (uint32_t)(rom[a] | rom[a + 1] << 8 | rom[a + 2] << 16) : 0;
}

int smz_smmap_rom_ok(const uint8_t *rom, size_t rom_len)
{
    if (!rom || rom_len < 0x400000) return 0;
    for (int a = 0; a < SMZ_SMMAP_AREAS; a++) if ((area_ptr(rom, rom_len, a) & 0xFF0FFF) != 0xB50000) return 0;
    return rd16(rom, rom_len, ITEM_LISTS) == (ITEM_LISTS & 0xFFFF) + 24;
}

void smz_smmap_init(SmzSmMap *m)
{
    memset(m, 0, sizeof *m);
    m->area = m->shown = m->samus_area = m->samus_x = m->samus_y = -1;
}

const char *smz_smmap_area_name(int area) { return area >= 0 && area < SMZ_SMMAP_AREAS ? AREA_NAMES[area] : ""; }

static void tile_px(const uint8_t *rom, size_t len, unsigned word, const uint32_t *pal16, uint32_t *out, int stride)
{
    size_t b = pc(MAP_TILES) + (size_t)(word & 0x3FF) * 32;
    if (b + 32 > len) return;
    for (int y = 0; y < 8; y++) {
        int sy = word & 0x8000 ? 7 - y : y;
        const uint8_t *r = rom + b + sy * 2;
        for (int x = 0; x < 8; x++) {
            int bit = 7 - (word & 0x4000 ? 7 - x : x);
            int c = (r[0] >> bit & 1) | (r[1] >> bit & 1) << 1 | (r[16] >> bit & 1) << 2 | (r[17] >> bit & 1) << 3;
            out[y * stride + x] = c ? pal16[c] : 0;
        }
    }
}

static int sram_bit(const uint8_t *sram, size_t len, size_t base, int area, int byte, int bit)
{
    size_t a = base + (size_t)area * 0x100 + byte;
    return a < len && sram[a] >> (7 - bit) & 1;
}

/* What the pause screen's map would show now: a hash of the bytes it is built from. */
static uint32_t state_key(const uint8_t *wram, size_t wl, const uint8_t *sram, size_t sl, int area, int sx, int sy)
{
    uint32_t h = 2166136261u;
#define MIX(p, n) for (size_t i_ = 0; i_ < (n); i_++) h = (h ^ (p)[i_]) * 16777619u
    if (wl >= 0x10000) { MIX(wram + 0x07F7, 0x100); MIX(wram + 0xCD52, 0x600); MIX(wram + 0xD870, 0x40); }
    if (sl >= 0x2D00) { MIX(sram + SRAM_REVEALED, 0x600); MIX(sram + SRAM_PARTIAL, 0x600); }
#undef MIX
    return (h ^ (uint32_t)(area * 131 + sx * 7 + sy * 1031)) * 16777619u;
}

int smz_smmap_update(SmzSmMap *m, const uint8_t *rom, size_t rom_len, const uint8_t *wram, size_t wram_len,
                     const uint8_t *sram, size_t sram_len)
{
    if (!smz_smmap_rom_ok(rom, rom_len) || !wram || wram_len < 0x10000) return 0;
    unsigned state = ram16(wram, wram_len, 0x0998), cur = ram16(wram, wram_len, 0x1F5B);
    int playing = state >= 0x07 && state <= 0x13 && cur < SMZ_SMMAP_AREAS && ram16(wram, wram_len, 0x079B) != 0;
    m->samus_area = playing ? (int)cur : -1;
    m->samus_x = playing ? (int)(ram16(wram, wram_len, 0x07A1) + (ram16(wram, wram_len, 0x0AF6) >> 8)) : -1;
    m->samus_y = playing ? (int)(ram16(wram, wram_len, 0x07A3) + (ram16(wram, wram_len, 0x0AFA) >> 8) + 1) : -1;
    int area = m->area >= 0 ? m->area : m->samus_area >= 0 ? m->samus_area : m->shown;
    if (area < 0) return 0;
    uint32_t key = state_key(wram, wram_len, sram, sram_len, area, m->samus_x, m->samus_y);
    if (key == m->key && area == m->shown) return 0;
    m->key = key;
    m->shown = area;

    uint32_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = argb(rd16(rom, rom_len, PAUSE_PALETTE + 2 * i));
    unsigned black = rd16(rom, rom_len, PARTIAL_BLACK);
    pal[0x25] = argb(0);
    pal[0x37] = pal[0x3D] = argb(black);
    pal[0x21] = argb(rd16(rom, rom_len, AREA_COLORS + 2 * area));
    pal[0x22] = argb(rd16(rom, rom_len, AREA_COLORS + 12 + 2 * area));
    pal[0x23] = pal[0x73] = argb(0x7FFF);
    pal[0x71] = argb(0x294A);
    pal[0x72] = argb(0x4631);

    uint16_t src[2048], out[2048];
    uint32_t base = area_ptr(rom, rom_len, area);
    for (int i = 0; i < 2048; i++) src[i] = (uint16_t)rd16(rom, rom_len, base + 2 * i);
    unsigned list = rd16(rom, rom_len, ITEM_LISTS + 2 * area), n = rd16(rom, rom_len, ITEM_LISTS + 12 + 2 * area);
    for (unsigned k = 0; k < n && k < 512; k++) {   /* item_dots_disappear.asm: a check not yet taken keeps its dot */
        uint32_t r = 0x830000 | ((list + 6 * k) & 0xFFFF);
        size_t a = pc(r);
        if (a + 6 > rom_len) break;
        unsigned off = rd16(rom, rom_len, r + 2) / 2;
        if (!(wram[0xD870 + rom[a]] & rom[a + 1]) && off < 2048) src[off] = (uint16_t)rd16(rom, rom_len, r + 4);
    }
    const uint8_t *explored = area == m->samus_area ? wram + 0x07F7 : wram + 0xCD52 + area * 0x100;
    for (int i = 0; i < 2048; i++) {
        int byte = i / 8, bit = i % 8;
        if (explored[byte] >> (7 - bit) & 1) out[i] = src[i] & 0xEBFF;
        else if (sram_bit(sram, sram_len, SRAM_REVEALED, area, byte, bit)) out[i] = src[i];
        else if (sram_bit(sram, sram_len, SRAM_PARTIAL, area, byte, bit)) out[i] = src[i] & 0xEFFF;
        else out[i] = 0x001F;
    }
    for (uint32_t r = DOOR_REVEAL; r + 14 <= DOOR_REVEAL + 0x56 && rd16(rom, rom_len, r) != 0xFFFF; r += 14) {
        if ((int)rd16(rom, rom_len, r) != area) continue;
        unsigned trig = rd16(rom, rom_len, r + 2), tmask = rd16(rom, rom_len, r + 4);
        unsigned dst = rd16(rom, rom_len, r + 6), dmask = rd16(rom, rom_len, r + 8), at = rd16(rom, rom_len, r + 10);
        if (trig >= sram_len || dst >= sram_len || !(sram[trig] & tmask) || (sram[dst] & dmask)) continue;
        if (at >= 0x4000 && (at - 0x4000) / 2 < 2048) out[(at - 0x4000) / 2] = (uint16_t)rd16(rom, rom_len, r + 12);
    }

    m->x0 = m->y0 = 1 << 20;
    m->x1 = m->y1 = 0;
    for (int i = 0; i < 2048; i++) {
        int tx = (i & 0x400 ? 32 : 0) + i % 32, ty = i % 0x400 / 32;
        tile_px(rom, rom_len, out[i], pal + 16 * (out[i] >> 10 & 7), m->px + ty * 8 * SMZ_SMMAP_W + tx * 8, SMZ_SMMAP_W);
        if (out[i] == 0x001F) continue;
        if (tx * 8 < m->x0) m->x0 = tx * 8;
        if (ty * 8 < m->y0) m->y0 = ty * 8;
        if (tx * 8 + 8 > m->x1) m->x1 = tx * 8 + 8;
        if (ty * 8 + 8 > m->y1) m->y1 = ty * 8 + 8;
    }
    if (m->x1 <= m->x0) m->x0 = m->y0 = m->x1 = m->y1 = 0;

    unsigned g = rd16(rom, rom_len, BG2_TILEMAP + 2 * (10 * 32 + 10));
    tile_px(rom, rom_len, g, pal + 16 * (g >> 10 & 7), m->grid, 8);
    return 1;
}

void smz_smmap_step(SmzSmMap *m, const uint8_t *sram, size_t sram_len, int dir)
{
    if (!dir) { m->area = -1; return; }
    unsigned mask = sram_len > SRAM_AREAS + 1 ? (unsigned)(sram[SRAM_AREAS] | sram[SRAM_AREAS + 1] << 8) : 0;
    int a = m->area >= 0 ? m->area : m->shown >= 0 ? m->shown : 0;
    for (int k = 0; k < SMZ_SMMAP_AREAS; k++) {
        a = (a + (dir > 0 ? 1 : SMZ_SMMAP_AREAS - 1)) % SMZ_SMMAP_AREAS;
        if (mask >> a & 1 || a == m->samus_area) break;
    }
    m->area = a == m->samus_area ? -1 : a;
}

void smz_smmap_home(const SmzSmMap *m, int w, int h, float *cx, float *cy, float *zoom)
{
    int bw = m->x1 - m->x0, bh = m->y1 - m->y0, z = 1;
    while (bw > 0 && (z + 1) * bw <= w && (z + 1) * bh <= h) z++;
    *zoom = (float)z;
    int here = m->samus_area == m->shown && m->samus_x >= 0;
    if (bw > 0 && bw * z <= w && bh * z <= h) { *cx = (m->x0 + m->x1) / 2.0f; *cy = (m->y0 + m->y1) / 2.0f; }
    else if (here) { *cx = m->samus_x * 8 + 4.0f; *cy = m->samus_y * 8 + 4.0f; }
    else if (bw > 0) { *cx = (m->x0 + m->x1) / 2.0f; *cy = (m->y0 + m->y1) / 2.0f; }
    else { *cx = SMZ_SMMAP_W / 2.0f; *cy = SMZ_SMMAP_H / 2.0f; }
}

void smz_smmap_draw(const SmzSmMap *m, uint32_t *out, int w, int h, int stride, float cx, float cy, float zoom)
{
    if (zoom <= 0) zoom = 1;
    float ox = cx - w / 2.0f / zoom, oy = cy - h / 2.0f / zoom;
    int mark = m->samus_area == m->shown && m->samus_x >= 0;
    int bx = m->samus_x * 8, by = m->samus_y * 8;
    static int col[4096];
    int cw = w < 4096 ? w : 4096;
    for (int x = 0; x < cw; x++) { float f = ox + (x + 0.5f) / zoom; col[x] = f < 0 ? -1 - (int)(-f) : (int)f; }
    for (int y = 0; y < h; y++) {
        float fy = oy + (y + 0.5f) / zoom;
        int my = fy < 0 ? -1 - (int)(-fy) : (int)fy;
        uint32_t *row = out + (size_t)y * stride;
        int in_y = my >= 0 && my < SMZ_SMMAP_H && m->shown >= 0;
        for (int x = 0; x < cw; x++) {
            int mx = col[x];
            uint32_t c = 0;
            if (in_y && mx >= 0 && mx < SMZ_SMMAP_W) {
                c = m->px[my * SMZ_SMMAP_W + mx];
                int dx = mx - bx, dy = my - by;   /* the pause screen's indicator: white corners around her tile */
                if (mark && dx >= -2 && dx <= 9 && dy >= -2 && dy <= 9 && (dx == -2 || dx == 9 || dy == -2 || dy == 9)
                    && (dx <= 1 || dx >= 6) && (dy <= 1 || dy >= 6)) c = 0xFFFFFFFFu;
            }
            if (!c) c = m->grid[(my & 7) * 8 + (mx & 7)];
            row[x] = c ? c | 0xFF000000u : 0xFF000000u;
        }
    }
}
