/* Seeded PRNG for the A Link to the Past generator: PCG32 (XSH RR, 64-bit state, O'Neill 2014).
   alttpr draws from PHP's random_int, a CSPRNG, so no seed can match alttpr.com; any good seeded generator does.
   PCG32 is small (16 bytes), integer-only (same stream on every host), cheap on the 3DS's ARM11, and has published
   reference outputs. The seed number is spread over state and stream by splitmix64. */
#ifndef Z3_RNG_H
#define Z3_RNG_H
#include <stdint.h>

/* PHP 8.1's mt_rand state (MT19937), for oracle runs: the container seeds get_random_int with mt_srand. */
typedef struct { uint32_t s[624]; int left, next; } Z3Mt;
typedef struct { uint64_t state, inc; Z3Mt *mt; } Z3Rng;   /* mt NULL: PCG32 */

void z3_rng_init(Z3Rng *r, uint64_t seed);
/* mt_srand(seed, MT_RAND_MT19937): z3_rng_int then returns what the oracle's mt_rand(min, max) does. */
void z3_rng_init_mt(Z3Rng *r, Z3Mt *mt, uint32_t seed);
void z3_rng_init_pcg(Z3Rng *r, uint64_t initstate, uint64_t initseq);   /* pcg32_srandom_r, for the reference vectors */
uint32_t z3_rng_next(Z3Rng *r);                                          /* pcg32_random_r */
uint32_t z3_rng_below(Z3Rng *r, uint32_t bound);                         /* [0, bound), unbiased */
int32_t z3_rng_int(Z3Rng *r, int32_t min, int32_t max);                  /* get_random_int(min, max): [min, max] */
void z3_rng_shuffle(Z3Rng *r, int *a, int n);                            /* fy_shuffle's order of draws */

#endif
