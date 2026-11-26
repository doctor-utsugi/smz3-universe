#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_json.h"
#include "smz3_md5.h"
#include "smz3_sync.h"

const char *const SMZ_SYNC_KIND_NAMES[SMZ_SYNC_FILES] = {"srm", "resume", "q1", "q2", "q3", "q4", "race"};

static void hash_of(const uint8_t *data, size_t len, char out[33])
{
    if (data) smz_md5_hex(data, len, out);
    else out[0] = 0;
}

void smz_sync_record_load(const SmzSyncFiles *fs, SmzSyncRecord *rec)
{
    memset(rec, 0, sizeof *rec);
    size_t n;
    char *t = (char *)fs->read(fs->ctx, SMZ_SYNC_RECORD, &n);
    if (!t) return;
    char *text = realloc(t, n + 1);
    if (!text) { free(t); return; }
    text[n] = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "rev")) rec->rev = atoi(v);
        else if (!strcmp(line, "rom")) rec->rom_id = atoi(v);
        else if (!strcmp(line, "note")) rec->note_id = atoi(v);
        else if (!strcmp(line, "unranked")) rec->unranked = atoi(v);
        else for (int k = 0; k < SMZ_SYNC_FILES; k++) if (!strcmp(line, SMZ_SYNC_KIND_NAMES[k])) {
            char *own = strchr(v, ','), *id = own ? strchr(own + 1, ',') : NULL;
            if (!id) break;
            *own++ = 0; *id++ = 0;
            snprintf(rec->f[k].base, 33, "%s", v);
            snprintf(rec->f[k].own, 33, "%s", own);
            rec->f[k].id = atoi(id);
        }
    }
    free(text);
}

int smz_sync_record_save(const SmzSyncFiles *fs, const SmzSyncRecord *rec)
{
    char text[1200];
    int n = snprintf(text, sizeof text, "SMZSYNC1\nrev=%d\nrom=%d\nnote=%d\nunranked=%d\n", rec->rev, rec->rom_id, rec->note_id, rec->unranked);
    for (int k = 0; k < SMZ_SYNC_FILES; k++)
        n += snprintf(text + n, sizeof text - (size_t)n, "%s=%s,%s,%d\n", SMZ_SYNC_KIND_NAMES[k], rec->f[k].base, rec->f[k].own, rec->f[k].id);
    return fs->write(fs->ctx, SMZ_SYNC_RECORD, (const uint8_t *)text, (size_t)n);
}

void smz_sync_record_ids(const SmzSyncFiles *fs, int rom_id, int note_id)
{
    SmzSyncRecord rec;
    smz_sync_record_load(fs, &rec);
    if (!rom_id || (rec.rom_id == rom_id && rec.note_id == note_id)) return;
    if (rec.rom_id != rom_id) {   /* its files there went with the old ROM */
        rec.rev = 0;
        for (int k = 0; k < SMZ_SYNC_FILES; k++) rec.f[k].id = 0;
    }
    rec.rom_id = rom_id;
    rec.note_id = note_id;
    smz_sync_record_save(fs, &rec);
}

void smz_sync_own(const SmzSyncFiles *fs, int kind, const uint8_t *data, size_t len)
{
    if (kind < 0 || kind >= SMZ_SYNC_FILES) return;
    SmzSyncRecord rec;
    smz_sync_record_load(fs, &rec);
    hash_of(data, len, rec.f[kind].own);
    smz_sync_record_save(fs, &rec);
}

void smz_sync_new_id(char letter, uint64_t entropy, char out[40])
{
    uint64_t x = entropy ^ 0x9E3779B97F4A7C15ull, y = entropy * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
    for (int i = 0; i < 4; i++) { x ^= x >> 31; x *= 0xD6E8FEB86659FD93ull; y ^= y >> 29; y *= 0x94D049BB133111EBull; }
    snprintf(out, 40, "%c%08x-%04x-4%03x-%04x-%012llx", letter, (unsigned)(x >> 32), (unsigned)(x >> 16 & 0xFFFF), (unsigned)(x & 0xFFF),
             (unsigned)(0x8000 | (y >> 48 & 0x3FFF)), (unsigned long long)(y & 0xFFFFFFFFFFFFull));
}

