#include "smz3_race.h"
#include "smz3_rom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A small JSON reader: pointers into the text, values skipped whole. Enough for the race server's answers. */
static const char *ws(const char *p) { while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }

static const char *skip(const char *p)
{
    p = ws(p);
    if (!p || !*p) return NULL;
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
        return *p ? p + 1 : NULL;
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        for (; *p; p++) {
            if (*p == '"') { p = skip(p); if (!p) return NULL; p--; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

/* The value of key in the object at obj, or NULL. */
static const char *get(const char *obj, const char *key)
{
    obj = ws(obj);
    if (!obj || *obj != '{') return NULL;
    const char *p = obj + 1;
    size_t kl = strlen(key);
    for (;;) {
        p = ws(p);
        if (!p || *p != '"') return NULL;
        const char *k = p + 1, *v = skip(p);
        if (!v) return NULL;
        int match = (size_t)(v - 1 - k) == kl && !memcmp(k, key, kl);
        v = ws(v);
        if (!v || *v != ':') return NULL;
        v = ws(v + 1);
        if (match) return v;
        p = ws(skip(v));
        if (!p || *p != ',') return NULL;
        p++;
    }
}

static void str(const char *v, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    v = ws(v);
    if (!v || *v != '"') return;
    for (v++; *v && *v != '"' && n + 1 < cap; v++) {
        char c = *v;
        if (c == '\\' && v[1]) {
            v++;
            c = *v == 'n' ? ' ' : *v == 't' ? ' ' : *v == 'u' ? '?' : *v;
            if (*v == 'u') for (int i = 0; i < 4 && v[1]; i++) v++;
        }
        out[n++] = c;
    }
    out[n] = 0;
}

static int64_t num(const char *v, int64_t def)
{
    v = ws(v);
    if (!v || (*v != '-' && (*v < '0' || *v > '9'))) return def;
    return strtoll(v, NULL, 10);
}

static int flag(const char *v) { v = ws(v); return v && !strncmp(v, "true", 4); }

/* The first element of the array at arr, then each next one; NULL at the end. */
static const char *first(const char *arr) { arr = ws(arr); if (!arr || *arr != '[') return NULL; arr = ws(arr + 1); return arr && *arr != ']' ? arr : NULL; }
static const char *next(const char *el) { const char *p = ws(skip(el)); if (!p || *p != ',') return NULL; return ws(p + 1); }

static int state_of(const char *s)
{
    static const char *const N[] = {"", "open", "closed", "running", "finished", "cancelled", "paused"};
    for (int i = 1; i < 7; i++) if (!strcmp(s, N[i])) return i;
    return RACE_NONE;
}

static void room(SmzRace *r, const char *o)
{
    char s[16];
    str(get(o, "state"), s, sizeof s);
    r->state = state_of(s);
    str(get(o, "code"), r->code, sizeof r->code);
    str(get(o, "format"), r->format, sizeof r->format);
    str(get(o, "creator"), r->creator, sizeof r->creator);
    str(get(o, "endWhy"), r->end_why, sizeof r->end_why);
    r->size = (int)num(get(o, "size"), 2);
    r->is_public = flag(get(o, "public"));
    r->start_at = num(get(o, "startAt"), 0);
    r->end_at = num(get(o, "endAt"), 0);
    r->go_at = num(get(o, "goAt"), r->start_at);
    r->resume_at = num(get(o, "resumeAt"), 0);
    r->pauses = (int)num(get(o, "pauses"), 0);
    const char *v = get(o, "vote");
    str(v ? get(v, "by") : NULL, r->vote_by, sizeof r->vote_by);
    r->vote_at = v ? num(get(v, "resumeAt"), 0) : 0;
    r->vote_n = 0;
    for (const char *e = first(v ? get(v, "yes") : NULL); e && r->vote_n < RACE_MAX; e = next(e)) str(e, r->vote_yes[r->vote_n++], sizeof r->vote_yes[0]);
    if (get(o, "seed")) str(get(o, "seed"), r->seed, sizeof r->seed);
    if (get(o, "settings")) str(get(o, "settings"), r->settings, sizeof r->settings);
    r->n = 0;
    for (const char *e = first(get(o, "players")); e && r->n < RACE_MAX; e = next(e)) {
        SmzRacePlayer *p = &r->p[r->n++];
        str(get(e, "pid"), p->pid, sizeof p->pid);
        str(get(e, "name"), p->name, sizeof p->name);
        str(get(e, "country"), p->country, sizeof p->country);
        str(get(e, "link"), p->link, sizeof p->link);
        str(get(e, "samus"), p->samus, sizeof p->samus);
        p->ready = flag(get(e, "ready"));
        p->left = flag(get(e, "left"));
        p->held = flag(get(e, "held"));
        p->away = flag(get(e, "away"));
        p->back = flag(get(e, "back"));
        p->progress = (int)num(get(e, "progress"), 0);
        p->place = (int)num(get(e, "place"), 0);
        p->time_ms = num(get(e, "timeMs"), -1);
    }
}

int smz_race_read(SmzRace *r, const char *json, int64_t local_now)
{
    const char *o = json ? get(json, "room") : NULL;
    if (!o) return -1;
    room(r, o);
    int64_t now = num(get(json, "now"), 0);
    if (now) r->offset = now - local_now;
    if (get(json, "you")) str(get(json, "you"), r->you, sizeof r->you);
    r->fresh = 0;
    for (const char *e = first(get(json, "log")); e; e = next(e)) {
        int64_t seq = num(get(e, "seq"), 0);
        if (seq <= r->since) continue;
        r->since = seq;
        if (r->lines == RACE_LINES) { memmove(r->line, r->line + 1, sizeof r->line[0] * (RACE_LINES - 1)); r->lines--; }
        SmzRaceLine *l = &r->line[r->lines++];
        char k[12];
        str(get(e, "kind"), k, sizeof k);
        l->kind = !strcmp(k, "event") ? RACE_LINE_EVENT : !strcmp(k, "chat") ? RACE_LINE_CHAT : !strcmp(k, "finish") ? RACE_LINE_FINISH : RACE_LINE_SYSTEM;
        l->seq = seq;
        l->at = num(get(e, "at"), 0);
        str(get(e, "pid"), l->pid, sizeof l->pid);
        str(get(e, "name"), l->name, sizeof l->name);
        str(get(e, "text"), l->text, sizeof l->text);
        r->fresh++;
    }
    return 0;
}

int smz_race_rooms(const char *json, SmzRaceRoom *rows, int max)
{
    int n = 0;
    for (const char *e = first(json ? get(json, "rooms") : NULL); e && n < max; e = next(e)) {
        SmzRaceRoom *w = &rows[n++];
        str(get(e, "code"), w->code, sizeof w->code);
        str(get(e, "format"), w->format, sizeof w->format);
        str(get(e, "creator"), w->creator, sizeof w->creator);
        w->size = (int)num(get(e, "size"), 2);
        w->players = (int)num(get(e, "players"), 0);
    }
    return n;
}

void smz_race_error(const char *json, char *out, size_t cap) { str(json ? get(json, "error") : NULL, out, cap); }

const SmzRacePlayer *smz_race_me(const SmzRace *r)
{
    for (int i = 0; i < r->n; i++) if (r->you[0] && !strcmp(r->p[i].pid, r->you)) return &r->p[i];
    return NULL;
}

int64_t smz_race_server_now(const SmzRace *r, int64_t local_now) { return local_now + r->offset; }

int smz_race_voted(const SmzRace *r)
{
    for (int i = 0; i < r->vote_n; i++) if (r->you[0] && !strcmp(r->vote_yes[i], r->you)) return 1;
    return 0;
}

void smz_race_when(int64_t ms, char *out, size_t cap)
{
    static const char *const DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    time_t t = (time_t)(ms / 1000);
    struct tm *tm = localtime(&t);
    if (!tm) { snprintf(out, cap, "?"); return; }
    snprintf(out, cap, "%s %02d:%02d", DAY[tm->tm_wday % 7], tm->tm_hour, tm->tm_min);
}

const char *smz_race_format_name(const char *f) { return !strcmp(f, "sprint") ? "Sprint" : !strcmp(f, "half") ? "Half" : !strcmp(f, "draft") ? "Draft" : "Full game"; }

void smz_race_quote(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    if (cap < 3) { if (cap) out[0] = 0; return; }
    out[n++] = '"';
    for (; s && *s && n + 3 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = (char)c; }
        else if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; }   /* a proof is one check per line */
        else if (c >= 0x20) out[n++] = (char)c;
    }
    out[n++] = '"';
    out[n] = 0;
}

int smz_race_mark_settings(char *json, size_t cap, const char *room)
{
    char goal[32], start[16], add[96];
    str(room ? get(room, "goal") : NULL, goal, sizeof goal);
    str(room ? get(room, "start") : NULL, start, sizeof start);
    int an = 0;
    if (!strstr(json, "\"challenge\":")) an += snprintf(add + an, sizeof add - an, "\"challenge\":true,");   /* no states, fast forward or rewind */
    if (goal[0] && !strstr(json, "\"goal\":")) an += snprintf(add + an, sizeof add - an, "\"goal\":\"%s\",", goal);
    if (start[0] && !strstr(json, "\"start\":")) an += snprintf(add + an, sizeof add - an, "\"start\":\"%s\",", start);
    size_t n = strlen(json);
    if (json[0] != '{' || n + (size_t)an + 1 > cap) return -1;
    memmove(json + 1 + an, json + 1, n);   /* the terminator moves too */
    memcpy(json + 1, add, (size_t)an);
    return 0;
}

int smz_race_apply_start(uint8_t *rom, const char *settings)
{
    char start[16];
    int a, b;
    str(settings ? get(settings, "start") : NULL, start, sizeof start);
    if (!start[0]) return 0;
    int rc = sscanf(start, "sm:%d:%d", &a, &b) == 2 ? smz_rom_start_sm(rom, a, b) : sscanf(start, "z3:%d", &a) == 1 ? smz_rom_start_z3(rom, a) : -1;
    if (!rc) smz_rom_checksum(rom);
    return rc;
}
