#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pc_compat.h"
#ifdef _WIN32
#include <bcrypt.h>
#endif
#include "pc_online.h"
#include "pc_store.h"

#define BASE "https://smz3-universe.duckdns.org"

static char *read_text(const char *path);

/* Runs curl without a shell. Returns the HTTP status (-1 when curl failed) and the body in out. */
#ifdef __EMSCRIPTEN__
#include "web.h"
static int http(const char *method, const char *path, const char *body, char *out, size_t cap)
{
    char url[256];
    uint8_t *o = NULL;
    snprintf(url, sizeof url, "%s%s", online_server(), path);
    int code = web_http(method, url, body ? "Content-Type: application/json\n" : NULL, (const uint8_t *)body, body ? strlen(body) : 0,
                        &o, NULL, NULL, NULL, 0);
    snprintf(out, cap, "%s", o ? (char *)o : "");
    free(o);
    return code;
}
#elif defined(_WIN32)
static int http(const char *method, const char *path, const char *body, char *out, size_t cap)
{
    char req[600], res[600], cmd[2048], url[256];
    snprintf(req, sizeof req, "%s", store_path("tmp/request%lu.json", (unsigned long)GetCurrentThreadId()));   /* requests run on several threads */
    snprintf(res, sizeof res, "%s", store_path("tmp/response%lu.txt", (unsigned long)GetCurrentThreadId()));
    snprintf(url, sizeof url, "%s%s", online_server(), path);
    out[0] = 0;
    if (body && store_write(req, body, strlen(body))) return -1;
    int n = snprintf(cmd, sizeof cmd, "curl.exe -s -m 8 -X %s -H \"Content-Type: application/json\" ", method);
    if (body) n += snprintf(cmd + n, sizeof cmd - n, "--data-binary @\"%s\" ", req);
    snprintf(cmd + n, sizeof cmd - n, "-w \"\\n%%{http_code}\" \"%s\"", url);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE h = CreateFileA(res, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    STARTUPINFOA si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(h);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    remove(req);
    FILE *f = fopen(res, "rb");
    if (!f) return -1;
    size_t len = fread(out, 1, cap - 1, f);
    fclose(f);
    remove(res);
    out[len] = 0;
    char *nl = strrchr(out, '\n');
    if (!nl || exit_code != 0) return -1;
    int code = atoi(nl + 1);
    *nl = 0;
    return code;
}
#else
static int http(const char *method, const char *path, const char *body, char *out, size_t cap)
{
    int in_pipe[2], out_pipe[2];
    out[0] = 0;
    if (pipe(in_pipe) || pipe(out_pipe)) return -1;
    char url[256];
    snprintf(url, sizeof url, "%s%s", online_server(), path);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(in_pipe[0], 0); dup2(out_pipe[1], 1);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) dup2(nul, 2);
        close(in_pipe[1]); close(out_pipe[0]);
        unsetenv("LD_LIBRARY_PATH");   /* the AppImage's bundled libs break the system curl */
        unsetenv("LD_PRELOAD");
        if (body) execlp("curl", "curl", "-s", "-m", "8", "-X", method, "-H", "Content-Type: application/json", "--data-binary", "@-", "-w", "\n%{http_code}", url, (char *)NULL);
        execlp("curl", "curl", "-s", "-m", "8", "-w", "\n%{http_code}", url, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]); close(out_pipe[1]);
    if (body) { size_t n = strlen(body); if (write(in_pipe[1], body, n) < 0) {} }
    close(in_pipe[1]);
    size_t len = 0;
    ssize_t r;
    while (len + 1 < cap && (r = read(out_pipe[0], out + len, cap - 1 - len)) > 0) len += (size_t)r;
    out[len] = 0;
    close(out_pipe[0]);
    int status;
    waitpid(pid, &status, 0);
    char *nl = strrchr(out, '\n');
    if (!nl || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    int code = atoi(nl + 1);
    *nl = 0;
    return code;
}

#endif

/* The game of the runs this thread reaches (store_get_mode): SMZ3 sends no game, so its requests stay as they always were.
 * game_query gives "" or "<sep>game=sm" for a path, game_field "" or ",\"game\":\"sm\"" for a JSON body. */
