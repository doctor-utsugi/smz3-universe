#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "pc_compat.h"
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "pc_store.h"
#include "pc_ips.h"
#include "smz3_md5.h"

static void mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
}

/* The folder of the running program ("" when unknown). */
const char *store_exe_dir(void)
{
    static char exe[512];
    if (exe[0]) return exe;
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe - 1);
    for (DWORD i = 0; i < n; i++) if (exe[i] == '\\') exe[i] = '/';
#elif defined(__APPLE__)
    uint32_t sz = sizeof exe - 1;
    ssize_t n = _NSGetExecutablePath(exe, &sz) == 0 ? (ssize_t)strlen(exe) : -1;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
#endif
    if (n <= 0) { exe[0] = 0; return exe; }
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    return exe;
}

const char *store_dir(void)
{
    static char dir[768];
    if (dir[0]) return dir;
    char exe[512];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe - 1);
    for (DWORD i = 0; i < n; i++) if (exe[i] == '\\') exe[i] = '/';
#elif defined(__APPLE__)
    uint32_t sz = sizeof exe - 1;
    ssize_t n = _NSGetExecutablePath(exe, &sz) == 0 ? (ssize_t)strlen(exe) : -1;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
#endif
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            char marker[600];
            snprintf(marker, sizeof marker, "%s/portable.txt", exe);
            if (access(marker, F_OK) == 0) { snprintf(dir, sizeof dir, "%s/data", exe); return dir; }
        }
    }
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        snprintf(dir, sizeof dir, "%s/smz3", appdata);
        for (char *c = dir; *c; c++) if (*c == '\\') *c = '/';
        return dir;
    }
#endif
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
#ifdef __APPLE__
    if (!(xdg && *xdg)) { snprintf(dir, sizeof dir, "%s/Library/Application Support/smz3", home ? home : "."); return dir; }
#endif
    if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/smz3", xdg);
    else snprintf(dir, sizeof dir, "%s/.local/share/smz3", home ? home : ".");
    return dir;
}

const char *store_path(const char *fmt, ...)
{
    static _Thread_local char path[1024];   /* one per thread: the online requests run beside the menu */
    char rel[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rel, sizeof rel, fmt, ap);
    va_end(ap);
    snprintf(path, sizeof path, "%s/%s", store_dir(), rel);
    mkdirs(path);
    return path;
}

uint8_t *store_read(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int store_write(const char *path, const void *data, size_t len)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
#ifdef _WIN32
    /* rename() refuses to replace an existing file on Windows */
    if (ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
    remove(tmp);
    return -1;
#else
    return ok && rename(tmp, path) == 0 ? 0 : -1;
#endif
}

void store_roms_get(char sm[512], char z3[512])
{
    sm[0] = z3[0] = 0;
    size_t n;
    char *t = (char *)store_read(store_path("config.txt"), &n);
    if (!t) return;
    t[n] = 0;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        if (!strncmp(line, "sm=", 3)) snprintf(sm, 512, "%s", line + 3);
        else if (!strncmp(line, "z3=", 3)) snprintf(z3, 512, "%s", line + 3);
    }
    free(t);
}

void store_roms_set(const char *sm, const char *z3)
{
    char text[1100], a[PATH_MAX], b[PATH_MAX];
    int n = snprintf(text, sizeof text, "sm=%s\nz3=%s\n", realpath(sm, a) ? a : sm, realpath(z3, b) ? b : z3);
    store_write(store_path("config.txt"), text, (size_t)n);
}

static uint8_t *load_rom(const char *path, size_t want, const char *md5, const char *what, char *err, size_t errcap)
{
    size_t n;
    uint8_t *rom = path && *path ? store_read(path, &n) : NULL;
    if (!rom) { snprintf(err, errcap, "cannot read the %s ROM (%s)", what, path && *path ? path : "path not set"); return NULL; }
    uint8_t *start = rom;
    if (n % 0x8000 == 0x200) { start += 0x200; n -= 0x200; }
    char hex[33];
    if (n != want) { snprintf(err, errcap, "the %s ROM has the wrong size", what); free(rom); return NULL; }
    smz_md5_hex(start, n, hex);
    if (strcmp(hex, md5)) { snprintf(err, errcap, "the %s ROM is not the revision the randomizer needs (md5 %s)", what, hex); free(rom); return NULL; }
    uint8_t *out = malloc(n);
    memcpy(out, start, n);
    free(rom);
    return out;
}

int store_load_base(uint8_t **sm, uint8_t **z3, char *err, size_t errcap)
{
    char smp[512], z3p[512];
    store_roms_get(smp, z3p);
    *sm = load_rom(smp, 0x300000, SMZ_MD5_SM, "Super Metroid (JU)", err, errcap);
    if (!*sm) return -1;
    *z3 = load_rom(z3p, 0x100000, SMZ_MD5_Z3, "A Link to the Past (JP v1.0)", err, errcap);
    if (!*z3) { free(*sm); *sm = NULL; return -1; }
    return 0;
}

const uint8_t *store_ips(void *user, const char *name, size_t *len)
{
    (void)user;
    for (const PcIps *p = PC_IPS; p->name; p++)
        if (!strcmp(p->name, name)) { *len = p->len; return p->data; }
    return NULL;
}

int store_run_load(int slot, SmzRun *r) { return smz_run_load(store_path("runs/slot%d.run", slot), r); }
int store_run_save(int slot, const SmzRun *r) { return smz_run_save(store_path("runs/slot%d.run", slot), r); }

void store_run_delete(int slot)
{
    SmzRun r;
    if (store_run_load(slot, &r) == 0 && r.rom_path[0]) remove(r.rom_path);
    remove(store_path("runs/slot%d.run", slot));
    remove(store_path("runs/slot%d.srm", slot));
    remove(store_path("runs/slot%d.state", slot));
}

int store_free_slot(void)
{
    for (int i = 1; i <= UI_SLOTS; i++) if (access(store_path("runs/slot%d.run", i), F_OK) != 0) return i;
    return -1;
}

void store_fill_slots(SmzUi *u)
{
    ui_set_slot_count(u, UI_SLOTS);
    for (int i = 0; i < UI_SLOTS; i++) {
        SmzRun r;
        if (store_run_load(i + 1, &r) != 0) { ui_set_slot(u, i, 0, "", ""); continue; }
        char sum[48];
        smz_run_summary(&r, sum, sizeof sum);
        ui_set_slot(u, i, 1, r.name, sum);
        ui_set_slot_stats(u, i, r.zelda_cleared, r.zelda_total, r.metroid_cleared, r.metroid_total, r.play_ms, r.game, r.boss_mask, r.crystal_mask);
        ui_set_slot_rules(u, i, r.settings);
    }
}
