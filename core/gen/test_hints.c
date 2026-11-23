/* Prints the hint tiles of a seed in the format of the C# oracle (gen-cli placement, "hints" section). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_hints.h"
#include "test_args.h"
#include "smz3_text.h"
#include "smz3_texts_data.h"
#include "smz3_hints_data.h"

static const char *TYPES[] = { "Location", "LocationGroup", "Room", "Region", "Dungeon", "Requirement" };
static const char *USES[] = { "Mandatory", "Goal", "Key", "Sword", "NiceToHave", "Useless" };

int main(int argc, char **argv)
{
    SmzSettings s;
    smz_settings_defaults(&s);
    s.seed = argc > 1 ? (int32_t)atol(argv[1]) : 12345;
    if (argc > 2) s.keysanity = atoi(argv[2]);
    if (argc > 3) s.placement_rule = atoi(argv[3]);
    if (argc > 4) s.ganon_crystals = atoi(argv[4]);
    if (argc > 5) s.gt_crystals = atoi(argv[5]);
    if (argc > 6) s.tourian_bosses = atoi(argv[6]);
    apply_extra_args(&s, argc, argv, 7);
    int with_text = argc > 7 && !strchr(argv[7], '=');   /* a plain 7th argument asks for the texts too */
    static SmzSeed seed;
    if (smz_fill(&seed, &s)) { fprintf(stderr, "fill failed\n"); return 1; }
    if (!smz_playthrough_ok(&seed, &s)) { fprintf(stderr, "playthrough failed\n"); return 1; }
    SmzPatchCfg cfg;
    smz_patch_cfg_defaults(&cfg, &s);
    cfg.hint_seed = smz_hint_seed(&seed);
    static SmzHints hints;
    smz_hints_generate(&seed, &cfg, cfg.hint_seed, 15, &hints);
    for (int i = 0; i < hints.n; i++) {
        const SmzHint *h = &hints.h[i];
        printf("H\t%s\t%s\t", SMZ_HINT_TILES[h->tile], TYPES[h->type]);
        if (h->type == HINT_Location) printf("%d", h->loc);
        else if (h->type == HINT_Dungeon) for (int k = 0; k < SMZ_HINT_DUNGEONS[h->def].nloc; k++) printf(k ? ",%d" : "%d", SMZ_HINT_DUNGEONS[h->def].locs[k]);
        else if (h->type != HINT_Requirement) for (int k = 0; k < SMZ_HINT_LOCATIONS[h->def].nloc; k++) printf(k ? ",%d" : "%d", SMZ_HINT_LOCATIONS[h->def].locs[k]);
        printf("\t%s\t", h->usefulness < 0 ? "-" : USES[h->usefulness]);
        if (h->medallion >= 0) printf("%d", h->medallion); else printf("-");
        printf("\n");
        if (with_text) { char t[512]; smz_hint_text(&seed, h, 1, t, sizeof t); printf("  %s\n", t); }
    }
    if (with_text) {
        static char help[SMZ_HELP_COUNT][SMZ_HELP_TEXT];
        smz_help_texts(&seed, &cfg, help);
        for (int i = 0; i < SMZ_HELP_COUNT; i++) printf("help %d: %s\n", i, help[i]);
    }
    return 0;
}