static const char *game_query(char sep)
{
    static _Thread_local char q[24];
    int mode = store_get_mode();
    if (mode == SMZ_MODE_SMZ3) return "";
    snprintf(q, sizeof q, "%cgame=%s", sep, smz_mode_id(mode));
    return q;
}

static const char *game_field(void)
{
    static _Thread_local char f[24];
    int mode = store_get_mode();
    if (mode == SMZ_MODE_SMZ3) return "";
    snprintf(f, sizeof f, ",\"game\":\"%s\"", smz_mode_id(mode));
    return f;
}

/* The string value of "key":"..." (no escapes expected from the server), 0 when absent. */
static int json_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    size_t n = strcspn(p, "\"");
    if (n >= cap) return 0;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static char *read_text(const char *path)
{
    size_t n;
    char *t = (char *)store_read(path, &n);
    if (t) t[n] = 0;
    return t;
}

const char *online_server(void)
{
    static char kept[200];
    static int looked;
    if (getenv("SMZ3_SERVER")) return getenv("SMZ3_SERVER");
    if (!looked) {   /* read once: a test server for a whole session */
        looked = 1;
        char *t = read_text(store_path("server.txt"));
        if (t) { t[strcspn(t, " \t\r\n")] = 0; if (!strncmp(t, "http", 4)) snprintf(kept, sizeof kept, "%s", t); free(t); }
    }
    return kept[0] ? kept : BASE;
}

int online_staging(void) { return strcmp(online_server(), BASE) != 0; }

/* Windows builds before 2026-11-25 had no /dev/urandom and all wrote this id, so every Windows player was one install. */
#define ZERO_ID "00000000-0000-0000-0000-000000000000"

static void install_id(char out[48])
{
    char *t = read_text(store_path("runs/install_id"));
    if (t && strlen(t) >= 16 && strlen(t) <= 40 && strncmp(t, ZERO_ID, strlen(ZERO_ID))) { snprintf(out, 48, "%s", t); out[strcspn(out, "\r\n ")] = 0; free(t); return; }
    free(t);
    unsigned char b[16] = {0};
#ifdef _WIN32
    BCryptGenRandom(NULL, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(b, 1, 16, f) != 16) {} fclose(f); }
#endif
    uint64_t x = (uint64_t)time(NULL) * 6364136223846793005ull ^ (uint64_t)(uintptr_t)&x ^ (uint64_t)clock();
    for (int i = 0; i < 16; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b[i] ^= (unsigned char)(x >> 24); }   /* never all zero, even without a system RNG */
    snprintf(out, 48, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    store_write(store_path("runs/install_id"), out, strlen(out));
}

void online_init(SmzUi *u)
{
    char *t = read_text(store_path("runs/player.txt"));
    ui_player_enable(u, 1);
    ui_player_load(u, t);
    free(t);
}

void online_save_profile(SmzUi *u)
{
    if (!ui_player_dirty(u)) return;
    char text[128];
    if (ui_player_text(u, text, sizeof text) > 0) store_write(store_path("runs/player.txt"), text, strlen(text));
}

static void profile(char name[32], char country[4])
{
    char *t = read_text(store_path("runs/player.txt"));
    name[0] = country[0] = 0;
    if (t) {
        char *tab = strchr(t, '\t');
        if (tab) {
            *tab = 0;
            char *t2 = strchr(tab + 1, '\t');
            if (t2) *t2 = 0;
            snprintf(country, 4, "%s", tab + 1);
        }
        snprintf(name, 32, "%s", t);
        free(t);
    }
    if (!name[0]) { char id[48]; install_id(id); snprintf(name, 32, "Player-%.4s", id); }
    for (char *c = name; *c; c++) if (*c == '"' || *c == '\\' || (unsigned char)*c < 32) *c = '_';
    if (strlen(country) != 2 || !isupper((unsigned char)country[0]) || !isupper((unsigned char)country[1])) country[0] = 0;
}

int online_slot_kind(int slot)
{
    char *t = read_text(store_path("runs/slot%d.online", slot));
    int k = !t ? 0 : !strncmp(t, "original", 8) ? 1 : !strncmp(t, "monthly", 7) ? 2 : !strncmp(t, "weekly", 6) ? 3 : 0;
    free(t);
    return k;
}

