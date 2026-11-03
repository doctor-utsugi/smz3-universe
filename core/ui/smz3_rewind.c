#include "smz3_rewind.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

/* A packed delta is a list of (zero run, literal run, literal bytes) with both runs as varints; applying one XORs the
 * literals into a state, which turns either neighbour into the other. Deltas live back to back in one byte arena that
 * wraps, so pushing allocates nothing. */
typedef struct { uint32_t off, len; } Entry;

struct SmzRewind {
    size_t size, cap;
    uint8_t *arena, *head, *cur, *scratch;
    size_t scratch_cap, wpos;
    Entry *ent;   /* ent[i] turns state i into state i + 1 (and back); ring of max_states - 1 */
    int max, first, n, cursor;   /* n states: n - 1 deltas from ent[first] */
};

SmzRewind *smz_rewind_new(size_t state_size, size_t budget, int max_states) {
    if (!state_size || max_states < 2) return NULL;
    SmzRewind *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->size = state_size;
    r->cap = budget;
    r->max = max_states;
    r->scratch_cap = state_size + 16;
    r->arena = malloc(budget);
    r->head = malloc(state_size);
    r->cur = malloc(state_size);
    r->scratch = malloc(r->scratch_cap);
    r->ent = malloc(sizeof(Entry) * (size_t)max_states);
    if (!r->arena || !r->head || !r->cur || !r->scratch || !r->ent) { smz_rewind_free(r); return NULL; }
    return r;
}

void smz_rewind_free(SmzRewind *r) {
    if (!r) return;
    free(r->arena); free(r->head); free(r->cur); free(r->scratch); free(r->ent);
    free(r);
}

void smz_rewind_clear(SmzRewind *r) { r->n = r->first = r->cursor = 0; r->wpos = 0; }

static uint8_t *put_varint(uint8_t *p, size_t v) {
    while (v >= 0x80) { *p++ = (uint8_t)(v | 0x80); v >>= 7; }
    *p++ = (uint8_t)v;
    return p;
}

static const uint8_t *get_varint(const uint8_t *p, size_t *v) {
    size_t x = 0;
    int s = 0;
    while (*p & 0x80) { x |= (size_t)(*p++ & 0x7F) << s; s += 7; }
    *v = x | (size_t)*p++ << s;
    return p;
}

/* Packs a ^ b into the scratch buffer, word by word; returns its length. A literal run that would overflow the
 * buffer falls back to one literal run over the whole state. */
static size_t pack(SmzRewind *r, const uint8_t *a, const uint8_t *b) {
    size_t n = r->size, i = 0;
    uint8_t *o = r->scratch, *end = r->scratch + r->scratch_cap - 24;
    while (i < n) {
        size_t z = i;
        while (z + 8 <= n) { uint64_t x, y; memcpy(&x, a + z, 8); memcpy(&y, b + z, 8); if (x != y) break; z += 8; }
        while (z < n && a[z] == b[z]) z++;
        if (z == n) break;
        size_t l = z;   /* the literal ends at the first 8 equal bytes, so short matches stay inside it */
        while (l < n) {
            if (l + 8 <= n) { uint64_t x, y; memcpy(&x, a + l, 8); memcpy(&y, b + l, 8); if (x == y) break; }
            else if (a[l] == b[l]) break;
            l++;
        }
        if (o + (l - z) >= end) goto whole;
        o = put_varint(o, z - i);
        o = put_varint(o, l - z);
        for (size_t k = z; k < l; k++) *o++ = a[k] ^ b[k];
        i = l;
    }
    return (size_t)(o - r->scratch);
whole:
    o = put_varint(r->scratch, 0);
    o = put_varint(o, n);
    for (size_t k = 0; k < n; k++) *o++ = a[k] ^ b[k];
    return (size_t)(o - r->scratch);
}

static void apply(uint8_t *s, size_t size, const uint8_t *d, size_t len) {
    const uint8_t *e = d + len;
    size_t i = 0;
    while (d < e) {
        size_t z, l;
        d = get_varint(d, &z);
        d = get_varint(d, &l);
        i += z;
        if (i + l > size) return;
        for (size_t k = 0; k < l; k++) s[i + k] ^= d[k];
        d += l;
        i += l;
    }
}

static Entry *entry(SmzRewind *r, int i) { return &r->ent[(r->first + i) % (r->max - 1)]; }

static void drop_oldest(SmzRewind *r) {
    if (r->n < 2) return;
    r->first = (r->first + 1) % (r->max - 1);
    r->n--;
    if (r->cursor > 0) r->cursor--;
}

/* Room for len bytes at the write position, wrapping to the start and dropping the deltas it would overwrite. */
static int reserve(SmzRewind *r, size_t len) {
    if (len > r->cap) return 0;
    if (r->wpos + len > r->cap) {   /* the deltas in the tail are the oldest: they go first, then the ones at the start */
        size_t old = r->wpos;
        r->wpos = 0;
        while (r->n > 1 && entry(r, 0)->off >= old) drop_oldest(r);
    }
    while (r->n > 1) {
        Entry *o = entry(r, 0);
        int overlap = o->off < r->wpos + len && r->wpos < o->off + o->len;
        if (!overlap) break;
        drop_oldest(r);
    }
    return 1;
}

