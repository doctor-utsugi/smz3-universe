#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include "pc_compat.h"
#include "pc_online.h"
#include "pc_store.h"
#include "pc_sync.h"
#include "smz3_json.h"
#include "smz3_sync.h"
#include <SDL.h>

#define DEFAULT_ROMM "https://romm.example.com"
#define DEFAULT_SERVER "https://smz3-universe.duckdns.org"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* one sync call at a time */
static SmzRomm g_romm;
static char g_user[24];
static int g_pending;   /* background uploads running */
static char g_link[8];  /* the link code typed */

/* ---- transport: curl, as pc_online does, but with binary bodies and headers ---- */

static int64_t now_ms(void) { return (int64_t)time(NULL) * 1000; }

#ifdef _WIN32
static int run_curl(const char *const *argv, char *code, size_t cap)
{
    char cmd[8192], res[600];
    int n = 0;
    for (int i = 0; argv[i] && n < (int)sizeof cmd - 4; i++) n += snprintf(cmd + n, sizeof cmd - (size_t)n, i ? " \"%s\"" : "%s", i ? argv[i] : "curl.exe");
    snprintf(res, sizeof res, "%s", store_path("tmp/sync%lu.code", (unsigned long)GetCurrentThreadId()));
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE h = CreateFileA(res, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    STARTUPINFOA si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h; si.hStdError = h; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(h);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, 180000);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    size_t len;
    char *t = (char *)store_read(res, &len);
    remove(res);
    if (!t) return -1;
    snprintf(code, cap, "%.*s", (int)(len < cap ? len : cap - 1), t);
    free(t);
    return ec == 0 ? 0 : -1;
}
#else
static int run_curl(const char *const *argv, char *code, size_t cap)
{
    int out[2];
    if (pipe(out)) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(out[1], 1);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) dup2(nul, 2);
        close(out[0]);
        unsetenv("LD_LIBRARY_PATH");   /* the AppImage's bundled libs break the system curl */
        unsetenv("LD_PRELOAD");
        execvp("curl", (char *const *)argv);
        _exit(127);
    }
    close(out[1]);
    size_t len = 0;
    ssize_t r;
    while (len + 1 < cap && (r = read(out[0], code + len, cap - 1 - len)) > 0) len += (size_t)r;
    code[len] = 0;
    close(out[0]);
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}
#endif

static int http(void *ctx, const char *method, const char *url, const char *headers, const uint8_t *body, size_t len, uint8_t **out, size_t *out_len)
{
    (void)ctx;
    static int seq;
    int id = __sync_add_and_fetch(&seq, 1);
    char bodyf[600], outf[600], at[620], hb[2048], code[32];
    snprintf(bodyf, sizeof bodyf, "%s", store_path("tmp/sync%d.body", id));
    snprintf(outf, sizeof outf, "%s", store_path("tmp/sync%d.out", id));
    snprintf(hb, sizeof hb, "%s", headers ? headers : "");
    const char *argv[48];
    int a = 0;
    argv[a++] = "curl"; argv[a++] = "-s"; argv[a++] = "-m"; argv[a++] = "120"; argv[a++] = "-X"; argv[a++] = method;
    for (char *h = strtok(hb, "\n"); h && a < 36; h = strtok(NULL, "\n")) { argv[a++] = "-H"; argv[a++] = h; }
    if (body) {
        if (store_write(bodyf, body, len)) return -1;
        snprintf(at, sizeof at, "@%s", bodyf);
        argv[a++] = "--data-binary"; argv[a++] = at;
    }
    argv[a++] = "-o"; argv[a++] = outf; argv[a++] = "-w"; argv[a++] = "%{http_code}"; argv[a++] = url; argv[a] = NULL;
    int rc = run_curl(argv, code, sizeof code);
    if (body) remove(bodyf);
    size_t n = 0;
    uint8_t *o = store_read(outf, &n);
    remove(outf);
    if (!o) { o = malloc(1); n = 0; }
    if (o) o[n] = 0;
    *out = o;
    *out_len = n;
    int status = atoi(code);
    if (getenv("SMZ3_SYNC_LOG")) fprintf(stderr, "sync: %s %s -> %d\n", method, url, rc || !status ? -1 : status);
    return rc || !status ? -1 : status;
}

/* ---- sign-in, kept in sync.txt ---- */

static void config_save(void)
{
    char t[1400];
    int n = snprintf(t, sizeof t, "romm=%s\ntoken=%s\ndevice=%s\nuser=%s\n", g_romm.base, g_romm.token, g_romm.device, g_user);
    store_write(store_path("sync.txt"), t, (size_t)n);
}

