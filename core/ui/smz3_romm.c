#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "smz3_json.h"
#include "smz3_romm.h"

#define BOUNDARY "smz3romm2f8c41d07a"
#define NOTE_PREFIX "smz3-run "
#define CHUNK (2u << 20)

static void fail(SmzRomm *r, const char *what, const uint8_t *answer)
{
    char detail[96] = "";
    const char *d = answer ? sj_get((const char *)answer, "detail") : NULL;
    if (d) sj_str(d, detail, sizeof detail);
    snprintf(r->err, sizeof r->err, "%s (%d%s%s)", what, r->status, detail[0] ? ": " : "", detail);
}

/* One request; extra holds more header lines. Returns the status (also kept in r->status). */
static int req(SmzRomm *r, const char *method, const char *path, const char *extra, const char *type,
               const uint8_t *body, size_t len, uint8_t **out, size_t *out_len)
{
    char url[512], headers[1600];
    snprintf(url, sizeof url, "%s%s", r->base, path);
    int n = snprintf(headers, sizeof headers, "Accept: application/json\n");
    if (r->token[0]) n += snprintf(headers + n, sizeof headers - (size_t)n, "Authorization: Bearer %s\n", r->token);
    if (type) n += snprintf(headers + n, sizeof headers - (size_t)n, "Content-Type: %s\n", type);
    if (extra) snprintf(headers + n, sizeof headers - (size_t)n, "%s", extra);
    uint8_t *o = NULL;
    size_t ol = 0;
    r->status = r->http(r->ctx, method, url, headers, body, len, &o, &ol);
    if (r->status < 0) snprintf(r->err, sizeof r->err, "%.120s is not reachable", r->base);
    if (out) { *out = o; if (out_len) *out_len = ol; }
    else free(o);
    return r->status;
}

int smz_romm_call(SmzRomm *r, const char *method, const char *path, const char *content_type, const uint8_t *body, size_t len,
                  uint8_t **out, size_t *out_len)
{
    return req(r, method, path, NULL, content_type, body, len, out, out_len);
}

static int json_call(SmzRomm *r, const char *method, const char *path, const char *json, uint8_t **out)
{
    return req(r, method, path, NULL, json ? "application/json" : NULL, (const uint8_t *)json, json ? strlen(json) : 0, out, NULL);
}

int smz_romm_login_start(SmzRomm *r, const char *client_id, const char *device_name, const char *platform, const char *version, SmzRommLogin *l)
{
    static const char *const SCOPES[] = {"me.read", "roms.read", "roms.write", "roms.user.read", "roms.user.write", "assets.read",
                                         "assets.write", "platforms.read", "devices.read", "devices.write", "tasks.run"};
    SjBuf b = {0};
    sj_add(&b, "{\"client_device_identifier\":", 28); sj_quote(&b, client_id);
    sj_add(&b, ",\"name\":", 8); sj_quote(&b, device_name);
    sj_add(&b, ",\"client\":\"SMZ3 Universe\",\"platform\":", 37); sj_quote(&b, platform);
    sj_add(&b, ",\"client_version\":", 18); sj_quote(&b, version);
    sj_add(&b, ",\"requested_scopes\":[", 21);
    for (size_t i = 0; i < sizeof SCOPES / sizeof SCOPES[0]; i++) { if (i) sj_add(&b, ",", 1); sj_quote(&b, SCOPES[i]); }
    sj_add(&b, "]}", 2);
    uint8_t *o = NULL;
    r->token[0] = 0;
    int st = b.failed ? -1 : json_call(r, "POST", "/api/auth/device/init", b.p, &o);
    sj_free(&b);
    memset(l, 0, sizeof *l);
    const char *j = (const char *)o;
    char path[200] = "";
    if ((st != 200 && st != 201) || sj_str(sj_get(j, "device_code"), l->device_code, sizeof l->device_code) < 0 ||
        sj_str(sj_get(j, "user_code"), l->user_code, sizeof l->user_code) < 0) {
        if (st >= 0) fail(r, "RomM refused the sign-in", o);
        free(o);
        return -1;
    }
    sj_str(sj_get(j, "verification_path"), path, sizeof path);
    snprintf(l->url, sizeof l->url, "%.120s%.120s", r->base, path[0] ? path : "/pair/device");
    l->interval = (int)sj_int(sj_get(j, "interval"), 5);
    l->expires_in = (int)sj_int(sj_get(j, "expires_in"), 600);
    free(o);
    return 0;
}

