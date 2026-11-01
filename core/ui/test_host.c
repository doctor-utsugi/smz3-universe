/* The menu path (smz_host_generate with default options) must build the same ROM as the plain generator for the same seed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_host.h"
#include "smz3_generate.h"
#include "smz3_plando.h"

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    fread(b, 1, *n, f); fclose(f);
    return b;
}
static const char *dir;
static const uint8_t *ips(void *u, const char *name, size_t *len)
{
    static uint8_t *b; free(b);
    char p[256]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return b = slurp(p, len);
}

int main(int argc, char **argv)
{
    size_t n;
    uint8_t *sm = slurp(argv[1], &n), *z3 = slurp(argv[2], &n);
    dir = argv[3];
    static SmzUi ui;
    ui_init(&ui);
    static uint8_t a[SMZ_HOST_ROM_SIZE], b[SMZ_HOST_ROM_SIZE];
    char json[4096], seedText[16];
    if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, a, json, sizeof json, seedText, sizeof seedText, NULL)) return 2;
    SmzSettings s; smz_settings_defaults(&s); s.seed = 777;
    SmzPatchCfg cfg; smz_patch_cfg_defaults(&cfg, &s);
    cfg.sm_default_controls = 1;   /* what ui_settings sets: Super Metroid starts with shot Y, jump B, dash A, cancel X */
    static SmzSeed seed;
    if (smz_generate(b, &seed, sm, z3, &s, &cfg, ips, NULL, NULL, NULL)) return 3;
    int bad = memcmp(a, b, sizeof a) != 0;
    printf(bad ? "DIFFERENT\n" : "host path identical\n");
    /* the jingle row: choosing jingle 3 must give the generator's etecoons_jingle = 3 patch, and differ from the default ROM */
    {
        static uint8_t j[SMZ_HOST_ROM_SIZE];
        ui.value[UIO_JINGLE] = 3;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, j, json, sizeof json, seedText, sizeof seedText, NULL)) return 7;
        cfg.etecoons_jingle = 3;
        smz_patch_cfg_defaults(&cfg, &s); cfg.etecoons_jingle = 3; cfg.sm_default_controls = 1;
        if (smz_generate(b, &seed, sm, z3, &s, &cfg, ips, NULL, NULL, NULL)) return 8;
        int jbad = memcmp(j, b, sizeof j) != 0;
        printf(jbad ? "jingle DIFFERENT\n" : "jingle row reaches the ROM\n");
        if (!jbad && !memcmp(j, a, sizeof j)) { printf("jingle changed nothing\n"); jbad = 1; }
        bad |= jbad;
        ui.value[UIO_JINGLE] = 0;
    }
    {   /* the default controller bindings of the four Super Metroid sites: shot Y, jump B, dash A, cancel X */
        static const uint8_t WANT[24] = {0xA9, 0x00, 0x40, 0x8D, 0xB2, 0x09, 0xA9, 0x00, 0x80, 0x8D, 0xB4, 0x09,
                                         0xA9, 0x80, 0x00, 0x8D, 0xB6, 0x09, 0xA9, 0x40, 0x00, 0x8D, 0xB8, 0x09};
        int cbad = memcmp(a + 0x2F22C, WANT, 24) != 0 || memcmp(a + 0x0BBC4A, WANT, 24) != 0 || memcmp(a + 0x118845, WANT, 24) != 0;
        printf(cbad ? "default controls DIFFERENT\n" : "default controls: shot Y, jump B, dash A, cancel X at the three straight sites\n");
        bad |= cbad;
    }
    {   /* the "Nerfed charge" row: off by default (as on the web), on applies nerfed_charge.ips at $90:B81E */
        static const uint8_t VANILLA[5] = {0x89, 0x00, 0x10, 0xD0, 0x0A}, NERFED[5] = {0x89, 0x00, 0x00, 0x80, 0x0A};
        static uint8_t c[SMZ_HOST_ROM_SIZE];
        const size_t at = 0x10B81E;
        int nbad = memcmp(a + at, VANILLA, 5) != 0;
        ui.value[UIO_NERF] = 1;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, c, json, sizeof json, seedText, sizeof seedText, NULL)) return 9;
        nbad |= memcmp(c + at, NERFED, 5) != 0 || !strstr(json, "\"nerfedCharge\":true");
        printf(nbad ? "nerfed charge row DIFFERENT\n" : "nerfed charge: off by default, on patches the ROM\n");
        bad |= nbad;
        ui.value[UIO_NERF] = 0;
    }
    {   /* "Quick morph" is off by default; on applies UnifiedAim.ips (changes the ROM, not the rules) */
        static uint8_t c[SMZ_HOST_ROM_SIZE];
        int qbad = !strstr(json, "\"quickMorph\":false");
        ui.value[UIO_MORPH] = 1;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, c, json, sizeof json, seedText, sizeof seedText, NULL)) return 9;
        qbad |= !memcmp(c, a, SMZ_HOST_ROM_SIZE) || !strstr(json, "\"quickMorph\":true") || !strstr(json, "\"defaultRules\":true");
        printf(qbad ? "quick morph row DIFFERENT\n" : "quick morph: off by default, on patches the ROM and keeps the default rules\n");
        bad |= qbad;
        ui.value[UIO_MORPH] = 0;
    }
    {   /* "Random music" only records the choice (the MSU-1 links are made by the app); it keeps the default rules */
        static uint8_t c0[SMZ_HOST_ROM_SIZE];
        int mbad = !strstr(json, "\"musicPackage\":\"\"");
        ui.value[UIO_MUSIC] = 1;   /* "Random" */
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, c0, json, sizeof json, seedText, sizeof seedText, NULL)) return 9;
        mbad |= !strstr(json, "\"musicPackage\":\"*\"") || !strstr(json, "\"defaultRules\":true");
        printf(mbad ? "random music row DIFFERENT\n" : "random music: off by default, on is stored and keeps the default rules\n");
        bad |= mbad;
        ui.value[UIO_MUSIC] = 0;
    }
    {   /* "Save states" is off and the rules are the default ones until either changes (feeds the leaderboards) */
        static uint8_t c[SMZ_HOST_ROM_SIZE];
        int sbad = 0;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, c, json, sizeof json, seedText, sizeof seedText, NULL)) return 9;
        sbad |= !strstr(json, "\"saveStates\":false") || !strstr(json, "\"defaultRules\":true");
        ui.value[UIO_SAVES] = 1;
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, c, json, sizeof json, seedText, sizeof seedText, NULL)) return 9;
        sbad |= !strstr(json, "\"saveStates\":true") || !strstr(json, "\"defaultRules\":false");
        ui.value[UIO_SAVES] = 0;
        printf(sbad ? "save states / default rules DIFFERENT\n" : "save states: off by default, flips defaultRules\n");
        bad |= sbad;
    }
    if (argc > 4) {   /* a plando file: the host path with it set must equal smz_generate_plando */
        size_t yl;
        uint8_t *yaml = slurp(argv[4], &yl);
        char err[256];
        if (smz_host_set_plando((const char *)yaml, yl, err, sizeof err)) { printf("plando: %s\n", err); return 4; }
        if (smz_host_generate(&ui, 777, sm, z3, NULL, 0, NULL, 0, ips, NULL, a, json, sizeof json, seedText, sizeof seedText, NULL)) return 5;
        static SmzPlando pl;
        smz_plando_parse(&pl, (const char *)yaml, yl, err, sizeof err);
        smz_plando_settings(&pl, &s);
        smz_patch_cfg_defaults(&cfg, &s);
        cfg.sm_default_controls = 1;   /* the menu path sets it for plando runs too */
        if (smz_generate_plando(b, &seed, sm, z3, &pl, &cfg, ips, NULL, NULL, NULL)) return 6;
        int pbad = memcmp(a, b, sizeof a) != 0;
        printf(pbad ? "plando DIFFERENT\n" : "plando host path identical\n");
        bad |= pbad;
    }
    return bad;
}