static void config_load(void)
{
    memset(&g_romm, 0, sizeof g_romm);
    g_romm.http = http;
    snprintf(g_romm.base, sizeof g_romm.base, "%s", getenv("SMZ3_ROMM") ? getenv("SMZ3_ROMM") : DEFAULT_ROMM);
    size_t n;
    char *t = (char *)store_read(store_path("sync.txt"), &n);
    if (!t) return;
    t[n] = 0;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "romm") && *v) snprintf(g_romm.base, sizeof g_romm.base, "%s", v);
        else if (!strcmp(line, "token")) snprintf(g_romm.token, sizeof g_romm.token, "%s", v);
        else if (!strcmp(line, "device")) snprintf(g_romm.device, sizeof g_romm.device, "%s", v);
        else if (!strcmp(line, "user")) snprintf(g_user, sizeof g_user, "%s", v);
    }
    free(t);
}

static const char *server_host(void) { const char *b = g_romm.base, *p = strstr(b, "://"); return p ? p + 3 : b; }

static void device_name(char *out, size_t cap)
{
    char host[64] = "";
#ifdef _WIN32
    const char *c = getenv("COMPUTERNAME");
    if (c) snprintf(host, sizeof host, "%s", c);
#else
    gethostname(host, sizeof host - 1);
#endif
    snprintf(out, cap, "PC %s", host[0] ? host : "");
}

static void sync_ctx(SmzSync *s)
{
    memset(s, 0, sizeof *s);
    s->romm = &g_romm;
    snprintf(s->server, sizeof s->server, "%s", getenv("SMZ3_SERVER") ? getenv("SMZ3_SERVER") : DEFAULT_SERVER);
    online_install_id(s->install);
    device_name(s->device, sizeof s->device);
    s->now_ms = now_ms();
}

/* ---- a run's files ---- */

typedef struct { int slot, mode; char rom[96]; } RunFiles;

/* The small files beside a run that travel inside its run text, as "x.<name>=<escaped text>" lines. */
static const char *const SIDE[] = {"online", "token", "proof", "discovery", "preset", "q1.goals", "q2.goals", "q3.goals", "q4.goals"};
#define SIDE_N (int)(sizeof SIDE / sizeof SIDE[0])

static const char *slot_file(const RunFiles *f, const char *ext) { return store_path("runs/slot%d.%s", f->slot, ext); }

static const char *kind_path(const RunFiles *f, int kind)
{
    switch (kind) {
    case SMZ_SYNC_SRM: return slot_file(f, "srm");
    case SMZ_SYNC_RESUME: return slot_file(f, "state");
    case SMZ_SYNC_Q1: case SMZ_SYNC_Q2: case SMZ_SYNC_Q3: case SMZ_SYNC_Q4: return store_path("runs/slot%d.q%d.state", f->slot, kind - SMZ_SYNC_Q1 + 1);
    case SMZ_SYNC_HELP: return slot_file(f, "help");
    case SMZ_SYNC_RECORD: return slot_file(f, "sync");
    case SMZ_SYNC_ROM: return store_path("roms/%s", f->rom);
    default: return NULL;
    }
}

static void esc(SjBuf *b, const char *t, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '\\') sj_add(b, "\\\\", 2);
        else if (t[i] == '\n') sj_add(b, "\\n", 2);
        else if (t[i] != '\r') sj_add(b, t + i, 1);
    }
}

static size_t unesc(char *t)
{
    size_t n = 0;
    for (char *p = t; *p; p++) {
        if (*p == '\\' && p[1]) { p++; t[n++] = *p == 'n' ? '\n' : *p; }
        else t[n++] = *p;
    }
    t[n] = 0;
    return n;
}

static uint8_t *files_read(void *ctx, int kind, size_t *len)
{
    RunFiles *f = ctx;
    store_thread_mode(f->mode);
    if (kind == SMZ_SYNC_RACE) return NULL;
    if (kind != SMZ_SYNC_RUN) return store_read(kind_path(f, kind), len);
    size_t n;
    char *t = (char *)store_read(slot_file(f, "run"), &n);
    if (!t) return NULL;
    t[n] = 0;
    SjBuf b = {0};
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n"))
        if (strncmp(line, "rom=", 4)) { sj_add(&b, line, strlen(line)); sj_add(&b, "\n", 1); }   /* where the ROM lies is this device's own */
    free(t);
    for (int i = 0; i < SIDE_N; i++) {
        char *s = (char *)store_read(slot_file(f, SIDE[i]), &n);
        if (!s) continue;
        sj_fmt(&b, "x.%s=", SIDE[i]);
        esc(&b, s, n);
        sj_add(&b, "\n", 1);
        free(s);
    }
    char *other = (char *)store_read(slot_file(f, "xother"), &n);
    if (other) { sj_add(&b, other, n); free(other); }
    if (b.failed) { sj_free(&b); return NULL; }
    *len = b.n;
    return (uint8_t *)b.p;
}