int smz_romm_login_poll(SmzRomm *r, const SmzRommLogin *l)
{
    SjBuf b = {0};
    sj_add(&b, "{\"device_code\":", 15); sj_quote(&b, l->device_code); sj_add(&b, "}", 1);
    uint8_t *o = NULL;
    int st = b.failed ? -1 : json_call(r, "POST", "/api/auth/device/token", b.p, &o);
    sj_free(&b);
    const char *j = (const char *)o;
    if ((st == 200 || st == 201) && sj_str(sj_get(j, "access_token"), r->token, sizeof r->token) > 0) {
        sj_str(sj_get(j, "device_id"), r->device, sizeof r->device);
        free(o);
        return 0;
    }
    char detail[64] = "";
    if (j) sj_str(sj_get(j, "detail"), detail, sizeof detail);
    if (j && !detail[0]) sj_str(sj_get(j, "error"), detail, sizeof detail);
    int waiting = st < 0 || strstr(detail, "pending") || strstr(detail, "slow");
    if (!waiting) fail(r, "the sign-in was not approved", o);
    free(o);
    return waiting ? 1 : -1;
}

int smz_romm_whoami(SmzRomm *r, char *user, size_t cap)
{
    uint8_t *o = NULL;
    int st = json_call(r, "GET", "/api/users/me", NULL, &o);
    if (st == 200 && user) sj_str(sj_get((const char *)o, "username"), user, cap);
    free(o);
    return st == 200 ? 0 : st == 401 || st == 403 ? 1 : -1;
}

static int platform_id(SmzRomm *r)
{
    if (r->platform) return r->platform;
    uint8_t *o = NULL;
    if (json_call(r, "GET", "/api/platforms", NULL, &o) == 200)
        for (const char *e = sj_first((const char *)o); e; e = sj_next(e)) {
            char slug[32];
            if (sj_str(sj_get(e, "slug"), slug, sizeof slug) > 0 && !strcmp(slug, SMZ_ROMM_SNES)) { r->platform = (int)sj_int(sj_get(e, "id"), 0); break; }
        }
    free(o);
    if (!r->platform) snprintf(r->err, sizeof r->err, "RomM has no SNES platform");
    return r->platform;
}

int smz_romm_rom_find(SmzRomm *r, const char *md5, int *rom_id)
{
    char path[96];
    snprintf(path, sizeof path, "/api/roms/by-hash?md5_hash=%s", md5);
    uint8_t *o = NULL;
    int st = json_call(r, "GET", path, NULL, &o);
    if (st == 200) *rom_id = (int)sj_int(sj_get((const char *)o, "id"), 0);
    if (st != 200 && st != 404) fail(r, "RomM could not look the ROM up", o);
    free(o);
    return st == 200 && *rom_id ? 0 : st == 404 ? 1 : -1;
}

static void wait_s(int s)
{
    for (time_t end = time(NULL) + s; time(NULL) < end;) {}
}

