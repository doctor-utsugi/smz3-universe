/* gcc -O1 -Wall -Icore/ui core/ui/test_preview.c core/ui/smz3_preview.c -o build/test_preview
 * ./build/test_preview <dir with link/ and samus/ .pv files> : parses every real file, fuzzes the parser and counts the
 * rectangles of each page of the picker (15 cells). */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_preview.h"

static unsigned char *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    int w, h; const uint8_t *px;
    /* malformed buffers */
    uint8_t one[1] = {5}, zero[6] = {0, 4, 1, 2, 3, 4}, shortb[2 + 3 * 3 * 4 - 1] = {3, 3};
    assert(!smz_preview_parse(NULL, 0, &w, &h, &px) && !smz_preview_parse(one, 1, &w, &h, &px));
    assert(!smz_preview_parse(zero, sizeof zero, &w, &h, &px) && !smz_preview_parse(shortb, sizeof shortb, &w, &h, &px));
    /* fuzz: random sizes and lengths, parse then walk the runs; run it under -fsanitize=address to catch an overread */
    srand(7);
    for (int it = 0; it < 20000; it++) {
        size_t len = rand() % 600;
        uint8_t *b = malloc(len ? len : 1);
        for (size_t i = 0; i < len; i++) b[i] = (uint8_t)rand();
        if (len >= 2 && rand() % 2) { b[0] = 1 + rand() % 12; b[1] = 1 + rand() % 12; }
        if (smz_preview_parse(b, len, &w, &h, &px)) {
            int dw = 1 + rand() % 56, dh = 1 + rand() % 56;
            (void)smz_preview_runs(px, w, h, dw, dh);
        }
        free(b);
    }
    if (argc < 2) { puts("fuzz ok"); return 0; }
    int files = 0, worst = 0, total = 0;
    const char *sub[2] = {"link", "samus"};
    for (int s = 0; s < 2; s++) {
        char dir[512]; snprintf(dir, sizeof dir, "%s/%s", argv[1], sub[s]);
        DIR *d = opendir(dir); struct dirent *e;
        int cells = 0, page = 0;
        while (d && (e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 4 || strcmp(e->d_name + l - 3, ".pv")) continue;
            char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            size_t n; unsigned char *b = slurp(p, &n);
            assert(b && smz_preview_parse(b, n, &w, &h, &px));
            page += smz_preview_runs(px, w, h, w, h);   /* worst case: drawn at its own size (never larger than 56x56) */
            if (++cells % 15 == 0) { if (page > worst) worst = page; total += page; page = 0; }
            files++; free(b);
        }
        if (d) closedir(d);
        if (page > worst) worst = page;
    }
    printf("%d previews parsed, most rectangles on one page of 15: %d\n", files, worst);
    return 0;
}
