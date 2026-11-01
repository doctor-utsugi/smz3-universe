/* Generation entry point for C++ frontends: everything the menu needs in plain C types. */
#ifndef SMZ3_HOST_H
#define SMZ3_HOST_H

#include <stddef.h>
#include <stdint.h>
#include "smz3_ui.h"

#define SMZ_HOST_ROM_SIZE 0x600000

typedef const uint8_t *(*SmzHostIps)(void *user, const char *name, size_t *len);

/* Names of the sprites chosen in the menu (empty for the default ones). */
void smz_host_sprites(const SmzUi *u, char link[32], char samus[32]);

/* The plando file chosen in the menu ("" for none). The host reads it and hands its text to smz_host_set_plando before generating (NULL clears);
 * the plando then replaces the generated seed and the settings it carries. Returns 0 or -1 with the reason in err. */
void smz_host_plando_name(const SmzUi *u, char name[32]);
int smz_host_set_plando(const char *yaml, size_t len, char *err, size_t errcap);

/* Generates the run chosen in the menu. rom_out takes SMZ_HOST_ROM_SIZE bytes; settings_json receives the run's
 * settings and seed_text its seed. Returns 0 or a negative generator error. */
int smz_host_generate(const SmzUi *u, int32_t seed_if_random, const uint8_t *sm, const uint8_t *z3,
                      const uint8_t *link_rdc, size_t link_len, const uint8_t *samus_rdc, size_t samus_len,
                      SmzHostIps ips, void *ips_user, uint8_t *rom_out, char *settings_json, size_t json_cap,
                      char *seed_text, size_t seed_cap, char *hints_out);

/* hints_out (or NULL) receives the SMZ_HOST_HELP_COUNT hints a player may ask for, SMZ_HOST_HELP_TEXT bytes each; only
 * filled when the run's help mode is not "none". */
#define SMZ_HOST_HELP_COUNT 15
#define SMZ_HOST_HELP_TEXT 320

#endif
