/* A small JSON reader and a growable text buffer, for the server answers the sync code walks. */
#ifndef SMZ3_JSON_H
#define SMZ3_JSON_H

#include <stddef.h>
#include <stdint.h>

/* The value after key in the object starting at obj (whitespace skipped), or NULL. Only that object's own keys are seen. */
const char *sj_get(const char *obj, const char *key);
/* Past the value at p (string, number, literal, object or array); NULL on malformed input. */
const char *sj_skip(const char *p);
/* The string at v unescaped into out (\u escapes become UTF-8). Returns its length, or -1 when v is not a string or it does not fit. */
int sj_str(const char *v, char *out, size_t cap);
/* The same, into a malloc'd string (NULL when v is not a string). */
char *sj_strdup(const char *v);
/* The number at v, or def when v is NULL or not a number. */
long long sj_int(const char *v, long long def);
/* The first element of the array at v / the element after e; NULL at the end. */
const char *sj_first(const char *v);
const char *sj_next(const char *e);

typedef struct { char *p; size_t n, cap; int failed; } SjBuf;
void sj_add(SjBuf *b, const void *data, size_t n);
void sj_fmt(SjBuf *b, const char *fmt, ...);
/* s as a quoted JSON string. */
void sj_quote(SjBuf *b, const char *s);
void sj_free(SjBuf *b);

#endif