void online_mark_run(int slot, int online, int kind, const char *preset)
{
    remove(store_path("runs/slot%d.preset", slot));
    if (online && kind == 1 && preset && *preset) store_write(store_path("runs/slot%d.preset", slot), preset, strlen(preset));
    remove(store_path("runs/slot%d.token", slot));
    remove(store_path("runs/slot%d.finish", slot));
    remove(store_path("runs/slot%d.proof", slot));
    if (online) {
        const char *k = kind == 3 ? "weekly" : kind == 2 ? "monthly" : "original";
        store_write(store_path("runs/slot%d.online", slot), k, strlen(k));
    }
    else remove(store_path("runs/slot%d.online", slot));
}

int online_days_left(void)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    struct tm e = {0};
    e.tm_year = t.tm_year; e.tm_mon = t.tm_mon + 1; e.tm_mday = 1;
    time_t end = timegm(&e);
    return (int)((end - now) / 86400);
}

/* The fixed seed of this period: fetched, or the copy saved for the same period. */
/* "2026-W48": the ISO week by hand, since Windows' strftime has no %G or %V. */
static void iso_week(const struct tm *g, char *out, size_t cap)
{
    int year = g->tm_year + 1900, thursday = g->tm_yday - (g->tm_wday + 6) % 7 + 3;
#define DAYS(y) (((y) % 4 == 0 && (y) % 100 != 0) || (y) % 400 == 0 ? 366 : 365)
    if (thursday < 0) { year--; thursday += DAYS(year); }
    else if (thursday >= DAYS(year)) { thursday -= DAYS(year); year++; }
#undef DAYS
    snprintf(out, cap, "%04d-W%02d", year, thursday / 7 + 1);
}

static int fetch_period(const char *path, const char *file, const char *key, int weekly, char label[16], int *seed, int *playing)
{
    char body[512], s[32], m[16], url[64];
    snprintf(url, sizeof url, "%s%s", path, game_query('?'));
    int code = http("GET", url, NULL, body, sizeof body);
    const char *p = code == 200 ? strstr(body, "\"playing\":") : NULL;
    *playing = p ? atoi(p + 10) : 0;   /* only from a fresh answer: the cached file's count is old */
    if (code == 200 && json_str(body, "seed", s, sizeof s) && json_str(body, key, m, sizeof m) && atoi(s) > 0)
        store_write(store_path("runs/%s", file), body, strlen(body));
    char *t = read_text(store_path("runs/%s", file));
    int ok = 0;
    if (t && json_str(t, "seed", s, sizeof s) && json_str(t, key, m, sizeof m)) {
        time_t now = time(NULL);
        struct tm g;
        gmtime_r(&now, &g);
        char cur[16];
        if (weekly) iso_week(&g, cur, sizeof cur);
        else strftime(cur, sizeof cur, "%Y-%m", &g);
        if (!strcmp(cur, m) && atoi(s) > 0) { snprintf(label, 16, "%s", m); *seed = atoi(s); ok = 1; }
    }
    free(t);
    return ok;
}

int online_monthly(char label[16], int *seed, int *playing) { return fetch_period("/api/monthly", "monthly.json", "month", 0, label, seed, playing); }
int online_weekly(char label[16], int *seed, int *playing) { return fetch_period("/api/weekly", "weekly.json", "week", 1, label, seed, playing); }

int online_weekly_days_left(void)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    int to_monday = 8 - (t.tm_wday ? t.tm_wday : 7);           /* days to the next Monday 00:00 UTC, counting today */
    return (int)(to_monday * 86400L - (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec)) / 86400;
}