/* A call to the SMZ3 server: the status, -1 when it was not reached. */
static int server(SmzSync *s, const char *method, const char *path, const char *json, uint8_t **out)
{
    if (!s->server[0]) return -1;
    char url[384];
    snprintf(url, sizeof url, "%s%s", s->server, path);
    uint8_t *o = NULL;
    size_t n = 0;
    int st = s->romm->http(s->romm->ctx, method, url, json ? "Content-Type: application/json\n" : NULL,
                           (const uint8_t *)json, json ? strlen(json) : 0, &o, &n);
    if (out) *out = o;
    else free(o);
    return st;
}

int smz_sync_player_code(SmzSync *s, char code[8])
{
    char body[96];
    snprintf(body, sizeof body, "{\"install\":\"%s\"}", s->install);
    uint8_t *o = NULL;
    int st = server(s, "POST", "/api/player/code", body, &o);
    int ok = st == 200 && sj_str(sj_get((const char *)o, "code"), code, 8) == 6;
    if (!ok) snprintf(s->romm->err, sizeof s->romm->err, st < 0 ? "No connection to the SMZ3 server" : "The server gave no code (%d)", st);
    free(o);
    return ok ? 0 : -1;
}

int smz_sync_player_link(SmzSync *s, const char *code)
{
    char body[128];
    snprintf(body, sizeof body, "{\"install\":\"%s\",\"code\":\"%.6s\"}", s->install, code);
    uint8_t *o = NULL;
    int st = server(s, "POST", "/api/player/link", body, &o);
    if (st != 200) {
        char e[96] = "";
        if (o) sj_str(sj_get((const char *)o, "error"), e, sizeof e);
        snprintf(s->romm->err, sizeof s->romm->err, "%s", st < 0 ? "No connection to the SMZ3 server" : e[0] ? e : "The code was not accepted");
    }
    free(o);
    return st == 200 ? 0 : -1;
}

int smz_sync_hold(SmzSync *s, const char *run_id, int force)
{
    char body[160];
    snprintf(body, sizeof body, "{\"install\":\"%s\",\"run\":\"%s\"%s}", s->install, run_id, force ? ",\"force\":true" : "");
    int st = server(s, "POST", "/api/sync/hold", body, NULL);
    if (st == 409) return 1;
    if (st != 200) snprintf(s->romm->err, sizeof s->romm->err, "No connection to the SMZ3 server");
    return st == 200 ? 0 : -1;
}

void smz_sync_release(SmzSync *s, const char *run_id)
{
    char body[160];
    snprintf(body, sizeof body, "{\"install\":\"%s\",\"run\":\"%s\"}", s->install, run_id);
    server(s, "POST", "/api/sync/release", body, NULL);
}

/* Tells the server SMZ3 wrote this file: 0, 1 when another device holds the online run, -1 not reached. */
static int report_hash(SmzSync *s, const SmzSyncRun *run, int kind, const char *hash)
{
    SjBuf b = {0};
    sj_fmt(&b, "{\"install\":\"%s\",\"run\":\"%s\",\"hash\":\"%s\"", s->install, run->run_id, hash);
    if (run->online) {
        sj_fmt(&b, ",\"online\":true,\"playMs\":%lld", (long long)run->play_ms);
        if (run->proof && kind == SMZ_SYNC_RESUME) { sj_add(&b, ",\"proof\":", 9); sj_quote(&b, run->proof); }
    }
    sj_add(&b, "}", 1);
    int st = b.failed ? -1 : server(s, "POST", "/api/sync/hash", b.p, NULL);
    sj_free(&b);
    return st == 200 ? 0 : st == 409 ? 1 : -1;
}

