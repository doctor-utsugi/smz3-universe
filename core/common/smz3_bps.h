/* Applies BPS patches (the format Flips writes), for the Map Rando seeds the server builds for the 3DS. */
#ifndef SMZ3_BPS_H
#define SMZ3_BPS_H

#include <stddef.h>
#include <stdint.h>

/* The size the patch's target will have, or 0 if it is no BPS patch. */
size_t smz_bps_target_size(const uint8_t *patch, size_t n);

/* Writes the patched source into out (at least smz_bps_target_size bytes). 0, or -1 when the patch is damaged or the
 * source is not the ROM it was made from (the CRCs disagree). */
int smz_bps_apply(const uint8_t *patch, size_t n, const uint8_t *src, size_t src_n, uint8_t *out, size_t out_cap);

#endif
