/* Map Rando on the PC: the crate (core/maprando) behind smz_sm_set_maprando, with the data that tools/maprando/fetch.sh packs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include "pc_maprando.h"
#include "pc_online.h"
#include "pc_store.h"
#include "smz3_bps.h"
#include "smz3_sm.h"

#ifdef SMZ_MAPRANDO
#include "smz3_maprando.h"

static char g_dir[1100];
static volatile int g_phase;   /* 0 idle, 1 loading the data, 2 building the seed, 3 downloading it from the server */

#define MR_NEED_MB 600   /* the data loaded to build a seed, freed after it */

/* The memory the system can still give, in MB (Linux), or -1 where it is not known. */
static long mem_available_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128];
    long kb = -1;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb < 0 ? -1 : kb / 1024;
}

static void error_json(char *info, size_t cap, const char *why)
{
    char clean[300];
    snprintf(clean, sizeof clean, "%s", why);
    for (char *c = clean; *c; c++) if (*c == '"' || *c == '\\' || *c < ' ') *c = '\'';
    snprintf(info, cap, "{\"error\":\"%s\"}", clean);
}

/* The server builds the seed and sends a BPS patch from the vanilla ROM (server/ladder/maprando.js), as on the 3DS: for a
 * device without the memory to build it here. curl runs as a program, like the rest of the online code. */
static int from_server(const char *settings, uint64_t seed, const uint8_t *sm, size_t sm_len, uint8_t *out, size_t cap,
                       char *info, size_t info_cap)
{
#ifdef _WIN32
    (void)settings; (void)seed; (void)sm; (void)sm_len; (void)out; (void)cap;
    error_json(info, info_cap, "Not enough free memory for Map Rando");
    return -1;
#else
    char url[1400], q[1100], patch_path[1100], head_path[1100];
    size_t k = 0;
    for (const unsigned char *c = (const unsigned char *)settings; *c && k + 4 < sizeof q; c++) {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || strchr("-_.~", *c)) q[k++] = (char)*c;
        else k += (size_t)snprintf(q + k, sizeof q - k, "%%%02X", *c);
    }
    q[k] = 0;
    snprintf(url, sizeof url, "%s/api/maprando/seed?settings=%s&seed=%llu", online_server(), q, (unsigned long long)seed);
    snprintf(patch_path, sizeof patch_path, "%s", store_path("maprando-seed.bps"));
    snprintf(head_path, sizeof head_path, "%s", store_path("maprando-seed.head"));
    remove(patch_path); remove(head_path);
    g_phase = 3;
    int status = -1, pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            dup2(pipefd[1], 1);
            close(pipefd[0]);
            unsetenv("LD_LIBRARY_PATH");   /* the AppImage's bundled libs break the system curl */
            unsetenv("LD_PRELOAD");
            execlp("curl", "curl", "-s", "--connect-timeout", "20", "-m", "300", "-D", head_path, "-o", patch_path, "-w", "%{http_code}", url, (char *)NULL);
            _exit(127);
        }
        close(pipefd[1]);
        char code[16] = "";
        if (pid > 0) {
            ssize_t r = read(pipefd[0], code, sizeof code - 1);
            code[r > 0 ? r : 0] = 0;
            int w;
            waitpid(pid, &w, 0);
            status = atoi(code);
        }
        close(pipefd[0]);
    }
    g_phase = 0;
    if (status <= 0) { error_json(info, info_cap, "Connect to the internet: this device downloads Map Rando seeds"); return -1; }
    if (status == 503) { error_json(info, info_cap, "The server is busy, try again in a minute"); return -1; }
    if (status == 429) { error_json(info, info_cap, "Too many seeds this hour, try later"); return -1; }
    if (status == 422) { error_json(info, info_cap, "The server found no seed for this"); return -1; }
    if (status != 200) { error_json(info, info_cap, "The server could not make the seed"); return -1; }
    size_t n = 0;
    uint8_t *patch = store_read(patch_path, &n);
    if (sm_len % 0x8000 == 0x200) { sm += 0x200; sm_len -= 0x200; }   /* a copier header: the patch is made from the bare ROM */
    int b = !patch ? -1 : smz_bps_target_size(patch, n) > cap ? -1 : smz_bps_apply(patch, n, sm, sm_len, out, cap);
    free(patch);
    remove(patch_path);
    if (b == -2) { error_json(info, info_cap, "The patch does not fit this Super Metroid ROM (CRC)"); remove(head_path); return -1; }
    if (b) { error_json(info, info_cap, "The seed came damaged, try again"); remove(head_path); return -1; }
    snprintf(info, info_cap, "{}");
    size_t hn = 0;
    char *head = (char *)store_read(head_path, &hn), *line = NULL;
    for (char *h = head; h && *h; h = strchr(h, '\n') ? strchr(h, '\n') + 1 : NULL) if (!strncasecmp(h, "X-Seed-Info:", 12)) { line = h + 12; break; }
    if (line) {
        while (*line == ' ') line++;
        size_t len = strcspn(line, "\r\n");
        if (len >= info_cap) len = info_cap - 1;
        memcpy(info, line, len);
        info[len] = 0;
    }
    free(head);
    remove(head_path);
    return 0;
#endif
}

static int generate(const char *settings, uint64_t seed, const uint8_t *sm, size_t sm_len, uint8_t *out, size_t cap,
                    char *info, size_t info_cap)
{
    char err[300];
    long free_mb = mem_available_mb();
    if (free_mb >= 0 && free_mb < MR_NEED_MB) return from_server(settings, seed, sm, sm_len, out, cap, info, info_cap);   /* a 1 GB handheld */
    g_phase = 1;
    if (smz_mr_init(g_dir, err, sizeof err)) { g_phase = 0; error_json(info, info_cap, err); return -1; }
    g_phase = 2;
    int rc = smz_mr_generate(settings, seed, 0, sm, sm_len, out, cap, info, info_cap);
    smz_mr_free();   /* about 600 MB the game does not need */
    g_phase = 0;
    return rc;
}

void maprando_setup(void)
{
    /* the AppImage's share folder, a zip or tarball beside the program, then build/maprando-data from build/pc */
    static const char *const ROOTS[] = {"%s/../share/smz3/maprando-data", "%s/maprando-data", "%s/../maprando-data"};
    for (unsigned r = 0; r < sizeof ROOTS / sizeof ROOTS[0]; r++) {
        char probe[1200];
        snprintf(g_dir, sizeof g_dir, ROOTS[r], store_exe_dir());
        snprintf(probe, sizeof probe, "%s/rust/data/presets", g_dir);
        if (access(probe, F_OK) == 0) { smz_sm_set_maprando(generate, SMZ_MR_READY); return; }
    }
}

int maprando_progress(char *out, size_t cap)
{
    if (g_phase == 1 || g_phase == 2) snprintf(out, cap, "Loading");
    else if (g_phase == 3) snprintf(out, cap, "Downloading the Map Rando seed");
    return g_phase;
}
#else
void maprando_setup(void) {}
int maprando_progress(char *out, size_t cap) { (void)out; (void)cap; return 0; }
#endif
