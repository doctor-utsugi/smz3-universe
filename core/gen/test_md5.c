/* gcc -O1 -Wall -Icore/gen core/gen/test_md5.c core/gen/smz3_md5.c -o build/test_md5 && build/test_md5 <file>  (prints the md5; checks the vectors) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_md5.h"
int main(int argc, char **argv)
{
    char h[33];
    smz_md5_hex((const uint8_t *)"", 0, h); assert(!strcmp(h, "d41d8cd98f00b204e9800998ecf8427e"));
    smz_md5_hex((const uint8_t *)"abc", 3, h); assert(!strcmp(h, "900150983cd24fb0d6963f7d28e17f72"));
    const char *s = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    smz_md5_hex((const uint8_t *)s, strlen(s), h); assert(!strcmp(h, "57edf4a22be3c955ac49da2e2107b67a"));
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *b = malloc(n); fread(b, 1, n, f); fclose(f);
        smz_md5_hex(b, n, h); printf("%s\n", h);
    } else puts("md5 vectors ok");
    return 0;
}
