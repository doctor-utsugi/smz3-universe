/* Proof of run: the checks a run cleared, each with the play time it was first seen. The client records it while playing and
 * sends it with the finish; the server replays it against the seed (smz_proof_check) and refuses what cannot have happened. */
#ifndef SMZ3_PROOF_H
#define SMZ3_PROOF_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_PROOF_MAX 512
#define SMZ_PROOF_MIN 12            /* fewest checks a finished run can show */

typedef struct {
    int n;
    int loc[SMZ_PROOF_MAX];         /* index over smz3_locations */
    int64_t ms[SMZ_PROOF_MAX];      /* play time of the first reading that showed the check cleared */
    uint8_t seen[SMZ_PROOF_MAX];
} SmzProof;

/* Text form, one "loc ms" line per check. */
void smz_proof_parse(SmzProof *p, const char *text);
size_t smz_proof_format(const SmzProof *p, char *out, size_t cap);
/* Adds the checks of cleared[] that the proof does not hold yet, stamped now_ms. Returns how many were added. */
int smz_proof_note(SmzProof *p, const uint8_t *cleared, int count, int64_t now_ms);

typedef struct {
    int ok;                         /* 0: reject the finish */
    int suspect;                    /* 1: hold it for review */
    int checks, out_of_logic;
    char reason[160];
} SmzProofVerdict;

/* Checks a proof against the seed of settings_json (stack of at least 32 MB needed). time_ms is the time the client claims. */
void smz_proof_check(const char *settings_json, const SmzProof *p, int64_t time_ms, SmzProofVerdict *v);

#endif
