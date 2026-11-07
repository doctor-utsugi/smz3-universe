#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_daily.h"

/* ---- a small reader for the server's JSON: values are looked up by key inside [p, end) */

typedef struct { const char *p, *end; } Span;

static const char *skip_string(const char *p, const char *end)
{
    for (p++; p < end && *p != '"'; p++) if (*p == '\\' && p + 1 < end) p++;
    return p < end ? p + 1 : end;
}

/* The end of the value starting at p (an object, array, string or bare word). */
static const char *value_end(const char *p, const char *end)
{
    if (p >= end) return end;
    if (*p == '"') return skip_string(p, end);
    if (*p != '{' && *p != '[') { while (p < end && *p != ',' && *p != '}' && *p != ']') p++; return p; }
    int depth = 0;
    while (p < end) {
        if (*p == '"') { p = skip_string(p, end); continue; }
        if (*p == '{' || *p == '[') depth++;
        else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
        p++;
    }
    return end;
}

/* The value of key among the members of the object s (not inside nested ones). */
static Span member(Span s, const char *key)
{
    Span none = {NULL, NULL};
    const char *p = s.p;
    if (!p || p >= s.end || *p != '{') return none;
    size_t kl = strlen(key);
    for (p++; p < s.end;) {
        while (p < s.end && (*p == ',' || *p == ' ' || *p == '\n')) p++;
        if (p >= s.end || *p != '"') return none;
        const char *k = p + 1, *ke = skip_string(p, s.end) - 1;
        p = ke + 1;
        if (p < s.end && *p == ':') p++;
        const char *ve = value_end(p, s.end);
        if ((size_t)(ke - k) == kl && !strncmp(k, key, kl)) { Span v = {p, ve}; return v; }
        p = ve;
    }
    return none;
}

static Span root(const char *json) { Span s = {json, json ? json + strlen(json) : NULL}; if (json) { while (*s.p == ' ' || *s.p == '\n') s.p++; s.end = value_end(s.p, s.end); } return s; }
static int64_t num(Span v) { return v.p && v.p < v.end && (*v.p == '-' || (*v.p >= '0' && *v.p <= '9')) ? strtoll(v.p, NULL, 10) : 0; }
static int is_obj(Span v) { return v.p && v.p < v.end && *v.p == '{'; }

/* A string value without its quotes and escapes; non-ASCII from \u escapes becomes '?'. */
static int str(Span v, char *out, size_t cap)
{
    out[0] = 0;
    if (!v.p || v.p >= v.end || *v.p != '"' || !cap) return 0;
    size_t n = 0;
    for (const char *p = v.p + 1; p < v.end - 1 && n + 1 < cap; p++) {
        char c = *p;
        if (c == '\\' && p + 1 < v.end - 1) {
            c = *++p;
            if (c == 'n') c = ' ';
            else if (c == 'u') { unsigned u = (unsigned)strtoul((char[5]){p[1], p[2], p[3], p[4], 0}, NULL, 16); c = u < 128 ? (char)u : '?'; p += 4; }
        }
        out[n++] = c;
    }
    out[n] = 0;
    return 1;
}

/* Calls fn for each element of an array. */
static int each(Span a, void (*fn)(Span, void *), void *user)
{
    if (!a.p || a.p >= a.end || *a.p != '[') return 0;
    int n = 0;
    for (const char *p = a.p + 1; p < a.end;) {
        while (p < a.end && (*p == ',' || *p == ' ' || *p == '\n')) p++;
        if (p >= a.end || *p == ']') break;
        const char *e = value_end(p, a.end);
        Span el = {p, e};
        fn(el, user);
        n++;
        p = e;
    }
    return n;
}

static SmzDailyState state_of(Span v)
{
    char s[16];
    str(v, s, sizeof s);
    return !strcmp(s, "open") ? DAILY_OPEN : !strcmp(s, "started") ? DAILY_STARTED : !strcmp(s, "done") ? DAILY_DONE
         : !strcmp(s, "late") ? DAILY_LATE : !strcmp(s, "failed") ? DAILY_FAILED : DAILY_NONE;
}

