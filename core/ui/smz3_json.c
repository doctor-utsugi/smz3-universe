#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_json.h"

static const char *ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const char *skip_str(const char *p)
{
    for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
    return *p == '"' ? p + 1 : NULL;
}

const char *sj_skip(const char *p)
{
    p = ws(p);
    if (!p || !*p) return NULL;
    if (*p == '"') return skip_str(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') { if (!(p = skip_str(p))) return NULL; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
            p++;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p;
}

const char *sj_get(const char *obj, const char *key)
{
    const char *p = ws(obj);
    if (!p || *p != '{') return NULL;
    size_t kn = strlen(key);
    for (p = ws(p + 1); p && *p == '"';) {
        const char *k = p + 1, *e = skip_str(p);
        if (!e) return NULL;
        p = ws(e);
        if (*p != ':') return NULL;
        p = ws(p + 1);
        if ((size_t)(e - 1 - k) == kn && !strncmp(k, key, kn)) return p;
        p = ws(sj_skip(p));
        if (!p || *p != ',') return NULL;
        p = ws(p + 1);
    }
    return NULL;
}

static int put_utf8(char *out, size_t cap, size_t *n, unsigned c)
{
    char t[4];
    int k = c < 0x80 ? (t[0] = (char)c, 1) : c < 0x800 ? (t[0] = (char)(0xC0 | c >> 6), t[1] = (char)(0x80 | (c & 63)), 2)
          : (t[0] = (char)(0xE0 | c >> 12), t[1] = (char)(0x80 | (c >> 6 & 63)), t[2] = (char)(0x80 | (c & 63)), 3);
    if (*n + k >= cap) return -1;
    memcpy(out + *n, t, k);
    *n += k;
    return 0;
}

int sj_str(const char *v, char *out, size_t cap)
{
    v = ws(v);
    if (!v || *v != '"' || !cap) return -1;
    size_t n = 0;
    for (v++; *v && *v != '"'; v++) {
        unsigned c = (unsigned char)*v;
        if (c == '\\') {
            c = (unsigned char)*++v;
            if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r'; else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            else if (c == 'u') {
                char hex[5] = {0};
                for (int i = 0; i < 4; i++) { if (!v[1 + i]) return -1; hex[i] = v[1 + i]; }
                v += 4;
                if (put_utf8(out, cap, &n, (unsigned)strtoul(hex, NULL, 16))) return -1;
                continue;
            }
        }
        if (n + 1 >= cap) return -1;
        out[n++] = (char)c;
    }
    if (*v != '"') return -1;
    out[n] = 0;
    return (int)n;
}

char *sj_strdup(const char *v)
{
    const char *e = ws(v);
    if (!e || *e != '"' || !(e = skip_str(e))) return NULL;
    size_t cap = (size_t)(e - v) + 1;
    char *s = malloc(cap);
    if (s && sj_str(v, s, cap) < 0) { free(s); s = NULL; }
    return s;
}

long long sj_int(const char *v, long long def)
{
    v = ws(v);
    if (!v || !(*v == '-' || (*v >= '0' && *v <= '9'))) return def;
    return strtoll(v, NULL, 10);
}

const char *sj_first(const char *v)
{
    v = ws(v);
    if (!v || *v != '[') return NULL;
    v = ws(v + 1);
    return *v == ']' ? NULL : v;
}

const char *sj_next(const char *e)
{
    e = ws(sj_skip(e));
    return e && *e == ',' ? ws(e + 1) : NULL;
}

void sj_add(SjBuf *b, const void *data, size_t n)
{
    if (b->failed) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) { b->failed = 1; return; }
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
    b->p[b->n] = 0;
}

void sj_fmt(SjBuf *b, const char *fmt, ...)
{
    char small[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) { b->failed = 1; return; }
    if ((size_t)n < sizeof small) { sj_add(b, small, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    if (!big) { b->failed = 1; return; }
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sj_add(b, big, (size_t)n);
    free(big);
}

void sj_quote(SjBuf *b, const char *s)
{
    sj_add(b, "\"", 1);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { char e[2] = {'\\', (char)c}; sj_add(b, e, 2); }
        else if (c == '\n') sj_add(b, "\\n", 2);
        else if (c == '\r') sj_add(b, "\\r", 2);
        else if (c == '\t') sj_add(b, "\\t", 2);
        else if (c < 0x20) sj_fmt(b, "\\u%04x", c);
        else sj_add(b, s, 1);
    }
    sj_add(b, "\"", 1);
}

void sj_free(SjBuf *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; b->failed = 0; }
