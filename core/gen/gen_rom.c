/* Command line front end: gen_rom <sm.sfc> <z3.sfc> <ips dir> <out.sfc> <seed> [keysanity [rule [ganon gt tourian]]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_generate.h"
#include "smz3_plando.h"
#include "test_args.h"

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static const char *ips_dir;

static const uint8_t *load_ips(void *user, const char *name, size_t *len)
{
    (void)user;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", ips_dir, name);
    return slurp(path, len);
}

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: gen_rom sm z3 ipsdir out seed [keysanity rule ganon gt tourian]\n"); return 2; }
    size_t sml, z3l;
    uint8_t *sm = slurp(argv[1], &sml), *z3 = slurp(argv[2], &z3l);
    if (!sm || !z3) { fprintf(stderr, "cannot read the ROMs\n"); return 1; }
    if (sml == 0x300200) { sm += 0x200; sml -= 0x200; }
    if (z3l == 0x100200) { z3 += 0x200; z3l -= 0x200; }
    ips_dir = argv[3];
    SmzSettings s;
    smz_settings_defaults(&s);
    s.seed = (int32_t)atol(argv[5]);
    if (argc > 6) s.keysanity = atoi(argv[6]);
    if (argc > 7) s.placement_rule = atoi(argv[7]);
    if (argc > 8) s.ganon_crystals = atoi(argv[8]);
    if (argc > 9) s.gt_crystals = atoi(argv[9]);
    if (argc > 10) s.tourian_bosses = atoi(argv[10]);
    apply_extra_args(&s, argc, argv, 11);
    SmzPatchCfg cfg;
    smz_patch_cfg_defaults(&cfg, &s);
    for (int i = 11; i < argc; i++) if (!strncmp(argv[i], "jingle=", 7)) cfg.etecoons_jingle = atoi(argv[i] + 7);
    for (int i = 11; i < argc; i++) if (!strncmp(argv[i], "dark=", 5)) cfg.light_dark_rooms = atoi(argv[i] + 5);
    for (int i = 11; i < argc; i++) if (!strncmp(argv[i], "nerf=", 5)) cfg.nerfed_charge = atoi(argv[i] + 5);
    for (int i = 11; i < argc; i++) if (!strncmp(argv[i], "smctl=", 6)) cfg.sm_default_controls = atoi(argv[i] + 6);
    size_t ll, sl;
    const char *lp = getenv("SMZ_LINK"), *sp = getenv("SMZ_SAMUS");
    if (lp) { cfg.link_rdc = slurp(lp, &ll); cfg.link_rdc_len = ll; }
    if (sp) { cfg.samus_rdc = slurp(sp, &sl); cfg.samus_rdc_len = sl; }
    static SmzSeed seed;
    static uint8_t rom[SMZ_ROM_SIZE];
    int rc;
    const char *pf = getenv("SMZ_PLANDO");   /* a plando YAML: generate from it instead of filling */
    if (pf) {
        size_t pl_len;
        uint8_t *yaml = slurp(pf, &pl_len);
        static SmzPlando pl;
        char err[512];
        if (!yaml || smz_plando_parse(&pl, (const char *)yaml, pl_len, err, sizeof err)) { fprintf(stderr, "plando: %s\n", yaml ? err : "cannot read the file"); return 1; }
        rc = smz_generate_plando(rom, &seed, sm, z3, &pl, &cfg, load_ips, NULL, NULL, NULL);
    } else {
        rc = smz_generate(rom, &seed, sm, z3, &s, &cfg, load_ips, NULL, NULL, NULL);
    }
    if (rc) { fprintf(stderr, "generation failed: %d\n", rc); return 1; }
    FILE *f = fopen(argv[4], "wb");
    fwrite(rom, 1, SMZ_ROM_SIZE, f);
    fclose(f);
    return 0;
}
