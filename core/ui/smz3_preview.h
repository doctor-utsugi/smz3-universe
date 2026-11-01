/* Sprite preview pictures (.pv): u8 width, u8 height, then width*height RGBA bytes. */
#ifndef SMZ3_PREVIEW_H
#define SMZ3_PREVIEW_H

#include <stddef.h>
#include <stdint.h>

/* Checks a .pv buffer: returns 1 and the picture size when it is complete (non-empty and exactly the bytes its header
 * announces, extra bytes ignored), 0 for anything else (too short, zero size, truncated). rgba points into buf. */
int smz_preview_parse(const uint8_t *buf, size_t len, int *w, int *h, const uint8_t **rgba);

/* Number of rectangles (runs of one colour per row, transparent runs skipped) drawing the picture needs at size dw x dh. */
int smz_preview_runs(const uint8_t *rgba, int w, int h, int dw, int dh);

#endif