int online_ensure_token(int slot)
{
    char *kind = read_text(store_path("runs/slot%d.online", slot));
    if (!kind) return 1;
    if (access(store_path("runs/slot%d.token", slot), F_OK) == 0) { free(kind); return 1; }
    kind[strcspn(kind, "\r\n ")] = 0;
    char id[48], req[240], reply[1024], token[512], preset[40] = "";
    install_id(id);
    char *pt = read_text(store_path("runs/slot%d.preset", slot));   /* a race preset's table, none for Original */
    if (pt) { pt[strcspn(pt, "\r\n \"\\")] = 0; snprintf(preset, sizeof preset, ",\"preset\":\"%.24s\"", pt); free(pt); }
    snprintf(req, sizeof req, "{\"install\":\"%s\",\"kind\":\"%s\"%s%s}", id, kind, game_field(), preset);
    free(kind);
    if (http("POST", "/api/start", req, reply, sizeof reply) != 200 || !json_str(reply, "token", token, sizeof token)) return 0;
    store_write(store_path("runs/slot%d.token", slot), token, strlen(token));
    return 1;
}

int online_ladder_json(int tab, const char *preset, char *out, size_t cap)
{
    static const char *const PATH[3] = {"/api/ladder?kind=original", "/api/ladder?kind=monthly", "/api/ladder?kind=weekly"};
    char path[120];
    if (tab < 0 || tab > 2) return 0;
    snprintf(path, sizeof path, "%s%s%s%.24s", PATH[tab], game_query('&'), tab == 0 && preset && *preset ? "&preset=" : "", tab == 0 && preset ? preset : "");
    return http("GET", path, NULL, out, cap) == 200;
}

int online_goals_json(char *out, size_t cap)
{
    char id[48], path[112];
    install_id(id);
    snprintf(path, sizeof path, "/api/goals?install=%s%s", id, game_query('&'));
    return http("GET", path, NULL, out, cap) == 200;
}

int online_goals_report(int slot, int64_t created, int online, const char *counters)
{
    char id[48], run[64], body[2048], reply[256];
    install_id(id);
    SmzRun r;
    if (store_run_load(slot, &r) == 0 && r.id[0]) snprintf(run, sizeof run, "%s", r.id);   /* the same run on every device */
    else snprintf(run, sizeof run, "%s%d-%lld", store_get_mode() == SMZ_MODE_SM ? "m" : store_get_mode() == SMZ_MODE_Z3 ? "z" : "s", slot, (long long)created);
    int n = snprintf(body, sizeof body, "{\"install\":\"%s\",\"run\":\"%s\",\"online\":%s,\"counts\":{", id, run, online ? "true" : "false");
    for (const char *p = counters; p && *p && n < (int)sizeof body - 64;) {
        const char *eq = strchr(p, '='), *end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        if (eq && eq < end && *p != '_') n += snprintf(body + n, sizeof body - (size_t)n, "%s\"%.*s\":%.*s", body[n - 1] == '{' ? "" : ",", (int)(eq - p), p, (int)(end - eq - 1), eq + 1);
        p = *end ? end + 1 : end;
    }
    char name[32], country[4];
    profile(name, country);   /* the name and flag last month's top 10 shows */
    n += snprintf(body + n, sizeof body - (size_t)n, "},\"player\":\"%s\"", name);
    if (country[0]) n += snprintf(body + n, sizeof body - (size_t)n, ",\"country\":\"%s\"", country);
    snprintf(body + n, sizeof body - (size_t)n, "}");
    return http("POST", "/api/goals/report", body, reply, sizeof reply) == 200;
}

int online_playing_ready(int slot)
{
    int kind = online_slot_kind(slot);
    return (kind == 2 || kind == 3) && access(store_path("runs/slot%d.token", slot), F_OK) == 0;
}

int online_playing_ping(int slot)
{
    int kind = online_slot_kind(slot);
    if (kind != 2 && kind != 3) return 0;   /* only ranked monthly and weekly runs are counted */
    char *tk = read_text(store_path("runs/slot%d.token", slot));
    if (!tk) return 0;
    tk[strcspn(tk, "\r\n \"\\")] = 0;
    char body[600], reply[128];
    snprintf(body, sizeof body, "{\"token\":\"%.540s\"}", tk);
    free(tk);
    return http("POST", "/api/playing", body, reply, sizeof reply) == 200;
}

void online_queue_finish(int slot, int64_t time_ms, int seed)
{
    if (access(store_path("runs/slot%d.token", slot), F_OK) != 0) return;
    char t[48];
    int n = snprintf(t, sizeof t, "%lld %d", (long long)time_ms, seed);
    store_write(store_path("runs/slot%d.finish", slot), t, (size_t)n);
}