void smz_rewind_push(SmzRewind *r, const void *state) {
    if (r->n == 0) { memcpy(r->head, state, r->size); r->n = 1; r->wpos = 0; return; }
    size_t len = pack(r, r->head, state);
    if (r->n >= r->max) drop_oldest(r);
    if (!reserve(r, len)) { smz_rewind_clear(r); memcpy(r->head, state, r->size); r->n = 1; return; }
    memcpy(r->arena + r->wpos, r->scratch, len);
    Entry *e = entry(r, r->n - 1);
    e->off = (uint32_t)r->wpos;
    e->len = (uint32_t)len;
    r->wpos += len;
    r->n++;
    memcpy(r->head, state, r->size);
}

int smz_rewind_count(const SmzRewind *r) { return r->n; }

int smz_rewind_open(SmzRewind *r) {
    if (!r->n) return -1;
    memcpy(r->cur, r->head, r->size);
    return r->cursor = r->n - 1;
}

int smz_rewind_step(SmzRewind *r, int delta) {
    while (delta < 0 && r->cursor > 0) { Entry *e = entry(r, r->cursor - 1); apply(r->cur, r->size, r->arena + e->off, e->len); r->cursor--; delta++; }
    while (delta > 0 && r->cursor < r->n - 1) { Entry *e = entry(r, r->cursor); apply(r->cur, r->size, r->arena + e->off, e->len); r->cursor++; delta--; }
    return r->cursor;
}

int smz_rewind_cursor(const SmzRewind *r) { return r->cursor; }
const void *smz_rewind_state(const SmzRewind *r) { return r->cur; }

void smz_rewind_cut(SmzRewind *r) {
    if (!r->n) return;
    if (r->cursor < r->n - 1) {
        r->n = r->cursor + 1;
        r->wpos = r->n > 1 ? entry(r, r->n - 2)->off + entry(r, r->n - 2)->len : 0;
    }
    memcpy(r->head, r->cur, r->size);
}

/* Sounds: sines with a soft attack and an exponential tail, a glide for the sweep and a filtered noise burst for the catch. */
static void tone(float *o, int cap, int rate, float at_ms, float hz0, float hz1, float ms, float vol, float decay)
{
    int start = (int)(rate * at_ms / 1000), n = (int)(rate * ms / 1000);
    float ph = 0;
    for (int i = 0; i < n && start + i < cap; i++) {
        float t = (float)i / rate, f = hz0 + (hz1 - hz0) * i / n;
        float env = (i < rate / 250 ? (float)i * 250 / rate : 1) * expf(-t * decay / (ms / 1000));
        ph += 2 * 3.14159265f * f / rate;
        o[start + i] += vol * env * (sinf(ph) + 0.2f * sinf(2 * ph));
    }
}

static void catch_noise(float *o, int cap, int rate, float ms, float vol)
{
    int n = (int)(rate * ms / 1000);
    uint32_t x = 0x9E3779B9u;
    float lp = 0;
    for (int i = 0; i < n && i < cap; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        float w = (float)(int32_t)x / 2147483648.0f;
        lp += (w - lp) * 0.18f;
        o[i] += vol * lp * expf(-(float)i / n * 6);
    }
}

int smz_rewind_sound(int kind, int rate, int16_t *out, int cap)
{
    int len = 0;
    float *m = calloc((size_t)cap, sizeof *m);
    if (!m) return 0;
    switch (kind) {
    case SMZ_RW_SFX_OPEN:
        catch_noise(m, cap, rate, 35, 0.35f);
        tone(m, cap, rate, 0, 140, 90, 70, 0.22f, 4);
        tone(m, cap, rate, 30, 880, 330, 220, 0.09f, 3);
        len = rate * 260 / 1000;
        break;
    case SMZ_RW_SFX_TICK_BACK:
    case SMZ_RW_SFX_TICK_FWD:
        tone(m, cap, rate, 0, kind == SMZ_RW_SFX_TICK_BACK ? 1175 : 1568, kind == SMZ_RW_SFX_TICK_BACK ? 1100 : 1660, 30, 0.06f, 5);
        len = rate * 30 / 1000;
        break;
    case SMZ_RW_SFX_PLAY:
        tone(m, cap, rate, 0, 587, 587, 160, 0.10f, 4);
        tone(m, cap, rate, 60, 880, 880, 260, 0.09f, 4);
        tone(m, cap, rate, 60, 1175, 1175, 260, 0.04f, 5);
        len = rate * 320 / 1000;
        break;
    case SMZ_RW_SFX_CANCEL:
        tone(m, cap, rate, 0, 784, 523, 120, 0.09f, 4);
        len = rate * 120 / 1000;
        break;
    }
    if (len > cap) len = cap;
    for (int i = 0; i < len; i++) { float v = m[i] < -1 ? -1 : m[i] > 1 ? 1 : m[i]; out[i] = (int16_t)(v * 32767); }
    free(m);
    return len;
}
