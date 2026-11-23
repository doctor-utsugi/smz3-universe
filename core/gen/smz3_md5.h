/* MD5 of a buffer, used to tell the exact base ROM revisions the patches were written for. */
#ifndef SMZ3_MD5_H
#define SMZ3_MD5_H
#include <stddef.h>
#include <stdint.h>

void smz_md5(const uint8_t *data, size_t len, uint8_t out[16]);
/* Lower-case hex digest into out[33]. */
void smz_md5_hex(const uint8_t *data, size_t len, char out[33]);

/* Revisions the SMZ3 patches are written for (the tracker's SourceRomValidationService): Super Metroid (JU), A Link to the Past
 * Japanese v1.0. Another revision (the USA one, for instance) patches into a ROM whose Zelda-side portal code is misaligned and
 * crashes the moment Samus enters a portal. */
#define SMZ_MD5_SM "21f3e98df4780ee1c667b84e57d88675"
#define SMZ_MD5_Z3 "03a63945398191337e896e5771f77173"

#endif