int smz_romm_rom_upload(SmzRomm *r, const char *file_name, const uint8_t *rom, size_t len, const char *md5, int *rom_id)
{
    int pid = platform_id(r);
    if (!pid) return -1;
    unsigned chunks = (unsigned)((len + CHUNK - 1) / CHUNK);
    char extra[600], path[160], upload[80];
    snprintf(extra, sizeof extra, "x-upload-platform: %d\nx-upload-filename: %s\nx-upload-total-size: %zu\nx-upload-total-chunks: %u\n", pid, file_name, len, chunks);
    uint8_t *o = NULL;
    int st = req(r, "POST", "/api/roms/upload/start", extra, NULL, NULL, 0, &o, NULL);
    if ((st != 200 && st != 201) || sj_str(sj_get((const char *)o, "upload_id"), upload, sizeof upload) <= 0) {
        fail(r, "RomM refused the ROM upload", o);
        free(o);
        return -1;
    }
    free(o);
    for (unsigned c = 0; c < chunks; c++) {
        size_t off = (size_t)c * CHUNK, n = len - off < CHUNK ? len - off : CHUNK;
        snprintf(path, sizeof path, "/api/roms/upload/%s", upload);
        snprintf(extra, sizeof extra, "x-chunk-index: %u\n", c);
        st = req(r, "PUT", path, extra, "application/octet-stream", rom + off, n, &o, NULL);
        if (st < 200 || st > 299) { fail(r, "the ROM upload broke off", o); free(o); return -1; }
        free(o);
    }
    snprintf(path, sizeof path, "/api/roms/upload/%s/complete", upload);
    st = req(r, "POST", path, NULL, NULL, NULL, 0, &o, NULL);
    if (st < 200 || st > 299) { fail(r, "RomM could not finish the ROM upload", o); free(o); return -1; }
    free(o);
    char scan[64];
    snprintf(scan, sizeof scan, "{\"platforms\":[%d],\"type\":\"quick\"}", pid);
    json_call(r, "POST", "/api/tasks/scan", scan, NULL);   /* RomM lists an uploaded file only once a scan has seen it */
    for (int tries = 0; tries < 30; tries++) {
        int f = smz_romm_rom_find(r, md5, rom_id);
        if (f <= 0) return f;
        wait_s(2);
    }
    snprintf(r->err, sizeof r->err, "RomM did not list the uploaded ROM");
    return -1;
}

int smz_romm_rom_download(SmzRomm *r, int rom_id, const char *file_name, uint8_t **out, size_t *len)
{
    char path[256];
    snprintf(path, sizeof path, "/api/roms/%d/content/%s", rom_id, file_name);
    int st = req(r, "GET", path, NULL, NULL, NULL, 0, out, len);
    if (st != 200) { fail(r, "RomM did not send the ROM", NULL); free(*out); *out = NULL; return -1; }
    return 0;
}

static int note_parse(const char *e, int rom_id, SmzRommNote *n)
{
    char title[96];
    memset(n, 0, sizeof *n);
    if (sj_str(sj_get(e, "title"), title, sizeof title) <= 0 || strncmp(title, NOTE_PREFIX, strlen(NOTE_PREFIX))) return -1;
    snprintf(n->run_id, sizeof n->run_id, "%s", title + strlen(NOTE_PREFIX));
    n->rom_id = rom_id;
    n->note_id = (int)sj_int(sj_get(e, "id"), 0);
    sj_str(sj_get(e, "updated_at"), n->updated, sizeof n->updated);
    n->content = sj_strdup(sj_get(e, "content"));
    return n->content ? 0 : -1;
}

static int rom_notes(SmzRomm *r, int rom_id, const char *run_id, SmzRommNote **list, int *count)
{
    char path[160];
    snprintf(path, sizeof path, "/api/roms/%d/notes?search=%s%s", rom_id, "smz3-run", "");
    uint8_t *o = NULL;
    int st = json_call(r, "GET", path, NULL, &o);
    if (st != 200) { fail(r, "RomM did not list the run notes", o); free(o); return -1; }
    for (const char *e = sj_first((const char *)o); e; e = sj_next(e)) {
        SmzRommNote n;
        if (note_parse(e, rom_id, &n)) continue;
        if (run_id && strcmp(n.run_id, run_id)) { smz_romm_note_free(&n); continue; }
        SmzRommNote *grown = realloc(*list, sizeof **list * (size_t)(*count + 1));
        if (!grown) { smz_romm_note_free(&n); break; }
        *list = grown;
        (*list)[(*count)++] = n;
    }
    free(o);
    return 0;
}

