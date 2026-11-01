/* test_plando <plando.yml> [jingle=N]: parses the plando config, builds the seed and prints the patches like test_patch does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_plando.h"
#include "smz3_patch.h"
#include "smz3_text.h"

static void log_patch(void *user, const char *cls, int off, const uint8_t *d, int n)
{
    (void)user;
    printf("P\t%s\t%X\t", cls, off);
    for (int i = 0; i < n; i++) printf("%02X", d[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("plando"); return 2; }
    static char text[1 << 20];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    static SmzPlando pl;
    char err[512];
    if (smz_plando_parse(&pl, text, n, err, sizeof err)) { fprintf(stderr, "parse: %s\n", err); return 1; }
    static SmzSeed seed;
    if (smz_plando_seed(&seed, &pl, err, sizeof err)) { fprintf(stderr, "seed: %s\n", err); return 1; }
    SmzSettings s;
    smz_plando_settings(&pl, &s);
    SmzPatchCfg cfg;
    smz_patch_cfg_defaults(&cfg, &s);
    cfg.plando = &pl;
    cfg.pyramid_open = pl.open_pyramid;
    cfg.tourian_door_open = pl.skip_tourian_door;
    cfg.hint_tiles = 0;
    for (int i = 2; i < argc; i++) if (!strncmp(argv[i], "jingle=", 7)) cfg.etecoons_jingle = atoi(argv[i] + 7);
    static uint8_t rom[SMZ_ROM_SIZE];
    if (smz_apply_patches(rom, &seed, &s, &cfg, smz_seed_number(pl.seed), log_patch, NULL)) { fprintf(stderr, "patch failed\n"); return 1; }
    return 0;
}
