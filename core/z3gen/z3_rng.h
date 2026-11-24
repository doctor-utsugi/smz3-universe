/* Seeded PRNG for the A Link to the Past generator: PCG32 (XSH RR, 64-bit state, O'Neill 2014).
   alttpr draws from PHP's random_int, a CSPRNG, so no seed can match alttpr.com; any good seeded generator does.
   PCG32 is small (16 bytes), integer-only (same stream on every host), cheap on the 3DS's ARM11, and has published
   reference outputs. The seed number is spread over state and stream by splitmix64. */
#ifndef Z3_RNG_H
#define Z3_RNG_H
#include <stdint.h>

typedef struct { uint64_t state, inc; } Z3Rng;

void z3_rng_init(Z3Rng *r, uint64_t seed);
void z3_rng_init_pcg(Z3Rng *r, uint64_t initstate, uint64_t initseq);   /* pcg32_srandom_r, for the reference vectors */
uint32_t z3_rng_next(Z3Rng *r);                                          /* pcg32_random_r */
uint32_t z3_rng_below(Z3Rng *r, uint32_t bound);                         /* [0, bound), unbiased */
int32_t z3_rng_int(Z3Rng *r, int32_t min, int32_t max);                  /* get_random_int(min, max): [min, max] */
void z3_rng_shuffle(Z3Rng *r, int *a, int n);                            /* fy_shuffle's order of draws */

#endif
