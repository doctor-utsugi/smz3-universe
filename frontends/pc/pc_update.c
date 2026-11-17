#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "pc_compat.h"
#include "pc_store.h"
#include "pc_update.h"

/* The release file this build is replaced with; an AppImage keeps its own name, the archives unpack next to the program. */
#if defined(_WIN32)
#define ASSET "SMZ3-windows-x64.zip"
#elif defined(__APPLE__)
#define ASSET "SMZ3-macos-intel.tar.gz"
#elif defined(__aarch64__)
#define ASSET "SMZ3-aarch64.AppImage"
#else
#define ASSET "SMZ3-x86_64.AppImage"
#endif

static const char *appimage(void) { const char *a = getenv("APPIMAGE"); return a && *a ? a : NULL; }

int update_can_install(void)
{
#if defined(_WIN32) || defined(__APPLE__)
    return 1;
#else
    return appimage() != NULL;   /* a build from source is updated by building it again */
#endif
}

static int self_path(char *out, size_t cap)
{
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    return n > 0 && n < cap ? 0 : -1;
#elif defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t n = sizeof raw;
    if (_NSGetExecutablePath(raw, &n) || !realpath(raw, out)) return -1;
    return strlen(out) < cap ? 0 : -1;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return -1;
    out[n] = 0;
    return 0;
#endif
}

/* Runs a program and waits for it: 0 when it exited with 0. */
#ifdef _WIN32
static int run(char *cmd)
{
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 15 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code == 0 ? 0 : -1;
}
#else
static int run(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        unsetenv("LD_LIBRARY_PATH");   /* the AppImage's bundled libs break the system curl, as in pc_online.c */
        unsetenv("LD_PRELOAD");
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}
#endif

static int download(const char *url, const char *file)
{
    remove(file);
#ifdef _WIN32
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "curl.exe -s -f -L -m 900 -o \"%s\" \"%s\"", file, url);
    return run(cmd);
#else
    char *argv[] = {"curl", "-s", "-f", "-L", "-m", "900", "-o", (char *)file, (char *)url, NULL};
    return run(argv);
#endif
}

static int json_field(const char *json, const char *key, char *out, size_t cap)
{
    char pat[24];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = json ? strstr(json, pat) : NULL;
    if (!p) return -1;
    p += strlen(pat);
    size_t n = strcspn(p, "\"");
    if (!n || n >= cap) return -1;
    memcpy(out, p, n);
    out[n] = 0;
    return 0;
}

int update_install(const char *json, char *err, size_t cap)
{
    char base[256], url[400], self[PATH_MAX];
    if (json_field(json, "base", base, sizeof base)) { snprintf(err, cap, "No release to install"); return -1; }
    snprintf(url, sizeof url, "%s" ASSET, base);
#if defined(_WIN32) || defined(__APPLE__)
    char file[PATH_MAX], dir[PATH_MAX], old[PATH_MAX + 8];
    snprintf(file, sizeof file, "%s", store_path("tmp/" ASSET));
    if (self_path(self, sizeof self)) { snprintf(err, cap, "Can't find the program's file"); return -1; }
    if (download(url, file)) { remove(file); snprintf(err, cap, "Download failed"); return -1; }
    snprintf(dir, sizeof dir, "%s", self);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bslash = strrchr(dir, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (!slash) { remove(file); snprintf(err, cap, "Can't find the program's folder"); return -1; }
    *slash = 0;
    /* the running program moves aside (Windows allows a rename, not a delete) and the archive unpacks in its place */
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
    if (rename(self, old)) { remove(file); snprintf(err, cap, "The program's folder is read-only"); return -1; }
#ifdef _WIN32
    char cmd[2 * PATH_MAX + 32];
    snprintf(cmd, sizeof cmd, "tar.exe -xf \"%s\" -C \"%s\"", file, dir);
    int bad = run(cmd);
#else
    char *argv[] = {"tar", "xzf", file, "-C", dir, NULL};
    int bad = run(argv);
#endif
    remove(file);
    if (bad || access(self, F_OK)) { remove(self); rename(old, self); snprintf(err, cap, "Couldn't unpack the release"); return -1; }
#ifndef _WIN32
    remove(old);
#endif
    return 0;
#else
    const char *target = appimage();
    char part[PATH_MAX + 8];
    if (!target) { snprintf(err, cap, "Only the AppImage updates itself"); return -1; }
    (void)self;
    snprintf(part, sizeof part, "%s.new", target);
    if (download(url, part)) { remove(part); snprintf(err, cap, "Download failed"); return -1; }
    if (chmod(part, 0755) || rename(part, target)) { remove(part); snprintf(err, cap, "The AppImage's folder is read-only"); return -1; }
    return 0;
#endif
}

void update_restart(char **argv)
{
    char self[PATH_MAX];
#ifdef _WIN32
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!self_path(self, sizeof self) && CreateProcessA(self, GetCommandLineA(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    exit(0);
#else
    const char *target = appimage();
    if (!target && !self_path(self, sizeof self)) target = self;
    if (target) execv(target, argv);
    exit(0);
#endif
}

void update_cleanup(void)
{
#ifdef _WIN32
    char self[PATH_MAX], old[PATH_MAX + 8];
    if (self_path(self, sizeof self)) return;
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
#endif
}