static int files_write(void *ctx, int kind, const uint8_t *data, size_t len)
{
    RunFiles *f = ctx;
    store_thread_mode(f->mode);
    if (kind == SMZ_SYNC_RACE) return 0;
    if (kind != SMZ_SYNC_RUN) {
        const char *p = kind_path(f, kind);
        if (!data) { remove(p); return 0; }
        return store_write(p, data, len);
    }
    char *t = malloc(len + 1);
    if (!t) return -1;
    memcpy(t, data, len);
    t[len] = 0;
    SjBuf run = {0}, other = {0};
    for (int i = 0; i < SIDE_N; i++) remove(slot_file(f, SIDE[i]));
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        if (!strncmp(line, "x.", 2)) {
            char *v = strchr(line, '=');
            if (!v) continue;
            int known = 0;
            for (int i = 0; i < SIDE_N; i++) if (!strncmp(line + 2, SIDE[i], (size_t)(v - line - 2)) && !SIDE[i][v - line - 2]) {
                known = 1;
                size_t n = unesc(v + 1);
                store_write(slot_file(f, SIDE[i]), v + 1, n);
            }
            if (!known) { sj_add(&other, line, strlen(line)); sj_add(&other, "\n", 1); }   /* another device's own (Android's marks): kept for it */
            continue;
        }
        sj_add(&run, line, strlen(line));
        sj_add(&run, "\n", 1);
    }
    free(t);
    if (other.n) store_write(slot_file(f, "xother"), other.p, other.n);
    else remove(slot_file(f, "xother"));
    sj_free(&other);
    sj_fmt(&run, "rom=%s\n", kind_path(f, SMZ_SYNC_ROM));
    int e = run.failed ? -1 : store_write(slot_file(f, "run"), run.p, run.n);
    sj_free(&run);
    return e;
}

/* The run in a slot as the sync sees it; 0 or -1 when the slot is empty. */
static int run_of(int slot, int mode, SmzRun *r, RunFiles *f, SmzSyncRun *sr, char **proof)
{
    int was = store_thread_mode(mode);
    int e = store_run_load(slot, r);
    store_thread_mode(was);
    if (e) return -1;
    memset(f, 0, sizeof *f);
    f->slot = slot; f->mode = mode;
    smz_run_rom_name(r->settings, r->seed, f->rom, sizeof f->rom);
    memset(sr, 0, sizeof *sr);
    sr->run_id = r->id; sr->rom_name = f->rom; sr->name = r->name; sr->play_ms = r->play_ms;
    was = store_thread_mode(mode);
    sr->online = online_slot_kind(slot) > 0;
    size_t n;
    *proof = sr->online ? (char *)store_read(store_path("runs/slot%d.proof", slot), &n) : NULL;
    if (*proof) (*proof)[n] = 0;
    sr->proof = *proof;
    store_thread_mode(was);
    return 0;
}

static int signed_in(void) { return g_romm.token[0] != 0; }

/* ---- background calls and what they leave for the menu ---- */

static volatile int g_login_state, g_list_done, g_msg_ready;   /* g_login_state: 0 idle, 1 waiting, 2 signed in, 3 failed */
static char g_msg[48], g_code[16], g_url[96];
static SmzRommLogin g_login;
static volatile int g_login_cancel;

static void say(const char *m) { snprintf(g_msg, sizeof g_msg, "%s", m); g_msg_ready = 1; }

