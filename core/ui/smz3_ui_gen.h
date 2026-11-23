/* The generator-facing half of the menu: turns the chosen options into generation settings. Kept apart from
 * smz3_ui.h so C++ frontends need not include the generator's headers. */
#ifndef SMZ3_UI_GEN_H
#define SMZ3_UI_GEN_H

#include <stddef.h>
#include "smz3_generate.h"
#include "smz3_ui.h"

/* Fills generation settings from the chosen options. seed_if_random is used when the seed option is 0. The sprite names
 * are copied into link/samus (empty for the default sprite). */
void ui_settings(const SmzUi *u, int32_t seed_if_random, SmzSettings *s, SmzPatchCfg *pc, char link[32], char samus[32]);

/* Plando: a parsed config (the YAML text of the file chosen in the Plando row) takes the place of the generated seed and
 * of the settings it carries. NULL text clears it. Returns 0, or -1 with the reason in err. One menu per program. */
struct SmzPlando;
int ui_set_plando(const char *yaml, size_t len, char *err, size_t errcap);
const struct SmzPlando *ui_plando(void);
/* Name of the plando file chosen in the menu, "" for none. */
void ui_plando_name(const SmzUi *u, char name[32]);

#endif
