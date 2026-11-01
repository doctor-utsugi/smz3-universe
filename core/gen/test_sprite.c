#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_sprite.h"

int main(int argc, char **argv)
{
    static uint8_t rom[0x600000];
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { perror(argv[i]); return 1; }
        static uint8_t buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        int r = smz_sprite_apply_rdc(rom, sizeof rom, buf, n);
        printf("%s: %d blocks, author '%s'\n", argv[i], r, smz_rdc_author(buf, n));
        if (r < 0) return 1;
    }
    FILE *o = fopen("build/sprite_rom.bin", "wb");
    fwrite(rom, 1, sizeof rom, o);
    fclose(o);
    return 0;
}