/* 1 when the server knows the hash (SMZ3 wrote it), 0 when not, -1 when it was not reached. */
static int known_hash(SmzSync *s, const SmzSyncRun *run, const char *hash, int64_t *play_ms)
{
    char path[256];
    snprintf(path, sizeof path, "/api/sync/hash?install=%s&run=%s&hash=%s", s->install, run->run_id, hash);
    uint8_t *o = NULL;
    int st = server(s, "GET", path, NULL, &o);
    const char *k = st == 200 ? sj_get((const char *)o, "known") : NULL;
    int known = !k ? -1 : !strncmp(k, "true", 4);
    if (known == 1 && play_ms) *play_ms = sj_int(sj_get((const char *)o, "playMs"), 0);
    free(o);
    return known;
}

typedef struct { char h[33], dev[40]; int id, unranked; int64_t at; } NoteFile;
typedef struct { int rev; char dev[40]; int64_t at; NoteFile f[SMZ_SYNC_FILES]; char *run, *help, *name, *rom; } Note;

static void note_free(Note *n) { free(n->run); free(n->help); free(n->name); free(n->rom); memset(n, 0, sizeof *n); }

static int note_read(const char *content, Note *n)
{
    memset(n, 0, sizeof *n);
    if (!content || sj_int(sj_get(content, "v"), 0) != 1) return -1;
    n->rev = (int)sj_int(sj_get(content, "rev"), 0);
    n->at = sj_int(sj_get(content, "at"), 0);
    sj_str(sj_get(content, "dev"), n->dev, sizeof n->dev);
    n->run = sj_strdup(sj_get(content, "run"));
    n->help = sj_strdup(sj_get(content, "help"));
    n->name = sj_strdup(sj_get(content, "name"));
    n->rom = sj_strdup(sj_get(content, "rom"));
    const char *files = sj_get(content, "files");
    for (int k = 0; files && k < SMZ_SYNC_FILES; k++) {
        const char *f = sj_get(files, SMZ_SYNC_KIND_NAMES[k]);
        if (!f) continue;
        sj_str(sj_get(f, "h"), n->f[k].h, sizeof n->f[k].h);
        sj_str(sj_get(f, "dev"), n->f[k].dev, sizeof n->f[k].dev);
        n->f[k].id = (int)sj_int(sj_get(f, "id"), 0);
        n->f[k].at = sj_int(sj_get(f, "at"), 0);
        n->f[k].unranked = (int)sj_int(sj_get(f, "u"), 0);
    }
    return 0;
}

static char *note_write(const Note *n, const SmzSyncRun *run)
{
    SjBuf b = {0};
    sj_fmt(&b, "{\"v\":1,\"id\":\"%s\",\"rev\":%d,\"at\":%lld,\"dev\":", run->run_id, n->rev, (long long)n->at);
    sj_quote(&b, n->dev);
    sj_add(&b, ",\"name\":", 8); sj_quote(&b, run->name);
    sj_add(&b, ",\"rom\":", 7); sj_quote(&b, run->rom_name);
    sj_add(&b, ",\"run\":", 7); sj_quote(&b, n->run ? n->run : "");
    if (n->help) { sj_add(&b, ",\"help\":", 8); sj_quote(&b, n->help); }
    sj_add(&b, ",\"files\":{", 10);
    int first = 1;
    for (int k = 0; k < SMZ_SYNC_FILES; k++) {
        if (!n->f[k].h[0]) continue;
        sj_fmt(&b, "%s\"%s\":{\"h\":\"%s\",\"id\":%d,\"at\":%lld,\"u\":%d,\"dev\":", first ? "" : ",", SMZ_SYNC_KIND_NAMES[k], n->f[k].h,
               n->f[k].id, (long long)n->f[k].at, n->f[k].unranked);
        sj_quote(&b, n->f[k].dev);
        sj_add(&b, "}", 1);
        first = 0;
    }
    sj_add(&b, "}}", 2);
    if (b.failed) { sj_free(&b); return NULL; }
    return b.p;
}

int smz_sync_note_info(const char *content, char *name, size_t name_cap, char *rom_name, size_t rom_cap, int *rev, char **run_text)
{
    Note n;
    if (note_read(content, &n)) return -1;
    if (name) snprintf(name, name_cap, "%s", n.name ? n.name : "");
    if (rom_name) snprintf(rom_name, rom_cap, "%s", n.rom ? n.rom : "");
    if (rev) *rev = n.rev;
    if (run_text) { *run_text = n.run; n.run = NULL; }
    note_free(&n);
    return 0;
}

