#include <string.h>
#include "smz3_preview.h"

int smz_preview_parse(const uint8_t *buf, size_t len, int *w, int *h, const uint8_t **rgba)
{
    if (!buf || len < 2 || !buf[0] || !buf[1]) return 0;
    if ((size_t)buf[0] * buf[1] * 4 + 2 > len) return 0;
    *w = buf[0]; *h = buf[1]; *rgba = buf + 2;
    return 1;
}

int smz_preview_runs(const uint8_t *rgba, int w, int h, int dw, int dh)
{
    int runs = 0;
    for (int y = 0; y < dh; y++) {
        const uint8_t *row = rgba + (size_t)(y * h / dh) * w * 4;
        int x = 0;
        while (x < dw) {
            const uint8_t *px = row + (size_t)(x * w / dw) * 4;
            int x2 = x + 1;
            while (x2 < dw && !memcmp(row + (size_t)(x2 * w / dw) * 4, px, 4)) x2++;
            if (px[3] >= 128) runs++;
            x = x2;
        }
    }
    return runs;
}