typedef struct { char (*list)[32]; int *n; int cap; } StartList;
static void add_start(Span el, void *user)
{
    StartList *l = user;
    char t[64];
    if (*l->n >= l->cap || !str(el, t, sizeof t)) return;
    snprintf(l->list[(*l->n)++], 32, "%s", strncmp(t, "Start with ", 11) ? t : t + 11);   /* the list is shown under "Start with" */
}

int smz_daily_parse_info(const char *json, int64_t local_now_ms, SmzDailyInfo *d)
{
    memset(d, 0, sizeof *d);
    d->loaded = -1;
    d->players_now = d->players_done = -1;
    Span r = root(json);
    if (!is_obj(r) || !member(r, "now").p) return -1;
    int64_t shift = local_now_ms - num(member(r, "now"));
    Span cur = member(r, "current"), next = member(r, "next"), tok = member(r, "tokens");
    if (is_obj(cur)) {
        str(member(cur, "id"), d->window, sizeof d->window);
        str(member(cur, "goal"), d->goal, sizeof d->goal);
        str(member(cur, "difficulty"), d->difficulty, sizeof d->difficulty);
        StartList l = {d->start, &d->n_start, SMZ_DAILY_START};
        each(member(cur, "start"), add_start, &l);
        d->est_min = (int)num(member(cur, "estimateMin"));
        d->opens_at = num(member(cur, "opensAt")) + shift;
        d->closes_at = num(member(cur, "closesAt")) + shift;
        d->state = state_of(member(cur, "state"));
        if (member(cur, "startedAt").p && num(member(cur, "startedAt"))) d->started_at = num(member(cur, "startedAt")) + shift;
        d->time_ms = num(member(cur, "timeMs"));
        Span pl = member(cur, "players");
        if (is_obj(pl)) { d->players_now = (int)num(member(pl, "now")); d->players_done = (int)num(member(pl, "done")); }
    }
    if (is_obj(next)) d->next_opens_at = num(member(next, "opensAt")) + shift;
    if (is_obj(tok)) { d->tokens = (int)num(member(tok, "balance")); d->streak = (int)num(member(tok, "streak")); }
    d->loaded = 1;
    return 0;
}

typedef struct { SmzDailyRun *r; int64_t shift; } HintCtx;
static void add_hint(Span el, void *user)
{
    HintCtx *h = user;
    if (h->r->n_hints >= SMZ_DAILY_HINTS || !is_obj(el)) return;
    h->r->hint_at[h->r->n_hints] = num(member(el, "at")) + h->shift;
    str(member(el, "text"), h->r->hint[h->r->n_hints], SMZ_DAILY_HINT_LEN);
    h->r->n_hints++;
}

static void hints_of(Span r, int64_t shift, SmzDailyRun *run)
{
    HintCtx h = {run, shift};
    run->n_hints = 0;
    each(member(r, "hints"), add_hint, &h);
    int64_t next = num(member(r, "nextHintAt"));
    run->next_hint_at = next ? next + shift : 0;
}

/* The answers carry no "now", so the device clock is taken as the server's (the info answer gives the offset when it matters). */
static void players_of(Span j, SmzDailyRun *r);

int smz_daily_parse_start(const char *json, int64_t local_now_ms, SmzDailyRun *r, char *settings, size_t settings_cap)
{
    memset(r, 0, sizeof *r);
    Span j = root(json), set = member(j, "settings"), goal = member(j, "goal");
    if (!is_obj(j) || !set.p || !str(member(j, "token"), r->token, sizeof r->token) || !r->token[0]) return -1;
    int64_t shift = member(j, "now").p ? local_now_ms - num(member(j, "now")) : 0;
    str(member(j, "window"), r->window, sizeof r->window);
    if (is_obj(goal)) str(member(goal, "text"), r->goal, sizeof r->goal);
    r->started_at = num(member(j, "startedAt")) + shift;
    r->closes_at = num(member(j, "closesAt")) + shift;
    hints_of(j, shift, r);
    players_of(j, r);
    if (set.p && *set.p == '"') { if (!str(set, settings, settings_cap) || settings[0] != '{') return -1; }   /* the server sends it as a string */
    else {
        size_t n = (size_t)(set.end - set.p);
        if (n + 1 > settings_cap) return -1;
        memcpy(settings, set.p, n);
        settings[n] = 0;
    }
    const char *g = strstr(settings, "\"goal\":\"");
    if (g) { g += 8; snprintf(r->check, sizeof r->check, "%.*s", (int)strcspn(g, "\""), g); }
    return r->started_at && r->closes_at && r->check[0] ? 0 : -1;
}