void smz_sync_delete(SmzRomm *r, const SmzSyncRecord *rec)
{
    if (!rec->rom_id || !rec->note_id) return;
    Note n = {0};
    SmzRommNote rn;
    if (smz_romm_note_by_id(r, rec->rom_id, rec->note_id, &rn) == 0) { note_read(rn.content, &n); smz_romm_note_free(&rn); }
    for (int k = 0; k < SMZ_SYNC_FILES; k++) {
        int kind = k == SMZ_SYNC_SRM ? 's' : 't';
        if (n.f[k].id) smz_romm_file_delete(r, kind, n.f[k].id);
        if (rec->f[k].id && rec->f[k].id != n.f[k].id) smz_romm_file_delete(r, kind, rec->f[k].id);
    }
    smz_romm_note_delete(r, rec->rom_id, rec->note_id);
    note_free(&n);
}

static int fail(SmzSyncResult *res, SmzRomm *r)
{
    res->result = r->status < 0 ? SMZ_SYNC_OFFLINE : SMZ_SYNC_FAILED;
    snprintf(res->err, sizeof res->err, "%s", r->err);
    return res->result;
}

/* The ROM's id in RomM, uploading the ROM when RomM lacks it. */
static int ensure_rom(SmzSync *s, const SmzSyncRun *run, const SmzSyncFiles *fs, SmzSyncRecord *rec, SmzSyncResult *res)
{
    if (rec->rom_id) return 0;
    size_t len;
    uint8_t *rom = fs->read(fs->ctx, SMZ_SYNC_ROM, &len);
    if (!rom) { snprintf(res->err, sizeof res->err, "The run's ROM is missing"); res->result = SMZ_SYNC_FAILED; return -1; }
    char md5[33];
    smz_md5_hex(rom, len, md5);
    int f = smz_romm_rom_find(s->romm, md5, &rec->rom_id);
    if (f == 1) f = smz_romm_rom_upload(s->romm, run->rom_name, rom, len, md5, &rec->rom_id);
    free(rom);
    if (f) { fail(res, s->romm); return -1; }
    return 0;
}

/* The note in RomM: 0 read (*found 1) or not there (*found 0), -1 on error. */
static int fetch_note(SmzSync *s, const SmzSyncRun *run, SmzSyncRecord *rec, Note *n, int *found, SmzSyncResult *res)
{
    SmzRommNote rn;
    int g = smz_romm_note_get(s->romm, rec->rom_id, run->run_id, &rn);
    memset(n, 0, sizeof *n);
    *found = 0;
    if (g < 0 && s->romm->status == 404) { rec->rom_id = rec->note_id = 0; return 0; }   /* the ROM left RomM */
    if (g < 0) return fail(res, s->romm), -1;
    if (g == 1) return 0;
    rec->note_id = rn.note_id;
    int bad = note_read(rn.content, n);
    smz_romm_note_free(&rn);
    if (bad) { snprintf(res->err, sizeof res->err, "The run note in RomM is not one SMZ3 can read"); res->result = SMZ_SYNC_FAILED; return -1; }
    *found = 1;
    return 0;
}

/* 1 when a file on this device differs from what it last synced. */
static int changed_here(const SmzSyncFiles *fs, const SmzSyncRecord *rec)
{
    for (int k = 0; k < SMZ_SYNC_FILES; k++) {
        size_t len;
        uint8_t *d = fs->read(fs->ctx, k, &len);
        char h[33];
        hash_of(d, len, h);
        free(d);
        if (d && strcmp(h, rec->f[k].base)) return 1;
    }
    return 0;
}

static void conflict(Note *n, SmzSyncResult *res)
{
    res->result = SMZ_SYNC_CONFLICT;
    snprintf(res->cloud_dev, sizeof res->cloud_dev, "%s", n->dev);
    res->cloud_at = n->at;
}

