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

int32_t z3_rng_int(Z3Rng *r, int32_t min, int32_t max)
{
    uint32_t span = (uint32_t)max - (uint32_t)min;
    if (span == UINT32_MAX) return (int32_t)z3_rng_next(r);
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
