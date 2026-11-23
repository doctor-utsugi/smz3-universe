/* World goal icons cut from the player's own ROMs at setup: enemies the tracker's sprite set has no picture of. Each icon is
 * a sprite preview (u8 width, u8 height, then width * height RGBA bytes) named by the goal's sprite key with '/' as '_'
 * ("metroid_pirate"), which the hosts keep as goals/<name>.pv in the data folder. Nothing of the ROMs is stored in the repository. */
#ifndef SMZ3_GOALICONS_H
#define SMZ3_GOALICONS_H

#include <stddef.h>
#include <stdint.h>

/* An icon every set makes, renamed when the set changes: the hosts cut the icons again while it is missing. */
#define SMZ_GOAL_ICONS_MARK "metroid_pirate"

/* Calls out once per icon it could make; returns how many. */
int smz_goal_icons(const uint8_t *sm, size_t sm_len, const uint8_t *z3, size_t z3_len,
                   void (*out)(const char *name, const uint8_t *pv, size_t n, void *ctx), void *ctx);

#endif
