/* Extra settings for the test programs: key=value arguments after the positional ones. */
#ifndef TEST_ARGS_H
#define TEST_ARGS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_itemopts.h"

/* mode=0|1|2, spazers=N, required=N, opt:<group name>=<option index> */
static void apply_extra_args(SmzSettings *s, int argc, char **argv, int from)
{
    for (int i = from; i < argc; i++) {
        const char *a = argv[i], *eq = strchr(a, '=');
        if (!eq) continue;
        int v = atoi(eq + 1);
        if (!strncmp(a, "mode=", 5)) s->game_mode = v;
        else if (!strncmp(a, "spazers=", 8)) s->spazers_in_pool = v;
        else if (!strncmp(a, "required=", 9)) s->spazers_required = v;
        else if (!strncmp(a, "opt:", 4)) {
            char name[64];
            snprintf(name, sizeof name, "%.*s", (int)(eq - a - 4), a + 4);
            if (smz_itemopt_select(s, name, v)) fprintf(stderr, "bad option %s=%d\n", name, v);
        }
    }
}

#endif