int smz_romm_notes(SmzRomm *r, SmzRommNote **notes, int *count)
{
    *notes = NULL;
    *count = 0;
    int pid = platform_id(r);
    if (!pid) return -1;
    for (int offset = 0;; offset += 200) {
        char path[160];
        snprintf(path, sizeof path, "/api/roms?platform_ids=%d&limit=200&offset=%d", pid, offset);
        uint8_t *o = NULL;
        if (json_call(r, "GET", path, NULL, &o) != 200) { fail(r, "RomM did not list the ROMs", o); free(o); return -1; }
        int seen = 0;
        for (const char *e = sj_first(sj_get((const char *)o, "items")); e; e = sj_next(e), seen++) {
            const char *hn = sj_get(e, "has_notes");
            if (!hn || strncmp(hn, "true", 4)) continue;
            int before = *count, rom_id = (int)sj_int(sj_get(e, "id"), 0);
            if (rom_notes(r, rom_id, NULL, notes, count)) { free(o); return -1; }
            for (int i = before; i < *count; i++) sj_str(sj_get(e, "fs_name"), (*notes)[i].rom_name, sizeof (*notes)[i].rom_name);
        }
        long long total = sj_int(sj_get((const char *)o, "total"), 0);
        free(o);
        if (seen < 200 || offset + 200 >= total) return 0;
    }
}

int smz_romm_note_get(SmzRomm *r, int rom_id, const char *run_id, SmzRommNote *n)
{
    SmzRommNote *list = NULL;
    int count = 0;
    if (rom_notes(r, rom_id, run_id, &list, &count)) return -1;
    if (!count) { free(list); return 1; }
    *n = list[0];
    for (int i = 1; i < count; i++) smz_romm_note_free(&list[i]);
    free(list);
    return 0;
}

int smz_romm_note_by_id(SmzRomm *r, int rom_id, int note_id, SmzRommNote *n)
{
    SmzRommNote *list = NULL;
    int count = 0, found = 1;
    if (rom_notes(r, rom_id, NULL, &list, &count)) return -1;
    for (int i = 0; i < count; i++) {
        if (found && list[i].note_id == note_id) { *n = list[i]; found = 0; }
        else smz_romm_note_free(&list[i]);
    }
    free(list);
    return found;
}

void smz_romm_note_free(SmzRommNote *n) { free(n->content); n->content = NULL; }

void smz_romm_notes_free(SmzRommNote *notes, int count)
{
    for (int i = 0; i < count; i++) smz_romm_note_free(&notes[i]);
    free(notes);
}

int smz_romm_note_put(SmzRomm *r, int rom_id, int *note_id, const char *run_id, const char *content)
{
    SjBuf b = {0};
    char title[96], path[96];
    snprintf(title, sizeof title, NOTE_PREFIX "%s", run_id);
    sj_add(&b, "{\"title\":", 9); sj_quote(&b, title);
    sj_add(&b, ",\"content\":", 11); sj_quote(&b, content);
    sj_add(&b, ",\"is_public\":false,\"tags\":[\"smz3\"]}", 35);
    if (*note_id) snprintf(path, sizeof path, "/api/roms/%d/notes/%d", rom_id, *note_id);
    else snprintf(path, sizeof path, "/api/roms/%d/notes", rom_id);
    uint8_t *o = NULL;
    int st = b.failed ? -1 : json_call(r, *note_id ? "PUT" : "POST", path, b.p, &o);
    sj_free(&b);
    if (st != 200 && st != 201) { fail(r, "RomM did not keep the run note", o); free(o); return -1; }
    if (!*note_id) *note_id = (int)sj_int(sj_get((const char *)o, "id"), 0);
    free(o);
    return *note_id ? 0 : -1;
}

int smz_romm_note_delete(SmzRomm *r, int rom_id, int note_id)
{
    char path[96];
    snprintf(path, sizeof path, "/api/roms/%d/notes/%d", rom_id, note_id);
    int st = json_call(r, "DELETE", path, NULL, NULL);
    return st == 200 || st == 204 || st == 404 ? 0 : -1;
}

