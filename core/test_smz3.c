/* Usage: test_smz3 <wram.bin> <sram.bin>. Prints the run summary read from memory dumps. */
#include <stdio.h>
#include <stdlib.h>
#include "smz3_state.h"

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    rewind(f);
    uint8_t *buf = malloc(*len ? *len : 1);
    if (fread(buf, 1, *len, f) != *len) { perror("read"); exit(1); }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s wram.bin sram.bin\n", argv[0]); return 2; }
    size_t wl, sl;
    uint8_t *wram = load(argv[1], &wl), *sram = load(argv[2], &sl);
    smz3_summary s;
    smz3_summarize(wram, wl, sram, sl, &s);
    static const char *names[] = {"none", "zelda", "metroid", "credits"};
    printf("game=%s started=%d zelda=%u/%u metroid=%u/%u\n", names[s.game], s.started,
           s.zelda_cleared, s.zelda_total, s.metroid_cleared, s.metroid_total);
    for (unsigned i = 0; i < smz3_location_count; i++) {
        const smz3_location *l = &smz3_locations[i];
        if (smz3_location_cleared(l, s.game, wram, wl, sram, sl) > 0)
            printf("  cleared: %s / %s\n", l->region, l->name);
    }
    int inv[256];
    smz3_inventory(wram, wl, sram, sl, inv);
    for (unsigned i = 0; i < smz3_inventory_count; i++)
        if (inv[i] > 0) printf("inv %s=%d\n", smz3_inventory_items[i].name, inv[i]);
    return 0;
}