int smz_sync_pull(SmzSync *s, const SmzSyncRun *run, const SmzSyncFiles *fs, int force, SmzSyncResult *res)
{
    memset(res, 0, sizeof *res);
    SmzSyncRecord rec;
    smz_sync_record_load(fs, &rec);
    if (!rec.rom_id) rec.rom_id = run->rom_id;
    Note n;
    int found;
    for (int tries = 0;; tries++) {
        if (!rec.rom_id) {
            size_t len;
            uint8_t *rom = fs->read(fs->ctx, SMZ_SYNC_ROM, &len);
            char md5[33];
            hash_of(rom, len, md5);
            free(rom);
            int f = md5[0] ? smz_romm_rom_find(s->romm, md5, &rec.rom_id) : 1;
            if (f < 0) return fail(res, s->romm);
            if (f) return SMZ_SYNC_OK;   /* RomM has never seen this run */
        }
        if (fetch_note(s, run, &rec, &n, &found, res)) return res->result;
        if (rec.rom_id || tries) break;   /* a ROM gone from RomM: once more by its hash */
    }
    if (!rec.rom_id) return SMZ_SYNC_OK;
    if (!found || (n.rev == rec.rev && !force)) { note_free(&n); return SMZ_SYNC_OK; }
    if (!force && changed_here(fs, &rec)) { conflict(&n, res); note_free(&n); return res->result; }
    size_t len;
    uint8_t *rom = fs->read(fs->ctx, SMZ_SYNC_ROM, &len);
    if (!rom) {   /* a run made on another device: its ROM comes too */
        if (smz_romm_rom_download(s->romm, rec.rom_id, run->rom_name, &rom, &len) || fs->write(fs->ctx, SMZ_SYNC_ROM, rom, len)) {
            free(rom); note_free(&n);
            if (!res->err[0]) snprintf(res->err, sizeof res->err, "%s", s->romm->err[0] ? s->romm->err : "Could not store the ROM");
            res->result = SMZ_SYNC_FAILED;
            return res->result;
        }
    }
    free(rom);
    for (int k = 0; k < SMZ_SYNC_FILES; k++) {
        if (!n.f[k].h[0]) continue;
        uint8_t *d = fs->read(fs->ctx, k, &len);
        char h[33];
        hash_of(d, len, h);
        free(d);
        if (!strcmp(h, n.f[k].h)) { snprintf(rec.f[k].base, 33, "%s", h); rec.f[k].id = n.f[k].id; continue; }
        uint8_t *got = NULL;
        int g = smz_romm_file_get(s->romm, k == SMZ_SYNC_SRM ? 's' : 't', n.f[k].id, &got, &len);
        if (g == 1) continue;   /* removed on RomM's page */
        if (g < 0) { note_free(&n); return fail(res, s->romm); }
        hash_of(got, len, h);
        int64_t ms = 0;
        int known = known_hash(s, run, h, k == SMZ_SYNC_RESUME ? &ms : NULL);
        if (n.f[k].unranked || known == 0 || strcmp(h, n.f[k].h)) res->unranked = rec.unranked = 1;
        if (k == SMZ_SYNC_RESUME && ms) res->play_ms = ms;
        int w = fs->write(fs->ctx, k, got, len);
        free(got);
        if (w) { note_free(&n); snprintf(res->err, sizeof res->err, "Could not write the downloaded file"); res->result = SMZ_SYNC_FAILED; return res->result; }
        snprintf(rec.f[k].base, 33, "%s", h);
        snprintf(rec.f[k].own, 33, "%s", h);   /* checked above: an unknown file has already made the run unranked */
        rec.f[k].id = n.f[k].id;
        res->changed = 1;
    }
    if (n.run && n.run[0] && !fs->write(fs->ctx, SMZ_SYNC_RUN, (const uint8_t *)n.run, strlen(n.run))) res->changed = 1;
    if (n.help) fs->write(fs->ctx, SMZ_SYNC_HELP, (const uint8_t *)n.help, strlen(n.help));
    rec.rev = n.rev;
    note_free(&n);
    smz_sync_record_save(fs, &rec);
    return SMZ_SYNC_OK;
}