/* A multipart/form-data body holding one file. */
static uint8_t *multipart(const char *field, const char *file_name, const uint8_t *data, size_t len, size_t *out_len)
{
    SjBuf b = {0};
    sj_fmt(&b, "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n", field, file_name);
    sj_add(&b, data, len);
    sj_add(&b, "\r\n--" BOUNDARY "--\r\n", 4 + strlen(BOUNDARY) + 4);
    if (b.failed) { sj_free(&b); return NULL; }
    *out_len = b.n;
    return (uint8_t *)b.p;
}

static int upload_file(SmzRomm *r, const char *method, const char *path, const char *field, const char *file_name,
                       const uint8_t *data, size_t len, int *id)
{
    size_t n;
    uint8_t *body = multipart(field, file_name, data, len, &n), *o = NULL;
    if (!body) { snprintf(r->err, sizeof r->err, "out of memory"); return -1; }
    int st = req(r, method, path, NULL, "multipart/form-data; boundary=" BOUNDARY, body, n, &o, NULL);
    free(body);
    if (st != 200 && st != 201) { fail(r, "RomM did not keep the file", o); free(o); return -1; }
    *id = (int)sj_int(sj_get((const char *)o, "id"), *id);
    free(o);
    return 0;
}

int smz_romm_save_put(SmzRomm *r, int rom_id, const char *run_id, const char *file_name, const uint8_t *data, size_t len,
                      const char *md5, int *save_id)
{
    char path[320];
    if (*save_id) {
        snprintf(path, sizeof path, "/api/saves/%d?device_id=%s&content_hash=%s", *save_id, r->device, md5);
        if (!upload_file(r, "PUT", path, "saveFile", file_name, data, len, save_id)) return 0;
        if (r->status != 404) return -1;
        *save_id = 0;   /* deleted on RomM's page: make it again */
    }
    snprintf(path, sizeof path, "/api/saves?rom_id=%d&emulator=" SMZ_ROMM_EMULATOR "&slot=%s&device_id=%s&content_hash=%s&overwrite=true",
             rom_id, run_id, r->device, md5);
    return upload_file(r, "POST", path, "saveFile", file_name, data, len, save_id);
}

int smz_romm_state_put(SmzRomm *r, int rom_id, const char *run_id, const char *kind, const char *file_name,
                       const uint8_t *data, size_t len, int *state_id)
{
    char path[200];
    if (*state_id) {
        snprintf(path, sizeof path, "/api/states/%d", *state_id);
        if (!upload_file(r, "PUT", path, "stateFile", file_name, data, len, state_id)) return 0;
        if (r->status != 404) return -1;
        *state_id = 0;
    }
    snprintf(path, sizeof path, "/api/states?rom_id=%d&emulator=" SMZ_ROMM_EMULATOR, rom_id);
    if (upload_file(r, "POST", path, "stateFile", file_name, data, len, state_id)) return -1;
    char labels[160];
    snprintf(labels, sizeof labels, "{\"labels\":[\"%s\",\"%s\"]}", run_id, kind);
    snprintf(path, sizeof path, "/api/states/%d/labels", *state_id);
    if (json_call(r, "PUT", path, labels, NULL) != 200) { fail(r, "RomM did not label the state", NULL); return -1; }
    return 0;
}

int smz_romm_file_get(SmzRomm *r, int kind, int id, uint8_t **out, size_t *len)
{
    char path[160];
    if (kind == 's') snprintf(path, sizeof path, "/api/saves/%d/content?device_id=%s", id, r->device);
    else snprintf(path, sizeof path, "/api/states/%d/content", id);
    int st = req(r, "GET", path, NULL, NULL, NULL, 0, out, len);
    if (st == 200) return 0;
    free(*out);
    *out = NULL;
    if (st == 404) return 1;
    fail(r, "RomM did not send the file", NULL);
    return -1;
}

int smz_romm_file_delete(SmzRomm *r, int kind, int id)
{
    char body[48];
    snprintf(body, sizeof body, kind == 's' ? "{\"saves\":[%d]}" : "{\"states\":[%d]}", id);
    int st = json_call(r, "POST", kind == 's' ? "/api/saves/delete" : "/api/states/delete", body, NULL);
    return st == 200 || st == 404 ? 0 : -1;
}
