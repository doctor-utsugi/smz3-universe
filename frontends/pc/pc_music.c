#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "pc_compat.h"
#include "pc_music.h"
#include "pc_store.h"

static const int SLOTS[] = {
    2, 4, 5, 7, 9, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 26, 27, 28, 30, 31, 32,
    103, 104, 105, 106, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 126, 127,
};
#define SLOT_COUNT ((int)(sizeof SLOTS / sizeof SLOTS[0]))
#define MAX_FILES 512

static int is_pcm(const char *n) { size_t l = strlen(n); return l > 4 && !strcasecmp(n + l - 4, ".pcm"); }

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* The sorted .pcm paths of a folder (malloc'd strings). */
static int list_pcm(const char *dir, char **out, int max)
{
    DIR *d = opendir(dir);
    int n = 0;
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (!is_pcm(e->d_name)) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        out[n++] = strdup(path);
    }
    closedir(d);
    qsort(out, (size_t)n, sizeof out[0], cmp);
    return n;
}

int music_packages(char names[][32], int max)
{
    const char *root = store_path("music/x");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", store_path("music"));
    (void)root;
    DIR *d = opendir(dir);
    int n = 0;
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 31 || strchr(e->d_name, '"') || strchr(e->d_name, '\\')) continue;
        char sub[1100], *files[1];
        snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        DIR *t = opendir(sub);
        if (!t) continue;
        closedir(t);
        if (list_pcm(sub, files, 1) == 0) continue;
        free(files[0]);
        snprintf(names[n++], 32, "%s", e->d_name);
    }
    closedir(d);
    for (int i = 1; i < n; i++)   /* alphabetical, like the other frontends */
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { char t[32]; memcpy(t, names[j], 32); memcpy(names[j], names[j - 1], 32); memcpy(names[j - 1], t, 32); }
    return n;
}

/* Returns 1 when the track is in place. */
static int link_track(const char *base, int track, const char *file)
{
    char dst[1100];
    snprintf(dst, sizeof dst, "%s-%d.pcm", base, track);
    remove(dst);
#ifdef _WIN32
    /* symbolic links need a privilege on older Windows; a hard link needs the same volume; a copy always works */
    if (CreateSymbolicLinkA(dst, file, 0x2)) {
        if (GetFileAttributesA(dst) != INVALID_FILE_ATTRIBUTES) return 1;   /* some systems accept the call but leave a link that does not resolve */
        DeleteFileA(dst);
    }
    return CreateHardLinkA(dst, file, NULL) || CopyFileA(file, dst, FALSE);
#else
    return symlink(file, dst) == 0;   /* a track that fails to link stays on the game's own music */
#endif
}

static void clear_links(const char *base)
{
    char dir[1100], prefix[300];
    snprintf(dir, sizeof dir, "%s", base);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = 0;
    snprintf(prefix, sizeof prefix, "%s-", slash + 1);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char msu[300];
    snprintf(msu, sizeof msu, "%s.msu", slash + 1);
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, msu) && !(!strncmp(e->d_name, prefix, strlen(prefix)) && is_pcm(e->d_name))) continue;
        char full[1500];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        remove(full);
    }
    closedir(d);
}

static int json_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[48];
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

/* java.util.Random, so the same seed deals the same tracks as on Android */
static int64_t jr_seed;
static void jr_init(int64_t s) { jr_seed = (s ^ 0x5DEECE66DLL) & ((1LL << 48) - 1); }
static int jr_next(int bits) { jr_seed = (jr_seed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1); return (int)(jr_seed >> (48 - bits)); }
static int jr_int(int bound)
{
    int r = jr_next(31), m = bound - 1;
    if ((bound & m) == 0) return (int)(((int64_t)bound * (int64_t)r) >> 31);
    for (int u = r; u - (r = u % bound) + m < 0; u = jr_next(31)) ;
    return r;
}

static int32_t java_hash(const char *s) { uint32_t h = 0; for (; *s; s++) h = h * 31 + (unsigned char)*s; return (int32_t)h; }

int music_prepare(const char *rom_path, const char *settings_json, const char *seed)
{
    char base[1100], pkg[40] = "";
    snprintf(base, sizeof base, "%s", rom_path);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    clear_links(base);
    if (!json_str(settings_json, "musicPackage", pkg, sizeof pkg) || !pkg[0]) return 0;
    char *files[MAX_FILES];
    int n = 0;
    char root[1024];
    snprintf(root, sizeof root, "%s", store_path("music"));
    if (!strcmp(pkg, "*")) {
        DIR *d = opendir(root);
        struct dirent *e;
        while (d && (e = readdir(d)) && n < MAX_FILES - 1) {
            if (e->d_name[0] == '.') continue;
            char sub[1100];
            snprintf(sub, sizeof sub, "%s/%s", root, e->d_name);
            n += list_pcm(sub, files + n, MAX_FILES - n);
        }
        if (d) closedir(d);
        n += list_pcm(root, files + n, MAX_FILES - n);
    } else {
        char sub[1100];
        snprintf(sub, sizeof sub, "%s/%s", root, pkg);
        n = list_pcm(sub, files, MAX_FILES);
    }
    int linked = 0;
    if (n > 0) {
        char marker[1100];
        snprintf(marker, sizeof marker, "%s.msu", base);
        FILE *f = fopen(marker, "wb");
        if (f) fclose(f);
        int fixed = 0;
        if (strcmp(pkg, "*")) {
            for (int i = 0; i < n; i++) {
                const char *name = strrchr(files[i], '/') + 1, *dash = strrchr(name, '-');
                if (dash && atoi(dash + 1) > 0 && is_pcm(name)) { fixed += link_track(base, atoi(dash + 1), files[i]); }
            }
        }
        if (fixed) linked = fixed;
        else {
            jr_init((int64_t)java_hash(seed) * 31 + 7);
            for (int i = n; i > 1; i--) { int j = jr_int(i); char *t = files[i - 1]; files[i - 1] = files[j]; files[j] = t; }
            for (int i = 0; i < SLOT_COUNT; i++) linked += link_track(base, SLOTS[i], files[i % n]);
        }
    }
    for (int i = 0; i < n; i++) free(files[i]);
    return linked;
}
