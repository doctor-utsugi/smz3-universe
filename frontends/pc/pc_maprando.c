/* Map Rando on the PC: the crate (core/maprando) behind smz_sm_set_maprando, with the data that tools/maprando/fetch.sh packs. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "pc_maprando.h"
#include "pc_store.h"
#include "smz3_sm.h"

#ifdef SMZ_MAPRANDO
#include "smz3_maprando.h"

static char g_dir[1100];
static volatile int g_phase;   /* 0 idle, 1 loading the data, 2 building the seed */

static void error_json(char *info, size_t cap, const char *why)
{
    char clean[300];
    snprintf(clean, sizeof clean, "%s", why);
    for (char *c = clean; *c; c++) if (*c == '"' || *c == '\\' || *c < ' ') *c = '\'';
    snprintf(info, cap, "{\"error\":\"%s\"}", clean);
}

static int generate(const char *settings, uint64_t seed, const uint8_t *sm, size_t sm_len, uint8_t *out, size_t cap,
                    char *info, size_t info_cap)
{
    char err[300];
    g_phase = 1;
    if (smz_mr_init(g_dir, err, sizeof err)) { g_phase = 0; error_json(info, info_cap, err); return -1; }
    g_phase = 2;
    int rc = smz_mr_generate(settings, seed, 0, sm, sm_len, out, cap, info, info_cap);
    smz_mr_free();   /* about 600 MB the game does not need */
    g_phase = 0;
    return rc;
}

void maprando_setup(void)
{
    /* the AppImage's share folder, a zip or tarball beside the program, then build/maprando-data from build/pc */
    static const char *const ROOTS[] = {"%s/../share/smz3/maprando-data", "%s/maprando-data", "%s/../maprando-data"};
    for (unsigned r = 0; r < sizeof ROOTS / sizeof ROOTS[0]; r++) {
        char probe[1200];
        snprintf(g_dir, sizeof g_dir, ROOTS[r], store_exe_dir());
        snprintf(probe, sizeof probe, "%s/rust/data/presets", g_dir);
        if (access(probe, F_OK) == 0) { smz_sm_set_maprando(generate, SMZ_MR_READY); return; }
    }
}

int maprando_progress(char *out, size_t cap)
{
    if (g_phase == 1) snprintf(out, cap, "Loading Map Rando...");
    else if (g_phase == 2) snprintf(out, cap, "Generating Map Rando seed... try %u", (unsigned)smz_mr_attempts());
    return g_phase;
}
#else
void maprando_setup(void) {}
int maprando_progress(char *out, size_t cap) { (void)out; (void)cap; return 0; }
#endif