static void players_of(Span j, SmzDailyRun *r)
{
    Span p = member(j, "players");
    if (!is_obj(p)) return;
    r->players_known = 1;
    r->players_now = (int)num(member(p, "now"));
    r->players_started = (int)num(member(p, "started"));
    r->players_done = (int)num(member(p, "done"));
}

int smz_daily_parse_hints(const char *json, int64_t local_now_ms, SmzDailyRun *r)
{
    Span j = root(json);
    if (!is_obj(j) || !member(j, "hints").p) return -1;
    int64_t shift = member(j, "now").p ? local_now_ms - num(member(j, "now")) : 0;
    hints_of(j, shift, r);
    players_of(j, r);
    return 0;
}

int smz_daily_run_text(const SmzDailyRun *r, char *out, size_t cap)
{
    int n = snprintf(out, cap, "window\t%s\ntoken\t%s\ngoal\t%s\ncheck\t%s\nstarted\t%lld\ncloses\t%lld\nnext\t%lld\n", r->window, r->token, r->goal, r->check,
                     (long long)r->started_at, (long long)r->closes_at, (long long)r->next_hint_at);
    if (r->began_at && n < (int)cap) n += snprintf(out + n, cap - n, "began\t%lld\n", (long long)r->began_at);
    for (int i = 0; i < r->n_hints && n < (int)cap; i++) n += snprintf(out + n, cap - n, "hint\t%lld\t%s\n", (long long)r->hint_at[i], r->hint[i]);
    return n < (int)cap ? n : -1;
}

int64_t smz_daily_clock_from(const SmzDailyRun *r) { return r->began_at > r->started_at ? r->began_at : r->started_at; }

int smz_daily_note_began(SmzDailyRun *r, int64_t now_ms)
{
    if (r->began_at || !r->started_at) return 0;
    r->began_at = now_ms - r->started_at <= 10 * 60000 && now_ms > r->started_at ? now_ms : r->started_at;
    return 1;
}

int smz_daily_run_load(const char *text, SmzDailyRun *r)
{
    memset(r, 0, sizeof *r);
    if (!text) return -1;
    for (const char *p = text; *p;) {
        const char *nl = strchr(p, '\n'), *e = nl ? nl : p + strlen(p), *tab = memchr(p, '\t', (size_t)(e - p));
        if (tab) {
            char key[16], val[512];
            snprintf(key, sizeof key, "%.*s", (int)(tab - p), p);
            snprintf(val, sizeof val, "%.*s", (int)(e - tab - 1), tab + 1);
            if (!strcmp(key, "window")) snprintf(r->window, sizeof r->window, "%.15s", val);
            else if (!strcmp(key, "token")) snprintf(r->token, sizeof r->token, "%.319s", val);
            else if (!strcmp(key, "goal")) snprintf(r->goal, sizeof r->goal, "%.95s", val);
            else if (!strcmp(key, "check")) snprintf(r->check, sizeof r->check, "%.31s", val);
            else if (!strcmp(key, "started")) r->started_at = strtoll(val, NULL, 10);
            else if (!strcmp(key, "closes")) r->closes_at = strtoll(val, NULL, 10);
            else if (!strcmp(key, "next")) r->next_hint_at = strtoll(val, NULL, 10);
            else if (!strcmp(key, "began")) r->began_at = strtoll(val, NULL, 10);
            else if (!strcmp(key, "hint") && r->n_hints < SMZ_DAILY_HINTS) {
                char *t2 = strchr(val, '\t');
                if (t2) {
                    r->hint_at[r->n_hints] = strtoll(val, NULL, 10);
                    snprintf(r->hint[r->n_hints], SMZ_DAILY_HINT_LEN, "%s", t2 + 1);
                    r->n_hints++;
                }
            }
        }
        p = nl ? nl + 1 : e;
    }
    return r->window[0] && r->token[0] && r->started_at ? 0 : -1;
}

