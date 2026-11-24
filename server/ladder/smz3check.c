/* Reads a run's settings JSON (first line) and its proof of run (the rest) on stdin; prints the verdict as one JSON line. A Map
 * Rando run's verdict also lists smBits, the item bit ($7E:D870) of each check in proof order, for its logic check (maprando.js). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_proof.h"
#include "smz3_state.h"

static char *text;
static long long time_ms;
static SmzProof proof;
static SmzProofVerdict verdict;

static void *work(void *arg)
{
    (void)arg;
    char *nl = strchr(text, '\n');
    if (!nl) { verdict.ok = 0; strcpy(verdict.reason, "no proof"); return NULL; }
    *nl = 0;
    smz_proof_parse(&proof, nl + 1);
    smz_proof_check(text, &proof, time_ms, &verdict);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    time_ms = atoll(argv[1]);
    size_t cap = 1 << 20, n = 0, r;
    text = malloc(cap);
    while ((r = fread(text + n, 1, cap - n - 1, stdin)) > 0 && (n += r) < cap - 1) ;
    text[n] = 0;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 128u << 20);
    pthread_t th;
    if (pthread_create(&th, &at, work, NULL)) return 2;
    pthread_join(th, NULL);
    for (char *c = verdict.reason; *c; c++) if (*c == '"' || *c == '\\') *c = ' ';
    printf("{\"ok\":%d,\"suspect\":%d,\"checks\":%d,\"outOfLogic\":%d,\"reason\":\"%s\"", verdict.ok, verdict.suspect, verdict.checks, verdict.out_of_logic, verdict.reason);
    if (verdict.ok && strstr(text, "\"generator\":\"maprando\"")) {
        printf(",\"smBits\":\"");
        for (int i = 0; i < proof.n; i++) {
            const smz3_location *l = &smz3_locations[proof.loc[i]];
            printf("%s%u", i ? " " : "", l->addr * 8 + (unsigned)__builtin_ctz(l->flag));
        }
        printf("\"");
    }
    printf("}\n");
    return 0;
}
