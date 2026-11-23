/* A proof made by playing the seed in logic order passes; edited proofs are refused or held. */
#include <stdio.h>
#include <string.h>
#include "smz3_proof.h"
#include "smz3_runlogic.h"
#include "smz3_itemnames.h"
#include "smz3_state.h"

static SmzLogic L;
static SmzProof P;
static const char *J = "{\"seed\":4242}";

static int loc_id(int j) { for (int q = 0; q < SMZ_LOC_NAME_COUNT; q++) if (!strcmp(SMZ_LOC_NAMES[q].name, smz3_locations[j].id)) return SMZ_LOC_NAMES[q].id; return -1; }

int main(void)
{
    smz_logic_build(&L, J);
    int items[1100], ni = 0, bad = 0;
    uint8_t in[512], taken[512] = {0};
    memset(&P, 0, sizeof P);
    for (int round = 0; round < 600 && P.n < SMZ_PROOF_MAX; round++) {
        smz_logic_reachable(&L, items, ni, in, 512);
        int got = 0;
        for (int j = 0; j < (int)smz3_location_count && j < 512; j++)
            if (in[j] && !taken[j] && P.n < SMZ_PROOF_MAX) {
                taken[j] = 1; P.loc[P.n] = j; P.ms[P.n] = 60000 + 15000 * P.n; P.n++;
                int id = loc_id(j), it = id >= 0 ? L.seed.world.item_at[id] : 0;
                if (it > 0) items[ni++] = it;
                got = 1; break;
            }
        if (!got) break;
    }
    int64_t total = P.ms[P.n - 1] + 30000;
    SmzProofVerdict v;
    smz_proof_check(J, &P, total, &v);
    printf("honest: ok=%d suspect=%d checks=%d ool=%d %s\n", v.ok, v.suspect, v.checks, v.out_of_logic, v.reason);
    if (!v.ok || v.suspect) bad++;
    SmzProof q = P;
    q.ms[5] = q.ms[4] - 1;
    smz_proof_check(J, &q, total, &v);
    if (v.ok) { puts("time disorder accepted"); bad++; }
    q = P; q.n = 5;
    smz_proof_check(J, &q, total, &v);
    if (v.ok) { puts("short proof accepted"); bad++; }
    q = P; q.ms[q.n - 1] = total + 60000;
    smz_proof_check(J, &q, total, &v);
    if (v.ok) { puts("late check accepted"); bad++; }
    q = P;                                                        /* every check within a second or two: too fast to be played */
    for (int i = 0; i < q.n; i++) q.ms[i] = 20000 + i * 1000;
    smz_proof_check(J, &q, q.ms[q.n - 1] + 5000, &v);
    if (!v.suspect) { puts("hurried run accepted"); bad++; }
    /* the same checks in the reverse order lean on items found later */
    q = P;
    for (int i = 0; i < P.n; i++) { q.loc[i] = P.loc[P.n - 1 - i]; q.ms[i] = P.ms[i]; }
    smz_proof_check(J, &q, total, &v);
    printf("reversed: ok=%d suspect=%d ool=%d\n", v.ok, v.suspect, v.out_of_logic);
    if (!v.suspect) bad++;
    printf(bad ? "proof FAILED\n" : "proof ok\n");
    return bad;
}
