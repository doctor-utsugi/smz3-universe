#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_proof.h"
#include "smz3_runlogic.h"
#include "smz3_itemnames.h"
#include "smz3_state.h"

void smz_proof_parse(SmzProof *p, const char *text)
{
    memset(p, 0, sizeof *p);
    for (const char *s = text; s && *s && p->n < SMZ_PROOF_MAX;) {
        char *end, *end2;
        long loc = strtol(s, &end, 10);
        if (end == s) break;
        long long ms = strtoll(end, &end2, 10);
        if (end2 == end) break;
        s = end2;
        p->loc[p->n] = (int)loc;
        p->ms[p->n++] = ms;
        while (*s == '\n' || *s == '\r' || *s == ' ') s++;
    }
}

size_t smz_proof_format(const SmzProof *p, char *out, size_t cap)
{
    size_t n = 0;
    for (int i = 0; i < p->n; i++) {
        int w = snprintf(out + n, cap - n, "%d %lld\n", p->loc[i], (long long)p->ms[i]);
        if (w < 0 || (size_t)w >= cap - n) break;
        n += (size_t)w;
    }
    return n;
}

int smz_proof_note(SmzProof *p, const uint8_t *cleared, int count, int64_t now_ms)
{
    int added = 0;
    if (count > SMZ_PROOF_MAX) count = SMZ_PROOF_MAX;
    for (int i = 0; i < p->n; i++) if (p->loc[i] >= 0 && p->loc[i] < SMZ_PROOF_MAX) p->seen[p->loc[i]] = 1;
    for (int i = 0; i < count && p->n < SMZ_PROOF_MAX; i++)
        if (cleared[i] && !p->seen[i]) { p->seen[i] = 1; p->loc[p->n] = i; p->ms[p->n++] = now_ms; added++; }
    return added;
}

static void reject(SmzProofVerdict *v, const char *msg) { v->ok = 0; snprintf(v->reason, sizeof v->reason, "%s", msg); }

static int location_id(int j)
{
    for (int q = 0; q < SMZ_LOC_NAME_COUNT; q++)
        if (!strcmp(SMZ_LOC_NAMES[q].name, smz3_locations[j].id)) return SMZ_LOC_NAMES[q].id;
    return -1;
}

void smz_proof_check(const char *settings_json, const SmzProof *p, int64_t time_ms, SmzProofVerdict *v)
{
    memset(v, 0, sizeof *v);
    v->checks = p->n;
    if (p->n < (strstr(settings_json, "\"challenge\":true") ? 1 : SMZ_PROOF_MIN)) return reject(v, "too few checks");   /* a daily challenge may need only a few */
    uint8_t used[SMZ_PROOF_MAX] = {0};
    int64_t last = 0;
    for (int i = 0; i < p->n; i++) {
        if (p->loc[i] < 0 || p->loc[i] >= (int)smz3_location_count || p->loc[i] >= SMZ_PROOF_MAX) return reject(v, "unknown check");
        if (used[p->loc[i]]++) return reject(v, "check repeated");
        if (p->ms[i] < last) return reject(v, "checks out of time order");
        last = p->ms[i];
    }
    if (last > time_ms + 2000) return reject(v, "check after the finish");
    if (time_ms - last > 20 * 60 * 1000 && time_ms - last > time_ms / 4) return reject(v, "run ends long after its last check");

    v->ok = 1;
    if (strstr(settings_json, "\"nologic\"")) return;
    static SmzLogic logic;
    if (smz_logic_build(&logic, settings_json) != 0) return reject(v, "seed cannot be rebuilt");
    int items[1100], ni = 0;
    uint8_t in_logic[512];
    int dirty = 1;
    for (int i = 0; i < p->n; i++) {
        if (dirty) { smz_logic_reachable(&logic, items, ni, in_logic, 512); dirty = 0; }
        if (!in_logic[p->loc[i]]) v->out_of_logic++;
        int id = location_id(p->loc[i]);
        int it = id >= 0 && id < 512 ? logic.seed.world.item_at[id] : -1;
        if (it > 0 && ni < 1100) { items[ni++] = it; dirty = 1; }
    }
    if (v->out_of_logic * 10 > p->n) {
        v->suspect = 1;
        snprintf(v->reason, sizeof v->reason, "%d of %d checks out of logic", v->out_of_logic, p->n);
    } else if (p->ms[0] < 15000) {
        v->suspect = 1;
        snprintf(v->reason, sizeof v->reason, "first check after %lld ms", (long long)p->ms[0]);
    } else if (time_ms / p->n < 6000) {
        v->suspect = 1;
        snprintf(v->reason, sizeof v->reason, "%d checks in %lld s", p->n, (long long)(time_ms / 1000));
    }
}
