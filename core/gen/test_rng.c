#include <stdio.h>
#include <stdlib.h>
#include "smz3_rng.h"
int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "build/rng_vectors.txt", "r");
    if (!f) return 2;
    int bad = 0, n = 0;
    char tag[16]; long seed;
    while (fscanf(f, "%15s %ld", tag, &seed) == 2) {
        SmzRng r; rng_init(&r, (int32_t)seed);
        long v;
        for (int i = 0; i < 30; i++) { fscanf(f, "%ld", &v); n++; if (rng_next(&r) != v) bad++; }
        for (int i = 0; i < 30; i++) { fscanf(f, "%ld", &v); n++; if (rng_next_max(&r, 7 + i * 31) != v) bad++; }
        for (int i = 0; i < 10; i++) { fscanf(f, "%ld", &v); n++; if (rng_next_range(&r, 3, 20 + i) != v) bad++; }
    }
    printf("%d values, %d mismatches\n", n, bad);
    return bad != 0;
}
