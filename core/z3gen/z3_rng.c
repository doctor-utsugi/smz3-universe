/* PCG32 by Melissa O'Neill (pcg-random.org, Apache 2.0 / MIT), minimal C form. See z3_rng.h for why. */
#include "z3_rng.h"

static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

uint32_t z3_rng_next(Z3Rng *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27);
    uint32_t rot = (uint32_t)(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void z3_rng_init_pcg(Z3Rng *r, uint64_t initstate, uint64_t initseq)
{
    r->mt = 0;
    r->state = 0;
    r->inc = (initseq << 1) | 1;
    z3_rng_next(r);
    r->state += initstate;
    z3_rng_next(r);
}

void z3_rng_init(Z3Rng *r, uint64_t seed)
{
    uint64_t x = seed;
    uint64_t s = splitmix64(&x);
    z3_rng_init_pcg(r, s, splitmix64(&x));
}

/* Rejection on the low threshold (pcg32_boundedrand_r), so every value in range is equally likely. */
uint32_t z3_rng_below(Z3Rng *r, uint32_t bound)
{
    if (bound <= 1) return 0;
    uint32_t threshold = (uint32_t)(-bound) % bound;
    for (;;) {
        uint32_t v = z3_rng_next(r);
        if (v >= threshold) return v % bound;
    }
}

/* ext/standard/mt_rand.c: php_mt_initialize, php_mt_reload (the corrected MT_RAND_MT19937 twist), php_mt_rand. */
#define MT_TWIST(m, u, v) ((m) ^ ((((u) & 0x80000000U) | ((v) & 0x7FFFFFFFU)) >> 1) ^ ((uint32_t)(-(int32_t)((v) & 1U)) & 0x9908B0DFU))

static void mt_reload(Z3Mt *m)
{
    uint32_t *s = m->s;
    int i;
    for (i = 0; i < 624 - 397; i++) s[i] = MT_TWIST(s[i + 397], s[i], s[i + 1]);
    for (; i < 623; i++) s[i] = MT_TWIST(s[i + 397 - 624], s[i], s[i + 1]);
    s[623] = MT_TWIST(s[396], s[623], s[0]);
    m->left = 624;
    m->next = 0;
}

void z3_rng_init_mt(Z3Rng *r, Z3Mt *mt, uint32_t seed)
{
    r->state = r->inc = 0;
    r->mt = mt;
    mt->s[0] = seed;
    for (int i = 1; i < 624; i++) mt->s[i] = 1812433253U * (mt->s[i - 1] ^ (mt->s[i - 1] >> 30)) + (uint32_t)i;
    mt_reload(mt);
}

static uint32_t mt_next(Z3Mt *m)
{
    if (m->left == 0) mt_reload(m);
    --m->left;
    uint32_t s1 = m->s[m->next++];
    s1 ^= s1 >> 11;
    s1 ^= (s1 << 7) & 0x9D2C5680U;
    s1 ^= (s1 << 15) & 0xEFC60000U;
    return s1 ^ (s1 >> 18);
}

/* php_mt_rand_range32: a mask for powers of two, else rejection above the last whole multiple. */
static uint32_t mt_range(Z3Mt *m, uint32_t umax)
{
    uint32_t result = mt_next(m);
    if (umax == UINT32_MAX) return result;
    umax++;
    if ((umax & (umax - 1)) == 0) return result & (umax - 1);
    uint32_t limit = UINT32_MAX - (UINT32_MAX % umax) - 1;
    while (result > limit) result = mt_next(m);
    return result % umax;
}

/* min == max still draws once, as PHP's mt_rand (the oracle's) and random_int do, so draw counts stay aligned. */
int32_t z3_rng_int(Z3Rng *r, int32_t min, int32_t max)
{
    uint32_t span = (uint32_t)max - (uint32_t)min;
    if (r->mt) return (int32_t)((uint32_t)min + mt_range(r->mt, span));
    if (span == UINT32_MAX) return (int32_t)z3_rng_next(r);
    if (span == 0) {
        z3_rng_next(r);
        return min;
    }
    return (int32_t)((uint32_t)min + z3_rng_below(r, span + 1));
}

void z3_rng_shuffle(Z3Rng *r, int *a, int n)
{
    for (int i = n - 1; i >= 0; --i) {
        int j = z3_rng_int(r, 0, i);
        int t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}
