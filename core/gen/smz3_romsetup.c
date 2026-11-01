#include <stdlib.h>
#include <string.h>
#include "smz3_md5.h"
#include "smz3_romsetup.h"

int smz_rom_classify(const uint8_t *data, size_t n, const uint8_t **start, size_t *len)
{
    if (n % 0x8000 == 0x200) { data += 0x200; n -= 0x200; }
    if (start) *start = data;
    if (len) *len = n;
    if (n != 0x300000 && n != 0x100000) return SMZ_ROM_UNKNOWN;
    char hex[33];
    smz_md5_hex(data, n, hex);
    if (!strcmp(hex, SMZ_MD5_SM)) return SMZ_ROM_SM;
    if (!strcmp(hex, SMZ_MD5_Z3)) return SMZ_ROM_Z3;
    return n == 0x300000 ? SMZ_ROM_SM_OTHER : SMZ_ROM_Z3_OTHER;
}

const char *smz_rom_message(int kind)
{
    switch (kind) {
    case SMZ_ROM_SM: return "Super Metroid (JU) found";
    case SMZ_ROM_Z3: return "A Link to the Past (JP v1.0) found";
    case SMZ_ROM_SM_OTHER: return "that Super Metroid is another revision: the (JU) dump is needed";
    case SMZ_ROM_Z3_OTHER: return "that ALttP is another revision: the Japanese v1.0 dump is needed";
    }
    return "that is not Super Metroid (JU) or ALttP (JP v1.0)";
}

/* ---- inflate (RFC 1951), small and slow enough not to matter for 3 MB ---- */
typedef struct { const uint8_t *in; size_t n, pos; uint32_t bits; int cnt; uint8_t *out; size_t cap, len; int err; } Inf;
typedef struct { uint16_t count[16], sym[288]; } Huff;

static int getbit(Inf *s)
{
    if (!s->cnt) { if (s->pos >= s->n) { s->err = 1; return 0; } s->bits = s->in[s->pos++]; s->cnt = 8; }
    int b = s->bits & 1; s->bits >>= 1; s->cnt--;
    return b;
}
static int getbits(Inf *s, int k) { int v = 0; for (int i = 0; i < k; i++) v |= getbit(s) << i; return v; }

static void build(Huff *h, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
}

static int decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 15; l++) {
        code |= getbit(s);
        int c = h->count[l];
        if (code - c < first) return h->sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
        if (s->err) return -1;
    }
    return -1;
}

static const uint16_t LBASE[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t LEXT[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DBASE[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t DEXT[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int codes(Inf *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) { if (s->len >= s->cap) return -1; s->out[s->len++] = (uint8_t)sym; }
        else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            size_t l = LBASE[sym] + (size_t)getbits(s, LEXT[sym]);
            int d = decode(s, dist);
            if (d < 0 || d >= 30) return -1;
            size_t dist_v = DBASE[d] + (size_t)getbits(s, DEXT[d]);
            if (dist_v > s->len || s->len + l > s->cap) return -1;
            for (size_t i = 0; i < l; i++) { s->out[s->len] = s->out[s->len - dist_v]; s->len++; }
        }
        if (s->err) return -1;
    }
}

static int inflate_all(Inf *s)
{
    int last;
    do {
        last = getbit(s);
        int type = getbits(s, 2);
        if (type == 0) {
            s->cnt = 0;
            if (s->pos + 4 > s->n) return -1;
            size_t len = s->in[s->pos] | (s->in[s->pos + 1] << 8);
            s->pos += 4;
            if (s->pos + len > s->n || s->len + len > s->cap) return -1;
            memcpy(s->out + s->len, s->in + s->pos, len);
            s->pos += len; s->len += len;
        } else if (type == 1) {
            uint8_t l[288];
            Huff lit, dist;
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            build(&lit, l, 288);
            for (int i = 0; i < 30; i++) l[i] = 5;
            build(&dist, l, 30);
            if (codes(s, &lit, &dist)) return -1;
        } else if (type == 2) {
            static const uint8_t ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t l[320] = {0};
            int nlen = getbits(s, 5) + 257, ndist = getbits(s, 5) + 1, ncode = getbits(s, 4) + 4;
            if (nlen > 286 || ndist > 30) return -1;
            for (int i = 0; i < ncode; i++) l[ORDER[i]] = (uint8_t)getbits(s, 3);
            for (int i = ncode; i < 19; i++) l[ORDER[i]] = 0;
            Huff lc, lit, dist;
            build(&lc, l, 19);
            uint8_t ll[320] = {0};
            for (int i = 0; i < nlen + ndist;) {
                int sym = decode(s, &lc);
                if (sym < 0) return -1;
                if (sym < 16) ll[i++] = (uint8_t)sym;
                else {
                    int prev = 0, rep;
                    if (sym == 16) { if (!i) return -1; prev = ll[i - 1]; rep = 3 + getbits(s, 2); }
                    else if (sym == 17) rep = 3 + getbits(s, 3);
                    else rep = 11 + getbits(s, 7);
                    if (i + rep > nlen + ndist) return -1;
                    while (rep--) ll[i++] = (uint8_t)prev;
                }
            }
            build(&lit, ll, nlen);
            build(&dist, ll + nlen, ndist);
            if (codes(s, &lit, &dist)) return -1;
        } else return -1;
        if (s->err) return -1;
    } while (!last);
    return 0;
}

static unsigned u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int smz_is_zip(const uint8_t *d, size_t n) { return n > 30 && d[0] == 'P' && d[1] == 'K' && (d[2] == 3 || d[2] == 5); }

uint8_t *smz_zip_entry(const uint8_t *z, size_t n, int idx, size_t *out_len)
{
    if (n < 22) return NULL;
    size_t eocd = n - 22;
    while (eocd > 0 && u32(z + eocd) != 0x06054b50) { if (n - eocd > 70000) return NULL; eocd--; }
    if (u32(z + eocd) != 0x06054b50) return NULL;
    unsigned count = u16(z + eocd + 10);
    size_t p = u32(z + eocd + 16);
    int seen = 0;
    for (unsigned i = 0; i < count && p + 46 <= n; i++) {
        unsigned method = u16(z + p + 10), nlen = u16(z + p + 28), xlen = u16(z + p + 30), clen = u16(z + p + 32);
        uint32_t csize = u32(z + p + 20), usize = u32(z + p + 24), off = u32(z + p + 42);
        int is_dir = nlen && z[p + 46 + nlen - 1] == '/';
        p += 46 + nlen + xlen + clen;
        if (is_dir) continue;
        if (seen++ != idx) continue;
        if ((size_t)off + 30 > n || usize > (64u << 20)) return NULL;
        size_t data = off + 30 + u16(z + off + 26) + u16(z + off + 28);
        if (data + csize > n) return NULL;
        uint8_t *out = malloc(usize ? usize : 1);
        if (!out) return NULL;
        if (method == 0 && csize == usize) memcpy(out, z + data, usize);
        else if (method == 8) {
            Inf s = {z + data, csize, 0, 0, 0, out, usize, 0, 0};
            if (inflate_all(&s) || s.len != usize) { free(out); return NULL; }
        } else { free(out); return NULL; }
        *out_len = usize;
        return out;
    }
    return NULL;
}