static void *login_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    char id[48], name[48];
    online_install_id(id);
    device_name(name, sizeof name);
    SmzRomm r = g_romm;
    r.token[0] = 0;
    if (smz_romm_login_start(&r, id, name, "PC", SMZ3_VERSION, &g_login)) { say(r.err); g_login_state = 3; pthread_mutex_unlock(&g_lock); return NULL; }
    snprintf(g_code, sizeof g_code, "%s", g_login.user_code);
    snprintf(g_url, sizeof g_url, "%s", strstr(g_login.url, "://") ? strstr(g_login.url, "://") + 3 : g_login.url);
    g_login_state = 1;
    int p = 1;
    for (time_t end = time(NULL) + g_login.expires_in; p == 1 && !g_login_cancel && time(NULL) < end;) {
        SDL_Delay((Uint32)(g_login.interval > 0 ? g_login.interval : 5) * 1000);
        if (!g_login_cancel) p = smz_romm_login_poll(&r, &g_login);
    }
    if (p == 0 && !g_login_cancel) {
        smz_romm_whoami(&r, g_user, sizeof g_user);
        snprintf(g_romm.token, sizeof g_romm.token, "%s", r.token);
        snprintf(g_romm.device, sizeof g_romm.device, "%s", r.device);
        config_save();
        g_login_state = 2;
    } else {
        if (!g_login_cancel) say(p == 1 ? "The code expired: sign in again" : r.err);
        g_login_state = 3;
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

/* Brings the account's runs that are on no slot here into free slots, every game. */
/* A run here that RomM now keeps under other ids. */
static void keep_ids(int mode, int slot, const SmzRommNote *n)
{
    RunFiles f = {slot, mode, ""};
    SmzSyncFiles fs = {files_read, files_write, &f};
    smz_sync_record_ids(&fs, n->rom_id, n->note_id);
}

static void list_runs(void)
{
    SmzRommNote *notes;
    int count;
    if (smz_romm_notes(&g_romm, &notes, &count)) return;
    for (int i = 0; i < count; i++) {
        char letter = notes[i].run_id[0];
        int mode = letter == 'z' ? SMZ_MODE_Z3 : letter == 'm' ? SMZ_MODE_SM : SMZ_MODE_SMZ3, here = 0, free_slot = -1;
        int was = store_thread_mode(mode);
        for (int s = 1; s <= UI_SLOTS && !here; s++) {
            SmzRun r;
            if (smz_run_load(store_path("runs/slot%d.run", s), &r)) { if (free_slot < 0) free_slot = s; continue; }
            here = !strcmp(r.id, notes[i].run_id);
            if (here) keep_ids(mode, s, &notes[i]);
        }
        char *text = NULL, rom[96];
        if (!here && free_slot > 0 && !smz_sync_note_info(notes[i].content, NULL, 0, rom, sizeof rom, NULL, &text) && text && text[0]) {
            RunFiles f = {free_slot, mode, ""};
            snprintf(f.rom, sizeof f.rom, "%s", rom);
            SmzSyncFiles fs = {files_read, files_write, &f};
            SmzSyncRecord rec = {0};
            rec.rom_id = notes[i].rom_id;
            rec.note_id = notes[i].note_id;   /* revision 0: opening it brings everything down */
            if (!files_write(&f, SMZ_SYNC_RUN, (const uint8_t *)text, strlen(text))) smz_sync_record_save(&fs, &rec);
        }
        free(text);
        store_thread_mode(was);
    }
    smz_romm_notes_free(notes, count);
}

static void *list_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    if (signed_in()) {
        int who = smz_romm_whoami(&g_romm, NULL, 0);
        if (who == 1) { g_romm.token[0] = 0; config_save(); say("RomM signed this device out"); g_login_state = 3; }
        else if (who == 0) list_runs();
    }
    g_list_done = 1;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

static void start(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    if (!pthread_create(&t, NULL, fn, arg)) pthread_detach(t);
}

void sync_init(SmzUi *u)
{
    config_load();
    ui_cloud_enable(u, 1);
    if (signed_in()) { ui_cloud_state(u, UI_CLOUD_IN, g_user, server_host(), NULL, NULL); start(list_thread, NULL); }
    else ui_cloud_state(u, UI_CLOUD_OUT, NULL, server_host(), NULL, NULL);
}

int sync_poll(SmzUi *u)
{
    static int shown;
    if (g_login_state == 1 && shown != 1) { shown = 1; ui_cloud_state(u, UI_CLOUD_WAITING, NULL, server_host(), g_code, g_url); }
    if (g_login_state == 2) { g_login_state = 0; shown = 0; ui_cloud_state(u, UI_CLOUD_IN, g_user, server_host(), NULL, NULL); ui_cloud_msg(u, "Signed in"); start(list_thread, NULL); }
    if (g_login_state == 3) { g_login_state = 0; shown = 0; ui_cloud_state(u, UI_CLOUD_OUT, NULL, server_host(), NULL, NULL); }
    if (g_msg_ready) { g_msg_ready = 0; ui_cloud_msg(u, g_msg); }
    if (g_list_done) { g_list_done = 0; return 1; }
    return 0;
}

/* ---- opening and leaving a run ---- */

typedef struct { int slot, mode, force_pull, force_push, done, rc, held; SmzSyncResult res; } Job;

static void *open_thread(void *arg)
{
    Job *j = arg;
    pthread_mutex_lock(&g_lock);
    SmzRun r; RunFiles f; SmzSyncRun sr; char *proof;
    j->rc = SMZ_SYNC_OK;
    if (signed_in() && !run_of(j->slot, j->mode, &r, &f, &sr, &proof)) {
        SmzSync s;
        sync_ctx(&s);
        SmzSyncFiles fs = {files_read, files_write, &f};
        store_thread_mode(j->mode);
        if (sr.online && smz_sync_hold(&s, r.id, 1) < 0) {}   /* playing it here takes it over; offline it plays as before */
        j->rc = j->force_push ? smz_sync_push(&s, &sr, &fs, 1, &j->res) : smz_sync_pull(&s, &sr, &fs, j->force_pull, &j->res);
        if (j->rc == SMZ_SYNC_OK && j->res.unranked && sr.online) online_mark_run(j->slot, 0, 0, NULL);   /* files made outside SMZ3 */
        free(proof);
    }
    j->done = 1;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

static void when_text(int64_t ms, char *out, size_t cap)
{
    time_t t = (time_t)(ms / 1000);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    strftime(out, cap, "there on %d %b, %H:%M", &tm);
}

static int run_job(SmzUi *u, Job *j, void (*present)(void))
{
    j->mode = store_get_mode();
    pthread_t t;
    if (pthread_create(&t, NULL, open_thread, j)) return 0;
    int busy = 0;
    for (Uint32 since = SDL_GetTicks(); !j->done; SDL_Delay(30)) {
        if (SDL_GetTicks() - since > 400) { busy = 1; SDL_PumpEvents(); ui_busy(u, "Syncing with RomM..."); if (present) present(); }
    }
    pthread_join(t, NULL);
    if (busy) ui_idle(u);
    if (j->rc == SMZ_SYNC_CONFLICT) {
        char when[40];
        when_text(j->res.cloud_at, when, sizeof when);
        ui_cloud_conflict(u, j->slot - 1, j->res.cloud_dev, when);
        return 1;
    }
    if (j->rc == SMZ_SYNC_FAILED) { ui_idle(u); snprintf(u->notice, sizeof u->notice, "Sync: %.30s", j->res.err); }
    if (j->rc == SMZ_SYNC_OK && j->res.unranked) snprintf(u->notice, sizeof u->notice, "Synced from outside SMZ3: unranked");
    return 0;
}

int sync_open(SmzUi *u, int slot, void (*present)(void))
{
    if (!signed_in() || slot >= UI_SLOTS) return 0;   /* the daily and race slots stay on this device */
    Job j = {0};
    j.slot = slot + 1;
    return run_job(u, &j, present);
}

static void *left_thread(void *arg)
{
    Job *j = arg;
    pthread_mutex_lock(&g_lock);
    SmzRun r; RunFiles f; SmzSyncRun sr; char *proof;
    if (signed_in() && !run_of(j->slot, j->mode, &r, &f, &sr, &proof)) {
        SmzSync s;
        sync_ctx(&s);
        SmzSyncFiles fs = {files_read, files_write, &f};
        store_thread_mode(j->mode);
        j->rc = smz_sync_push(&s, &sr, &fs, 0, &j->res);
        if (getenv("SMZ3_SYNC_LOG")) fprintf(stderr, "sync: push slot %d -> %d %s\n", j->slot, j->rc, j->res.err);
        if (j->rc == SMZ_SYNC_CONFLICT) say("A run changed elsewhere too: open it to choose");
        else if (j->rc == SMZ_SYNC_HELD) say("Another device took this run over");
        else if (j->rc == SMZ_SYNC_FAILED) say(j->res.err);
        free(proof);
    }
    __sync_sub_and_fetch(&g_pending, 1);
    pthread_mutex_unlock(&g_lock);
    free(j);
    return NULL;
}

void sync_left(int slot)
{
    if (getenv("SMZ3_SYNC_LOG")) fprintf(stderr, "sync: left slot %d, signed in %d\n", slot + 1, signed_in());
    if (!signed_in() || slot >= UI_SLOTS) return;
    Job *j = calloc(1, sizeof *j);
    if (!j) return;
    j->slot = slot + 1;
    j->mode = store_get_mode();
    __sync_add_and_fetch(&g_pending, 1);
    pthread_t t;
    if (pthread_create(&t, NULL, left_thread, j)) { __sync_sub_and_fetch(&g_pending, 1); free(j); return; }
    pthread_detach(t);
}

void sync_own(int slot, int kind, const void *data, size_t len)
{
    RunFiles f = {slot + 1, store_get_mode(), ""};
    SmzSyncFiles fs = {files_read, files_write, &f};
    smz_sync_own(&fs, kind, data, len);
}

typedef struct { SmzSyncRecord rec; } Gone;

static void *delete_thread(void *arg)
{
    Gone *g = arg;
    pthread_mutex_lock(&g_lock);
    smz_sync_delete(&g_romm, &g->rec);
    __sync_sub_and_fetch(&g_pending, 1);
    pthread_mutex_unlock(&g_lock);
    free(g);
    return NULL;
}

void sync_deleted(int slot)
{
    if (!signed_in() || slot + 1 > UI_SLOTS) return;
    Gone *g = calloc(1, sizeof *g);
    if (!g) return;
    RunFiles f = {slot + 1, store_get_mode(), ""};
    SmzSyncFiles fs = {files_read, files_write, &f};
    smz_sync_record_load(&fs, &g->rec);
    if (!g->rec.note_id) { free(g); return; }
    __sync_add_and_fetch(&g_pending, 1);
    pthread_t t;
    if (pthread_create(&t, NULL, delete_thread, g)) { __sync_sub_and_fetch(&g_pending, 1); free(g); return; }
    pthread_detach(t);
}

/* ---- the Cloud sync screen ---- */

static void *link_thread(void *arg)
{
    int what = (int)(intptr_t)arg;
    SmzSync s;
    sync_ctx(&s);
    SmzRomm r = g_romm;   /* only its transport and err are used */
    s.romm = &r;
    if (what == CLOUD_CODE) {
        char code[8];
        if (smz_sync_player_code(&s, code)) say(r.err);
        else { char m[48]; snprintf(m, sizeof m, "On the other device enter %s", code); say(m); }
    } else if (what == CLOUD_LINK) say(smz_sync_player_link(&s, g_link) ? r.err : "Linked: one player on both devices");
    return NULL;
}

int sync_action(SmzUi *u, int what, void (*present)(void))
{
    switch (what) {
    case CLOUD_SIGN_IN:
        if (g_login_state == 1) break;
        g_login_cancel = 0;
        ui_cloud_msg(u, "Asking RomM for a code...");
        start(login_thread, NULL);
        break;
    case CLOUD_CANCEL:
        g_login_cancel = 1;
        ui_cloud_state(u, UI_CLOUD_OUT, NULL, server_host(), NULL, NULL);
        break;
    case CLOUD_SIGN_OUT:
        pthread_mutex_lock(&g_lock);
        g_romm.token[0] = g_romm.device[0] = g_user[0] = 0;
        config_save();
        pthread_mutex_unlock(&g_lock);
        ui_cloud_state(u, UI_CLOUD_OUT, NULL, server_host(), NULL, NULL);
        ui_cloud_msg(u, "Signed out: the runs here stay");
        break;
    case CLOUD_SYNC:
        ui_cloud_msg(u, "Syncing...");
        start(list_thread, NULL);
        break;
    case CLOUD_CODE:
        ui_cloud_msg(u, "Asking the server for a code...");
        start(link_thread, (void *)(intptr_t)CLOUD_CODE);
        break;
    case CLOUD_LINK:
        snprintf(g_link, sizeof g_link, "%s", u->cloud.link);
        start(link_thread, (void *)(intptr_t)CLOUD_LINK);
        break;
    case CLOUD_KEEP_HERE: case CLOUD_KEEP_CLOUD: {
        Job j = {0};
        j.slot = u->cloud.slot + 1;
        if (what == CLOUD_KEEP_HERE) j.force_push = 1; else j.force_pull = 1;
        if (run_job(u, &j, present) || j.rc != SMZ_SYNC_OK) return -1;
        return u->cloud.slot;
    }
    }
    return -1;
}

void sync_quit(void)
{
    for (Uint32 since = SDL_GetTicks(); g_pending > 0 && SDL_GetTicks() - since < 30000;) SDL_Delay(50);
}
