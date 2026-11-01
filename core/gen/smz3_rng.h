/* .NET System.Random(seed), bit for bit, so seeds can match the C# generator. */
#ifndef SMZ3_RNG_H
#define SMZ3_RNG_H
#include <stdint.h>

typedef struct { int32_t seed_array[56]; int inext, inextp; } SmzRng;

void rng_init(SmzRng *r, int32_t seed);
int32_t rng_next(SmzRng *r);               /* Next() */
int32_t rng_next_max(SmzRng *r, int32_t max);   /* Next(max) */
int32_t rng_next_range(SmzRng *r, int32_t min, int32_t max);   /* Next(min, max) */
void rng_sanitize(SmzRng *r);              /* skips the first ten results, like RandomExtensions.Sanitize */

#endif
