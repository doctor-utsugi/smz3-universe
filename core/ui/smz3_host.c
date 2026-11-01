#include <stdio.h>
#include <string.h>
#include "smz3_host.h"
#include "smz3_text.h"
#include "smz3_ui_gen.h"

void smz_host_sprites(const SmzUi *u, char link[32], char samus[32])
{
    SmzSettings s;
    SmzPatchCfg pc;
    ui_settings(u, 1, &s, &pc, link, samus);
}

void smz_host_plando_name(const SmzUi *u, char name[32]) { ui_plando_name(u, name); }
int smz_host_set_plando(const char *yaml, size_t len, char *err, size_t errcap) { return ui_set_plando(yaml, len, err, errcap); }

int smz_host_generate(const SmzUi *u, int32_t seed_if_random, const uint8_t *sm, const uint8_t *z3,
                      const uint8_t *link_rdc, size_t link_len, const uint8_t *samus_rdc, size_t samus_len,
                      SmzHostIps ips, void *ips_user, uint8_t *rom_out, char *settings_json, size_t json_cap,
                      char *seed_text, size_t seed_cap, char *hints_out)
{
    static SmzSeed seed;
    SmzSettings s;
    SmzPatchCfg cfg;
    char link[32], samus[32];
    if (ui_settings_json(u, seed_if_random, settings_json, json_cap) < 0) return -8;
    ui_settings(u, seed_if_random, &s, &cfg, link, samus);
    cfg.link_rdc = link_rdc; cfg.link_rdc_len = link_len;
    cfg.samus_rdc = samus_rdc; cfg.samus_rdc_len = samus_len;
    snprintf(seed_text, seed_cap, "%d", (int)s.seed);
    int rc = ui_plando() ? smz_generate_plando(rom_out, &seed, sm, z3, ui_plando(), &cfg, ips, ips_user, NULL, NULL)
                         : smz_generate(rom_out, &seed, sm, z3, &s, &cfg, ips, ips_user, NULL, NULL);
    if (rc == 0 && hints_out && u->value[UIO_HELP] != 2) smz_help_texts(&seed, &cfg, (char (*)[SMZ_HELP_TEXT])hints_out);
    return rc;
}