void smz_daily_span(int64_t ms, char *out, size_t cap)
{
    long long s = ms > 0 ? (ms + 999) / 1000 : 0;
    if (s >= 3600) snprintf(out, cap, "%lldh %02lldm", s / 3600, s / 60 % 60);
    else if (s >= 60) snprintf(out, cap, "%lldm %02llds", s / 60, s % 60);
    else snprintf(out, cap, "%llds", s);
}

int smz_daily_prepared_bosses(const char *settings)
{
    const char *p = settings ? strstr(settings, "\"smBossesDead\":") : NULL;
    return p ? atoi(p + 15) & 15 : 0;
}

int smz_daily_mark_settings(char *json, size_t cap, const char *goal_check, const char *server_settings)
{
    char add[112];
    int an = snprintf(add, sizeof add, "\"challenge\":true,\"goal\":\"%s\",", goal_check);
    int dead = smz_daily_prepared_bosses(server_settings);
    if (dead) an += snprintf(add + an, sizeof add - an, "\"smBossesDead\":%d,", dead);
    size_t n = strlen(json);
    if (json[0] != '{' || n + (size_t)an + 1 > cap || strstr(json, "\"challenge\":")) return -1;
    memmove(json + 1 + an, json + 1, n);   /* the terminator moves too */
    memcpy(json + 1, add, (size_t)an);
    /* the map shows only what is collected: which checks are open now would be a free hint */
    char *r = strstr(json, "\"showReachable\":true");
    if (r && strlen(json) + 2 <= cap) { memmove(r + 21, r + 20, strlen(r + 20) + 1); memcpy(r + 16, "false", 5); }
    return 0;
}

const char *const SMZ_DAILY_PHRASE[SMZ_DAILY_PHRASES] = {"GL HF!", "GG", ":)", ":D", ":(", "<3", "o7", "!!"};

typedef struct { SmzDailyChat *c; } ChatCtx;
static void add_line(Span el, void *user)
{
    SmzDailyChat *c = ((ChatCtx *)user)->c;
    int64_t seq = num(member(el, "seq"));
    if (!is_obj(el) || seq <= c->since) return;
    c->since = seq;
    if (c->lines == RACE_LINES) { memmove(c->line, c->line + 1, sizeof c->line[0] * (RACE_LINES - 1)); c->lines--; }
    SmzRaceLine *l = &c->line[c->lines++];
    memset(l, 0, sizeof *l);
    char k[12];
    str(member(el, "kind"), k, sizeof k);
    l->kind = !strcmp(k, "finish") ? RACE_LINE_FINISH : RACE_LINE_CHAT;
    l->seq = seq;
    l->at = num(member(el, "at"));
    str(member(el, "pid"), l->pid, sizeof l->pid);
    str(member(el, "name"), l->name, sizeof l->name);
    str(member(el, "text"), l->text, sizeof l->text);
    c->fresh++;
}

int smz_daily_parse_chat(const char *json, SmzDailyChat *c, SmzDailyRun *r)
{
    Span j = root(json);
    if (!is_obj(j) || !member(j, "log").p) return -1;
    ChatCtx x = {c};
    c->fresh = 0;
    each(member(j, "log"), add_line, &x);
    if (r) players_of(j, r);
    return 0;
}

int smz_daily_chat_body(const SmzDailyRun *r, const char *player, const char *country, const char *text, char *out, size_t cap)
{
    char p[64], t[200];
    smz_race_quote(player ? player : "", p, sizeof p);
    smz_race_quote(text ? text : "", t, sizeof t);
    int n = snprintf(out, cap, "{\"token\":\"%s\",\"player\":%s,\"country\":\"%s\",\"text\":%s}", r->token, p, country ? country : "", t);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}