static void daily_flush(void);

static void flush_game(void);

void online_flush(void)
{
    int was = store_thread_mode(SMZ_MODE_SMZ3);
    for (int m = 0; m < SMZ_MODE_COUNT; m++) {   /* each game's runs, whichever is entered */
        store_thread_mode(m);
        flush_game();
    }
    store_thread_mode(was);
}

static void flush_game(void)
{
    daily_flush();
    for (int slot = 1; slot <= UI_SLOTS; slot++) {
        char *ft = read_text(store_path("runs/slot%d.finish", slot));
        char *tk = read_text(store_path("runs/slot%d.token", slot));
        if (ft && tk) {
            tk[strcspn(tk, "\r\n ")] = 0;
            char name[32], country[4], reply[1024];
            static char body[16384];
            long long ms = 0;
            int seed = 0;
            sscanf(ft, "%lld %d", &ms, &seed);
            profile(name, country);
            int n = snprintf(body, sizeof body, "{\"token\":\"%s\",\"timeMs\":%lld,\"player\":\"%s\"", tk, ms, name);
            if (country[0]) n += snprintf(body + n, sizeof body - n, ",\"country\":\"%s\"", country);
            if (seed > 0) n += snprintf(body + n, sizeof body - n, ",\"seed\":\"%d\"", seed);
            n += snprintf(body + n, sizeof body - n, ",\"proof\":\"");
            char *pf = read_text(store_path("runs/slot%d.proof", slot));
            for (const char *c = pf ? pf : ""; *c && n < (int)sizeof body - 8; c++) {
                if (*c == '\n') { body[n++] = '\\'; body[n++] = 'n'; }
                else if (*c >= '0' && *c <= '9') body[n++] = *c;
                else if (*c == ' ') body[n++] = ' ';
            }
            free(pf);
            snprintf(body + n, sizeof body - n, "\"}");
            int code = http("POST", "/api/finish", body, reply, sizeof reply);
            if (code == 400) {
                char msg[1100];
                int m = snprintf(msg, sizeof msg, "slot %d: %s", slot, reply);
                store_write(store_path("runs/last_online_error.txt"), msg, (size_t)m);
            }
            if (code == 200 || code == 400) {
                remove(store_path("runs/slot%d.finish", slot));
                remove(store_path("runs/slot%d.proof", slot));
                remove(store_path("runs/slot%d.token", slot));
            }
        }
        free(ft); free(tk);
    }
}

/* ---------- the daily challenge ---------- */

int64_t online_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int online_daily_info(SmzDailyInfo *d)
{
    static char body[4096];
    char id[48], path[96];
    install_id(id);
    snprintf(path, sizeof path, "/api/daily?install=%s%s", id, game_query('&'));
    if (http("GET", path, NULL, body, sizeof body) != 200) { memset(d, 0, sizeof *d); d->loaded = -1; return -1; }
    return smz_daily_parse_info(body, online_now_ms(), d);
}

int online_daily_start(const char *window, SmzDailyRun *r, char *settings, size_t cap, char *err, size_t errcap)
{
    static char body[32768];
    char id[48], req[180];
    install_id(id);
    snprintf(req, sizeof req, "{\"install\":\"%s\",\"window\":\"%s\"%s}", id, window, game_field());
    int code = http("POST", "/api/daily/start", req, body, sizeof body);
    if (code == 200 && smz_daily_parse_start(body, online_now_ms(), r, settings, cap) == 0) return 0;
    char why[64] = "";
    json_str(body, "error", why, sizeof why);
    if (code < 0) snprintf(err, errcap, "No connection to the challenge server");
    else if (why[0]) snprintf(err, errcap, "Cannot start: %s", why);
    else snprintf(err, errcap, "Cannot start the challenge (%d)", code);
    return -1;
}

int online_daily_hints(SmzDailyRun *r)
{
    static char body[8192];
    char path[400];
    snprintf(path, sizeof path, "/api/daily/hints?token=%s", r->token);
    if (http("GET", path, NULL, body, sizeof body) != 200) return -1;
    return smz_daily_parse_hints(body, online_now_ms(), r);
}