int smz_sync_push(SmzSync *s, const SmzSyncRun *run, const SmzSyncFiles *fs, int force, SmzSyncResult *res)
{
    memset(res, 0, sizeof *res);
    SmzSyncRecord rec;
    smz_sync_record_load(fs, &rec);
    if (ensure_rom(s, run, fs, &rec, res)) return res->result;
    Note n;
    int found;
    if (fetch_note(s, run, &rec, &n, &found, res)) return res->result;
    if (!rec.rom_id) {   /* its ROM left RomM, and the files with it: everything goes up again */
        for (int k = 0; k < SMZ_SYNC_FILES; k++) rec.f[k].id = 0;
        if (ensure_rom(s, run, fs, &rec, res) || fetch_note(s, run, &rec, &n, &found, res)) return res->result;
    }
    if (found && n.rev != rec.rev && !force) {
        if (changed_here(fs, &rec)) { conflict(&n, res); note_free(&n); return res->result; }
        note_free(&n);
        smz_sync_record_save(fs, &rec);
        return smz_sync_pull(s, run, fs, 0, res);   /* only RomM moved on */
    }
    if (!found) memset(&n, 0, sizeof n);
    char file[160];
    for (int k = 0; k < SMZ_SYNC_FILES; k++) {
        size_t len;
        uint8_t *d = fs->read(fs->ctx, k, &len);
        if (!d) continue;
        char h[33];
        hash_of(d, len, h);
        int same = !strcmp(h, rec.f[k].base) && found && !strcmp(h, n.f[k].h);
        if (same) { free(d); continue; }
        int unranked = rec.unranked || strcmp(h, rec.f[k].own) != 0;
        int id = !strcmp(n.f[k].h, "") ? 0 : n.f[k].id, e;
        if (k == SMZ_SYNC_SRM) {
            snprintf(file, sizeof file, "%s.srm", run->name);
            e = smz_romm_save_put(s->romm, rec.rom_id, run->run_id, file, d, len, h, &id);
        } else {
            snprintf(file, sizeof file, "%s.%s.state", run->name, SMZ_SYNC_KIND_NAMES[k]);
            e = smz_romm_state_put(s->romm, rec.rom_id, run->run_id, SMZ_SYNC_KIND_NAMES[k], file, d, len, &id);
        }
        free(d);
        if (e) { note_free(&n); return fail(res, s->romm); }
        if (!unranked && report_hash(s, run, k, h) == 1) { note_free(&n); res->result = SMZ_SYNC_HELD; return res->result; }
        snprintf(n.f[k].h, 33, "%s", h);
        snprintf(n.f[k].dev, sizeof n.f[k].dev, "%s", s->device);
        n.f[k].id = id;
        n.f[k].at = s->now_ms;
        n.f[k].unranked = unranked;
        snprintf(rec.f[k].base, 33, "%s", h);
        rec.f[k].id = id;
    }
    size_t len;
    free(n.run); free(n.help);
    n.run = (char *)fs->read(fs->ctx, SMZ_SYNC_RUN, &len);
    if (n.run) { char *t = realloc(n.run, len + 1); if (t) { n.run = t; n.run[len] = 0; } }
    n.help = (char *)fs->read(fs->ctx, SMZ_SYNC_HELP, &len);
    if (n.help) { char *t = realloc(n.help, len + 1); if (t) { n.help = t; n.help[len] = 0; } }
    n.rev = (found ? n.rev : rec.rev) + 1;
    n.at = s->now_ms;
    snprintf(n.dev, sizeof n.dev, "%s", s->device);
    char *content = note_write(&n, run);
    note_free(&n);
    if (!content) { snprintf(res->err, sizeof res->err, "out of memory"); res->result = SMZ_SYNC_FAILED; return res->result; }
    int e = smz_romm_note_put(s->romm, rec.rom_id, &rec.note_id, run->run_id, content);
    if (!e) {
        Note back;   /* the revision just written */
        note_read(content, &back);
        rec.rev = back.rev;
        note_free(&back);
    }
    free(content);
    if (e) return fail(res, s->romm);
    smz_sync_record_save(fs, &rec);
    return SMZ_SYNC_OK;
}
