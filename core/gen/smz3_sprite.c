#include <string.h>
#include "smz3_sprite.h"

typedef struct { int addr[2]; int length; int count; int stride; } Field;

#define F(a, len, n, stride) {{a, -1}, len, n, stride}
#define F2(a, b, len) {{a, b}, len, 1, 0}

static const Field LINK[] = {
    F(0x508000, 0x7000, 1, 0), F(0x5BD308, 4 * 30, 1, 0), F(0x5BEDF5, 4, 1, 0),
};

static const Field SAMUS[] = {
    F(0x440000, 0x8000, 1, 0), F(0x450000, 0x8000, 1, 0), F(0x460000, 0x8000, 1, 0), F(0x470000, 0x8000, 1, 0),
    F(0x480000, 0x8000, 1, 0), F(0x490000, 0x8000, 1, 0), F(0x4A0000, 0x8000, 1, 0), F(0x4B0000, 0x8000, 1, 0),
    F(0x540000, 0x8000, 1, 0), F(0x550000, 0x8000, 1, 0), F(0x560000, 0x8000, 1, 0), F(0x570000, 0x8000, 1, 0),
    F(0x580000, 0x8000, 1, 0), F(0x590000, 0x7880, 1, 0), F(0x5A0000, 0x3F60, 1, 0), F(0x5A4000, 0x3F60, 1, 0),
    F(0x1A9A00, 0x3C0, 1, 0), F(0x36DA00, 0x600, 1, 0), F(0x36D900, 0x20, 1, 0), F(0x36D980, 0x20, 1, 0),
    F(0x1B9402, 30, 1, 0), F(0x1B9522, 30, 1, 0), F(0x1B9802, 30, 1, 0),
    /* loaders: nine palettes at irregular offsets, handled below */
    F(0xDDB6D, 30, -9, 0), F(0xDDCD3, 30, -9, 0), F(0xDDE39, 30, -9, 0),
    F(0xDE468, 30, 16, 0x22), F(0xDE694, 30, 16, 0x22), F(0xDE8C0, 30, 16, 0x22),
    F(0x1B9822, 30, 8, 0x20), F(0x1B9922, 30, 8, 0x20), F(0x1B9A22, 30, 8, 0x20),
    F(0x1B9B22, 30, 4, 0x20), F(0x1B9D22, 30, 4, 0x20), F(0x1B9F22, 30, 4, 0x20),
    F(0x1B9BA2, 30, 4, 0x20), F(0x1B9DA2, 30, 4, 0x20), F(0x1B9FA2, 30, 4, 0x20),
    F(0x1B9C22, 30, 4, 0x20), F(0x1B9E22, 30, 4, 0x20), F(0x1BA022, 30, 4, 0x20),
    F(0x1B9CA2, 30, 4, 0x20), F(0x1B9EA2, 30, 4, 0x20), F(0x1BA0A2, 30, 4, 0x20),
    F(0x1B96C2, 30, 6, 0x20), F(0x1BA122, 30, 9, 0x20), F(0x1BA242, 30, 10, 0x20),
    F2(0x1BA3A2, 0xCE56B, 30), F(0x1BA382, 30, 1, 0), F2(0x1BA3C0, 0x1BA3C6, 6),
    F(0x2E52C, 2, 1, 0), F(0xEE5E2, 30, 1, 0), F(0xCE68B, 30, 1, 0), F(0xDD6C2, 30, 16, 0x24),
    F(0x22A5A0, 28, 1, 0), F(0xDCA54, 2, 14, 0x6),
};

static const int LOADER_OFFSETS[9] = {0x0, 0x24, 0x4F, 0x73, 0x9E, 0xC2, 0xED, 0x111, 0x139};

static size_t field_bytes(const Field *f)
{
    int n = f->count < 0 ? -f->count : f->count;
    return (size_t)f->length * n;
}

/* Copies one field's slice of the block to the ROM; returns false when it would write past the ROM. */
static int apply_field(uint8_t *rom, size_t rom_size, const Field *f, const uint8_t *data)
{
    int n = f->count < 0 ? -f->count : f->count;
    for (int a = 0; a < 2 && f->addr[a] >= 0; a++)
        for (int i = 0; i < n; i++) {
            size_t off = (size_t)f->addr[a] + (f->count < 0 ? (size_t)LOADER_OFFSETS[i] : (size_t)i * f->stride);
            if (off + f->length > rom_size) return 0;
            memcpy(rom + off, data + (size_t)i * f->length, f->length);
        }
    return 1;
}

static int u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (int)((unsigned)p[3] << 24); }

static int apply_block(uint8_t *rom, size_t rom_size, const uint8_t *rdc, size_t len, size_t pos, const Field *fields, int nfields)
{
    for (int i = 0; i < nfields; i++) {
        size_t b = field_bytes(&fields[i]);
        if (pos + b > len) return 0;
        if (!apply_field(rom, rom_size, &fields[i], rdc + pos)) return 0;
        pos += b;
    }
    return 1;
}

int smz_sprite_apply_rdc(uint8_t *rom, size_t rom_size, const uint8_t *rdc, size_t len)
{
    static const char HDR[] = "RETRODATACONTAINER";
    if (len < 23 || memcmp(rdc, HDR, 18) || rdc[18] != 1) return -1;
    uint32_t blocks = (uint32_t)u32(rdc + 19);
    if (blocks > 64 || 23 + (size_t)blocks * 8 > len) return -1;
    int applied = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t type = (uint32_t)u32(rdc + 23 + i * 8), off = (uint32_t)u32(rdc + 27 + i * 8);
        if (type == 1 || type == 4) {
            const Field *f = type == 1 ? LINK : SAMUS;
            int n = type == 1 ? (int)(sizeof LINK / sizeof *LINK) : (int)(sizeof SAMUS / sizeof *SAMUS);
            if (off > len || !apply_block(rom, rom_size, rdc, len, off, f, n)) return -1;
            applied++;
        }
    }
    return applied;
}

const char *smz_rdc_author(const uint8_t *rdc, size_t len)
{
    if (len < 23 || memcmp(rdc, "RETRODATACONTAINER", 18)) return "";
    uint32_t blocks = (uint32_t)u32(rdc + 19);
    size_t p = 23 + (size_t)blocks * 8;
    for (size_t q = p; q < len; q++) if (!rdc[q]) return (const char *)rdc + p;
    return "";
}
