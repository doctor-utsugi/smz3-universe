#ifndef PC_ASSETS_H
#define PC_ASSETS_H
#include "smz3_discovery.h"
extern const SmzMapData PC_MAP_DATA;
extern const unsigned char PC_MAP_PNG[];   /* the combined map, 3246 x 1916 (twice the coordinate space of the markers) */
extern const unsigned PC_MAP_PNG_LEN;
typedef struct { const char *name; const unsigned char *data; unsigned len; } PcIcon;
extern const PcIcon PC_ITEM_ICONS[];   /* 32x32 PNGs named like the inventory icons with _ for spaces; NULL name ends */

#endif