int online_daily_load(SmzDailyRun *r)
{
    char *t = read_text(store_path("runs/daily.txt"));
    int rc = smz_daily_run_load(t, r);
    free(t);
    return rc;
}

void online_daily_save(const SmzDailyRun *r)
{
    static char text[8192];
    int n = smz_daily_run_text(r, text, sizeof text);
    if (n > 0) store_write(store_path("runs/daily.txt"), text, (size_t)n);
}

void online_daily_queue_finish(int64_t time_ms)
{
    char t[32];
    int n = snprintf(t, sizeof t, "%lld", (long long)time_ms);
    remove(store_path("runs/daily.result"));
    store_write(store_path("runs/daily.finish"), t, (size_t)n);
}

int online_daily_result(char *out, size_t cap)
{
    char *t = read_text(store_path("runs/daily.result"));
    if (!t) return 0;
    snprintf(out, cap, "%s", t);
    free(t);
    return 1;
}

static void daily_flush(void)
{
    char *ft = read_text(store_path("runs/daily.finish"));
    SmzDailyRun r;
    if (!ft || online_daily_load(&r)) { free(ft); return; }
    char name[32], country[4], reply[1024], line[160];
    static char body[16384];
    profile(name, country);
    int n = snprintf(body, sizeof body, "{\"token\":\"%s\",\"timeMs\":%lld,\"player\":\"%s\"", r.token, atoll(ft), name);
    free(ft);
    if (country[0]) n += snprintf(body + n, sizeof body - n, ",\"country\":\"%s\"", country);
    n += snprintf(body + n, sizeof body - n, ",\"proof\":\"");
    char *pf = read_text(store_path("runs/slot%d.proof", DAILY_SLOT));
    for (const char *c = pf ? pf : ""; *c && n < (int)sizeof body - 8; c++) {
        if (*c == '\n') { body[n++] = '\\'; body[n++] = 'n'; }
        else if ((*c >= '0' && *c <= '9') || *c == ' ') body[n++] = *c;
    }
    free(pf);
    snprintf(body + n, sizeof body - n, "\"}");
    int code = http("POST", "/api/daily/finish", body, reply, sizeof reply);
    if (code != 200 && code != 400) return;   /* no answer: tried again later */
    char why[64] = "", num[16];
    if (code == 400) { json_str(reply, "error", why, sizeof why); snprintf(line, sizeof line, "Not counted: %s", why[0] ? why : "refused"); }
    else if (strstr(reply, "\"held\":true")) snprintf(line, sizeof line, "Held for a check before it counts");
    else if (strstr(reply, "\"late\":true")) snprintf(line, sizeof line, "After the window closed: on the late list, no prize");
    else {
        const char *rk = strstr(reply, "\"rank\":"), *tk = strstr(reply, "\"tokens\":"), *bl = strstr(reply, "\"balance\":");
        snprintf(num, sizeof num, "%d", rk ? atoi(rk + 7) : 0);
        snprintf(line, sizeof line, "Rank %s for now  -  +%d rupeergies (%d)", rk ? num : "?", tk ? atoi(tk + 9) : 0, bl ? atoi(bl + 10) : 0);
    }
    store_write(store_path("runs/daily.result"), line, strlen(line));
    remove(store_path("runs/daily.finish"));
}

int online_daily_chat(const SmzDailyRun *r, int64_t since, char *out, size_t cap)
{
    char path[420];
    snprintf(path, sizeof path, "/api/daily/chat?token=%s&since=%lld", r->token, (long long)since);
    return http("GET", path, NULL, out, cap);
}

int online_daily_say(const SmzDailyRun *r, const char *text)
{
    char name[32], country[4], body[768], reply[256];
    profile(name, country);
    if (smz_daily_chat_body(r, name, country, text, body, sizeof body)) return -1;
    return http("POST", "/api/daily/chat", body, reply, sizeof reply);
}

void online_profile(char name[32], char country[4]) { profile(name, country); }

int online_http(const char *method, const char *path, const char *body, char *out, size_t cap) { return http(method, path, body, out, cap); }
void online_install_id(char out[48]) { install_id(out); }
