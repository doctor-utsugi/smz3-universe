/* Data read from the user's own base ROMs at setup (nothing of the games is stored in the repository): the Super Metroid room
 * table the map overlay uses, and the standing Link and Samus drawn as the "Original" sprite previews. Both ROMs are headerless. */
#ifndef SMZ3_ROMDERIVED_H
#define SMZ3_ROMDERIVED_H

#include <stddef.h>
#include <stdint.h>

#define SMZ_SM_ROOMS_MAX 512

/* Walks the room headers from the landing site: out gets 6 ints per room (pointer, area, map x, map y, width, height); returns the count. */
int smz_sm_rooms(const uint8_t *sm, size_t sm_len, int *out, int max_rooms);

/* Original.pv pictures (u8 width, u8 height, RGBA). The buffers are 2 + 4*w*h bytes: link 16x24, samus 32x53. Returns 0 when the ROMs are too short. */
#define SMZ_PV_LINK_LEN (2 + 4 * 16 * 24)
#define SMZ_PV_SAMUS_LEN (2 + 4 * 32 * 53)
int smz_original_previews(const uint8_t *sm, size_t sm_len, const uint8_t *z3, size_t z3_len, uint8_t *link_pv, uint8_t *samus_pv);

#endif
