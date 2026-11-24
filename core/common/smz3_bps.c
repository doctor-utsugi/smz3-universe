#include "smz3_bps.h"

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static int num(const uint8_t *p, size_t end, size_t *at, uint64_t *v)
{
    uint64_t data = 0, shift = 1;
    while (*at < end) {
        uint8_t x = p[(*at)++];
        data += (x & 0x7f) * shift;
        if (x & 0x80) { *v = data; return 0; }
        shift <<= 7;
        data += shift;
        if (shift > (1ull << 56)) return -1;
    }
    return -1;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

size_t smz_bps_target_size(const uint8_t *patch, size_t n)
{
    size_t at = 4;
    uint64_t s, t;
    if (n < 16 || patch[0] != 'B' || patch[1] != 'P' || patch[2] != 'S' || patch[3] != '1') return 0;
    if (num(patch, n - 12, &at, &s) || num(patch, n - 12, &at, &t)) return 0;
    return (size_t)t;
}

int smz_bps_apply(const uint8_t *patch, size_t n, const uint8_t *src, size_t src_n, uint8_t *out, size_t out_cap)
{
    size_t t_size = smz_bps_target_size(patch, n), end = n - 12, at = 4;
    uint64_t s_size, t, meta;
    if (!t_size || t_size > out_cap || crc32(patch, n - 4) != le32(patch + n - 4)) return -1;
    num(patch, end, &at, &s_size);
    num(patch, end, &at, &t);
    if (num(patch, end, &at, &meta) || meta > end - at) return -1;
    at += meta;
    if (s_size != src_n || crc32(src, src_n) != le32(patch + n - 12)) return -1;
    size_t o = 0, s_rel = 0, t_rel = 0;
    while (at < end) {
        uint64_t a, off;
        if (num(patch, end, &at, &a)) return -1;
        size_t len = (size_t)(a >> 2) + 1;
        if (len > t_size - o) return -1;
        switch (a & 3) {
        case 0:   /* SourceRead */
            if (o + len > src_n) return -1;
            for (size_t i = 0; i < len; i++, o++) out[o] = src[o];
            break;
        case 1:   /* TargetRead */
            if (len > end - at) return -1;
            for (size_t i = 0; i < len; i++) out[o++] = patch[at++];
            break;
        case 2:   /* SourceCopy */
            if (num(patch, end, &at, &off)) return -1;
            s_rel += (off & 1 ? -1 : 1) * (int64_t)(off >> 1);
            if (s_rel + len > src_n) return -1;
            for (size_t i = 0; i < len; i++) out[o++] = src[s_rel++];
            break;
        default:  /* TargetCopy */
            if (num(patch, end, &at, &off)) return -1;
            t_rel += (off & 1 ? -1 : 1) * (int64_t)(off >> 1);
            if (t_rel >= o) return -1;
            for (size_t i = 0; i < len; i++) out[o++] = out[t_rel++];
            break;
        }
    }
    if (o != t_size || crc32(out, o) != le32(patch + n - 8)) return -1;
    return 0;
}
