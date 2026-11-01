#include "smz3_rng.h"

#define MBIG 2147483647
#define MSEED 161803398

void rng_init(SmzRng *r, int32_t seed)
{
    int32_t subtraction = seed == INT32_MIN ? MBIG : (seed < 0 ? -seed : seed);
    int32_t mj = MSEED - subtraction;
    r->seed_array[55] = mj;
    int32_t mk = 1;
    int ii = 0;
    for (int i = 1; i < 55; i++) {
        if ((ii += 21) >= 55) ii -= 55;
        r->seed_array[ii] = mk;
        mk = mj - mk;
        if (mk < 0) mk += MBIG;
        mj = r->seed_array[ii];
    }
    for (int k = 1; k < 5; k++) {
        for (int i = 1; i < 56; i++) {
            int n = i + 30;
            if (n >= 55) n -= 55;
            r->seed_array[i] = (int32_t)((uint32_t)r->seed_array[i] - (uint32_t)r->seed_array[1 + n]);   /* wraps like .NET */
            if (r->seed_array[i] < 0) r->seed_array[i] += MBIG;
        }
    }
    r->inext = 0;
    r->inextp = 21;
}

static int32_t internal_sample(SmzRng *r)
{
    int a = r->inext, b = r->inextp;
    if (++a >= 56) a = 1;
    if (++b >= 56) b = 1;
    int32_t v = r->seed_array[a] - r->seed_array[b];
    if (v == MBIG) v--;
    if (v < 0) v += MBIG;
    r->seed_array[a] = v;
    r->inext = a;
    r->inextp = b;
    return v;
}

int32_t rng_next(SmzRng *r) { return internal_sample(r); }

static double sample(SmzRng *r) { return internal_sample(r) * (1.0 / MBIG); }

int32_t rng_next_max(SmzRng *r, int32_t max) { return (int32_t)(sample(r) * max); }

int32_t rng_next_range(SmzRng *r, int32_t min, int32_t max)
{
    int64_t range = (int64_t)max - min;
    if (range <= MBIG) return (int32_t)(sample(r) * range) + min;
    return min;   /* the large range path is never used by the generator */
}

void rng_sanitize(SmzRng *r)
{
    for (int i = 0; i < 10; i++) internal_sample(r);
}
