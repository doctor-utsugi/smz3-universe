#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "pc_compat.h"
#include "pc_gfx.h"
#include "pc_help.h"
#include "pc_pad.h"
#include "pc_settings.h"
#include "pc_shortcuts.h"
#include "pc_store.h"
#include "smz3_shortcuts.h"

enum { S_OFF, S_MAIN, S_PAD, S_REBIND, S_FILES, S_VIDEO, S_SC, S_SCBIND };
#define MAIN_COUNT 4
#define VIDEO_ROWS 10
#define VIDEO_COUNT (VIDEO_ROWS + 1)
static const char *const ASPECTS[] = {"Run default", "4:3", "8:7 (square pixels)", "Stretch", "Pixel perfect"};
static const char *const FILTERS[] = {"Sharp", "Smooth", "Scanlines", "LCD grid"};
static const char *const SIZES[] = {"720p", "1080p", "1440p", "Desktop"};
static const char *const MM_SIDES[] = {"Right", "Left", "Off"};
static const char *const MM_SIZES[] = {"Small", "Medium", "Large"};
static int vid_size = 1, vid_aspect, vid_filter, mm_side, mm_size = 2, ch_hide;   /* ch_hide: bits of the daily challenge info not shown in game */
#define ROW_H 22
#define SHOWN 8

static SettingsHooks hk;
static int screen, cursor, scroll, rebind_action;
static Uint32 rebind_until;
static char sc_msg[96];   /* the last bind's result on the item shortcuts screen */

typedef struct { char name[200]; int dir; } Entry;
static Entry *entries;
static int entry_count;
static char cwd[1024];

void settings_init(const SettingsHooks *hooks) { hk = *hooks; }
int settings_active(void) { return screen != S_OFF; }
void settings_open(void) { screen = S_MAIN; cursor = 0; scroll = 0; }

int settings_aspect(void) { return vid_aspect; }
int settings_filter(void) { return vid_filter; }
const char *settings_aspect_name(void) { return ASPECTS[vid_aspect]; }

static void video_save(void)
{
    char t[128];
    int n = snprintf(t, sizeof t, "fs=%d\nsize=%d\naspect=%d\nfilter=%d\nminimap=%d\nminimap_size=%d\nchallenge_hide=%d\n", hk.is_fullscreen ? hk.is_fullscreen() : 0, vid_size, vid_aspect, vid_filter, mm_side, mm_size, ch_hide);
    store_write(store_path("video.cfg"), t, (size_t)n);
}

void settings_video_save_state(void) { video_save(); }

static void apply_size(void)
{
    static const int W[] = {1280, 1920, 2560}, H[] = {720, 1080, 1440};
    if (hk.window_size) { if (vid_size < 3) hk.window_size(W[vid_size], H[vid_size]); else hk.window_size(0, 0); }
}

void settings_cycle_aspect(void) { vid_aspect = (vid_aspect + 1) % 5; video_save(); char m[64]; snprintf(m, sizeof m, "Aspect: %s", ASPECTS[vid_aspect]); hk.note(m); }
void settings_cycle_filter(void) { vid_filter = (vid_filter + 1) % 4; video_save(); char m[64]; snprintf(m, sizeof m, "Filter: %s", FILTERS[vid_filter]); hk.note(m); }

void settings_video_load(void)
{
    size_t n;
    char *t = (char *)store_read(store_path("video.cfg"), &n);
    int fs = 0;
    if (t) {
        t[n] = 0;
        for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
            int v = atoi(strchr(line, '=') ? strchr(line, '=') + 1 : "0");
            if (!strncmp(line, "fs=", 3)) fs = v;
            else if (!strncmp(line, "size=", 5) && v >= 0 && v < 4) vid_size = v;
            else if (!strncmp(line, "aspect=", 7) && v >= 0 && v < 5) vid_aspect = v;
            else if (!strncmp(line, "minimap_size=", 13) && atoi(line + 13) >= 0 && atoi(line + 13) < 3) mm_size = atoi(line + 13);
            else if (!strncmp(line, "minimap=", 8) && atoi(line + 8) >= 0 && atoi(line + 8) < 3) mm_side = atoi(line + 8);
            else if (!strncmp(line, "filter=", 7) && v >= 0 && v < 4) vid_filter = v;
            else if (!strncmp(line, "challenge_hide=", 15)) ch_hide = v & 15;
        }
        free(t);
    }
    if (!fs) apply_size();
    if (fs && hk.is_fullscreen && !hk.is_fullscreen()) hk.fullscreen();
}

static void video_change(int row, int dir)
{
    if (row == 0) { hk.fullscreen(); }
    else if (row == 1) { vid_size = (vid_size + dir + 4) % 4; if (!hk.is_fullscreen()) apply_size(); }
    else if (row == 2) vid_aspect = (vid_aspect + dir + 5) % 5;
    else if (row == 3) vid_filter = (vid_filter + dir + 4) % 4;
    else if (row == 4) mm_side = (mm_side + dir + 3) % 3;
    else if (row == 5) mm_size = (mm_size + dir + 3) % 3;
    else if (row >= 6 && row < 10) ch_hide ^= 1 << (row - 6);
    video_save();
}

/* the compact video panel shown over the game */
const char *settings_video_row(int row, char *val, size_t cap)
{
    static const char *const L[VIDEO_ROWS] = {"Full screen", "Window size", "Aspect ratio", "Filter", "Minimap", "Minimap size",
                                              "Challenge: window clock", "Challenge: own time", "Challenge: goal and hints", "Challenge: next hint in"};
    const char *v[VIDEO_ROWS] = {hk.is_fullscreen() ? "On" : "Off", SIZES[vid_size], ASPECTS[vid_aspect], FILTERS[vid_filter], MM_SIDES[mm_side], MM_SIZES[mm_size],
                                 ch_hide & 1 ? "Hidden" : "Shown", ch_hide & 2 ? "Hidden" : "Shown", ch_hide & 4 ? "Hidden" : "Shown", ch_hide & 8 ? "Hidden" : "Shown"};
    if (row < 0 || row >= VIDEO_ROWS) row = 0;
    snprintf(val, cap, "%s", v[row]);
    return L[row];
}
int settings_video_rows(void) { return VIDEO_ROWS; }
void settings_video_change(int row, int dir) { if (row >= 0 && row < VIDEO_ROWS) video_change(row, dir); }
int settings_minimap_side(void) { return mm_side; }
float settings_minimap_scale(void) { return mm_size == 0 ? 0.6f : mm_size == 1 ? 0.8f : 1.0f; }
int settings_challenge_show(int what) { return !(ch_hide >> what & 1); }

/* ---- file browser ---- */
static int is_rom_name(const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc") || !strcasecmp(dot, ".zip") || !strcasecmp(dot, ".bin") || !strcasecmp(dot, ".rom");
}

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void list_dir(const char *path)
{
    free(entries);
    entries = NULL;
    entry_count = 0;
    int cap = 64;
    entries = malloc(sizeof(Entry) * (size_t)cap);
    snprintf(cwd, sizeof cwd, "%s", path);
    size_t l = strlen(cwd);
    while (l > 1 && (cwd[l - 1] == '/' || cwd[l - 1] == '\\') && !(l == 3 && cwd[1] == ':')) cwd[--l] = 0;
    DIR *d = opendir(cwd);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof entries[0].name) continue;
            char full[1400];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", cwd, e->d_name);
            if (stat(full, &st)) continue;
            int dir = S_ISDIR(st.st_mode);
            if (!dir && !is_rom_name(e->d_name)) continue;
            if (entry_count == cap) { cap *= 2; entries = realloc(entries, sizeof(Entry) * (size_t)cap); }
            snprintf(entries[entry_count].name, sizeof entries[0].name, "%s", e->d_name);
            entries[entry_count++].dir = dir;
        }
        closedir(d);
    }
    qsort(entries, (size_t)entry_count, sizeof(Entry), entry_cmp);
    cursor = 0;
    scroll = 0;
}

/* rows: 0 "use this folder", 1 "up one level", then the entries */
static int file_rows(void) { return entry_count + 2; }

static void go_up(void)
{
    char up[1024];
    snprintf(up, sizeof up, "%s", cwd);
    char *s = strrchr(up, '/');
    char *b = strrchr(up, '\\');
    if (b && (!s || b > s)) s = b;
    if (!s) return;
    if (s == up) up[1] = 0; else if (s[-1] == ':') s[1] = 0; else *s = 0;
    list_dir(up);
}

void settings_open_files(void)
{
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    char start[1024] = ".";
    if (home) {
        snprintf(start, sizeof start, "%s/Downloads", home);
        struct stat st;
        if (stat(start, &st) || !S_ISDIR(st.st_mode)) snprintf(start, sizeof start, "%s", home);
    }
    list_dir(start);
    screen = S_FILES;
}

static void leave_files(void)
{
    screen = S_OFF;
}

static void file_pick(int row)
{
    if (row == 0) {
        hk.note(hk.setup(cwd));
        leave_files();
    } else if (row == 1) go_up();
    else {
        Entry e = entries[row - 2];
        char full[1400];
        snprintf(full, sizeof full, "%s/%s", cwd, e.name);
        if (e.dir) list_dir(full);
        else { hk.note(hk.setup(full)); leave_files(); }
    }
}

/* ---- input ---- */
static void clamp_scroll(int rows)
{
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + SHOWN) scroll = cursor - SHOWN + 1;
    if (scroll > rows - SHOWN) scroll = rows - SHOWN;
    if (scroll < 0) scroll = 0;
}

void settings_nav(UiButton b)
{
    int rows = screen == S_MAIN ? MAIN_COUNT : screen == S_VIDEO ? VIDEO_COUNT : screen == S_PAD ? PA_COUNT + 2 : screen == S_SC ? SC_COUNT + 1 : screen == S_FILES ? file_rows() : 0;
    if (screen == S_REBIND || screen == S_SCBIND) return;
    if (b == BTN_DOWN && cursor < rows - 1) cursor++;
    else if (b == BTN_UP && cursor > 0) cursor--;
    else if ((b == BTN_LEFT || b == BTN_RIGHT) && screen == S_VIDEO && cursor < VIDEO_ROWS) video_change(cursor, b == BTN_RIGHT ? 1 : -1);
    else if ((b == BTN_R || b == BTN_L) && screen == S_SC) cursor = b == BTN_R ? (cursor + SHOWN < rows ? cursor + SHOWN : rows - 1) : (cursor > SHOWN ? cursor - SHOWN : 0);
    else if (b == BTN_R && screen == S_FILES) cursor = cursor + SHOWN < rows ? cursor + SHOWN : rows - 1;
    else if (b == BTN_L && screen == S_FILES) cursor = cursor > SHOWN ? cursor - SHOWN : 0;
    else if (b == BTN_B) {
        if (screen == S_MAIN) screen = S_OFF;
        else if (screen == S_FILES) leave_files();
        else { cursor = screen == S_SC ? 2 : screen == S_VIDEO ? 1 : 0; screen = S_MAIN; scroll = 0; }
    } else if (b == BTN_A) {
        if (screen == S_MAIN) {
            if (cursor == 0) { screen = S_PAD; cursor = 0; scroll = 0; sc_msg[0] = 0; }
            else if (cursor == 1) { screen = S_VIDEO; cursor = 0; scroll = 0; }
            else if (cursor == 2) { screen = S_SC; cursor = 0; scroll = 0; sc_msg[0] = 0; }
            else hk.quit();
        } else if (screen == S_VIDEO) {
            if (cursor == VIDEO_ROWS) { screen = S_MAIN; cursor = 1; scroll = 0; }
            else video_change(cursor, 1);
        } else if (screen == S_PAD) {
            if (cursor < PA_COUNT) { rebind_action = cursor; rebind_until = SDL_GetTicks() + 6000; screen = S_REBIND; }
            else if (cursor == PA_COUNT) { pad_cfg_reset(); pad_cfg_save(); snprintf(sc_msg, sizeof sc_msg, "Back to the default keys and buttons"); }
            else { screen = S_MAIN; cursor = 0; scroll = 0; }
        } else if (screen == S_SC) {
            if (cursor < SC_COUNT) { rebind_action = cursor; rebind_until = SDL_GetTicks() + 6000; screen = S_SCBIND; }
            else { screen = S_MAIN; cursor = 2; scroll = 0; }
        } else if (screen == S_FILES) file_pick(cursor);
    }
    clamp_scroll(rows);
}

int settings_raw(const SDL_Event *e, int code)
{
    if (screen == S_SCBIND) {
        const char *err = NULL;
        const char *name = smz_shortcuts[rebind_action].name;
        if (e->type == SDL_KEYDOWN && e->key.repeat) return 1;
        if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) { screen = S_SC; return 1; }
        if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_DELETE) { sc_clear(rebind_action); snprintf(sc_msg, sizeof sc_msg, "%s has no key or button now", name); screen = S_SC; return 1; }
        if (e->type == SDL_KEYDOWN) err = sc_bind_key(rebind_action, e->key.keysym.sym);
        else if (code >= 0) err = sc_bind_pad(rebind_action, code);
        else return 0;
        if (err) snprintf(sc_msg, sizeof sc_msg, "%s", err);
        else snprintf(sc_msg, sizeof sc_msg, "%s is on %s", name, e->type == SDL_KEYDOWN ? SDL_GetKeyName(e->key.keysym.sym) : pad_code_name(code));
        screen = S_SC;
        return 1;
    }
    if (screen != S_REBIND) return 0;
    if (e->type == SDL_KEYDOWN && e->key.repeat) return 1;
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) { screen = S_PAD; return 1; }
    const char *name = pad_action_name(rebind_action);
    if (e->type == SDL_KEYDOWN && rebind_action < PA_TRACKER) {
        SDL_Keycode key = e->key.keysym.sym;
        const char *use = pad_key_use(key);
        if (key == SDLK_DELETE) { pad_set_key(rebind_action, SDLK_UNKNOWN); snprintf(sc_msg, sizeof sc_msg, "%s has no key now", name); }
        else if (key == pad_key(rebind_action)) sc_msg[0] = 0;
        else if (use) snprintf(sc_msg, sizeof sc_msg, "%s is used by %s", SDL_GetKeyName(key), use);
        else {
            for (int i = 0; i < SC_COUNT && !use; i++) if (sc_key(i) == key) use = smz_shortcuts[i].name;
            if (use) snprintf(sc_msg, sizeof sc_msg, "%s is used by %s", SDL_GetKeyName(key), use);
            else { pad_set_key(rebind_action, key); snprintf(sc_msg, sizeof sc_msg, "%s is on %s", name, SDL_GetKeyName(key)); }
        }
        screen = S_PAD;
        return 1;
    }
    if (code >= 0) {
        pad_set(rebind_action, code);
        pad_cfg_save();
        snprintf(sc_msg, sizeof sc_msg, "%s is on %s", name, pad_code_name(code));
        screen = S_PAD;
        return 1;
    }
    return e->type == SDL_KEYDOWN;
}

/* ---- drawing ---- */
static SDL_Color col(int i) { return gfx_ui_color(i); }

static void rect(float k, float ox, float oy, float x, float y, float w, float h, int c) { gfx_rect_idx(k, ox, oy, x, y, w, h, c); }
static void text(float k, float ox, float oy, float x, float y, int c, int align, const char *s) { gfx_text(ox + x * k, oy + y * k, 12 * k, col(c), align, s); }

static void row(float k, float ox, float oy, int i, const char *left, const char *right, int selected)
{
    float y = 32 + i * ROW_H;
    rect(k, ox, oy, 8, y, 304, ROW_H - 3, selected ? 2 : 1);
    if (selected) rect(k, ox, oy, 8, y, 4, ROW_H - 3, 5);
    text(k, ox, oy, 20, y + 1, 3, 0, left);
    if (right) text(k, ox, oy, 306, y + 1, selected ? 3 : 4, 2, right);
}

void settings_draw(float k, float ox, float oy)
{
    if (screen == S_OFF) return;
    gfx_backdrop_ui(k, ox, oy);
    rect(k, ox, oy, 0, 0, 320, 28, 1);
    const char *title = screen == S_MAIN ? "Settings" : screen == S_FILES ? "Find the ROM files" : screen == S_VIDEO ? "Video" : screen == S_SC || screen == S_SCBIND ? "Item shortcuts" : "Buttons and keys";
    gfx_logo(ox + 8 * k, oy + 2 * k, 24 * k, 1);
    text(k, ox, oy, 38, 6, 5, 0, title);
    const char *hint = "A choose   B back";
    (void)hint;
    if (screen == S_MAIN) {
        static const char *const L[] = {"Buttons and keys", "Video", "Item shortcuts", "Quit"};
        for (int i = 0; i < MAIN_COUNT; i++) row(k, ox, oy, i, L[i], NULL, i == cursor);
        hint = "A choose   B back";
    } else if (screen == S_VIDEO) {
        for (int i = 0; i < SHOWN && scroll + i < VIDEO_COUNT; i++) {
            int r = scroll + i;
            char val[48], v[64];
            if (r == VIDEO_ROWS) { row(k, ox, oy, i, "Back", NULL, cursor == r); continue; }
            const char *lab = settings_video_row(r, val, sizeof val);
            snprintf(v, sizeof v, "< %s >", val);
            row(k, ox, oy, i, lab, v, r == cursor);
        }
        hint = "Left Right change   B back";
    } else if (screen == S_SC || screen == S_SCBIND) {
        for (int i = 0; i < SHOWN && scroll + i < SC_COUNT + 1; i++) {
            int r = scroll + i;
            if (r == SC_COUNT) { row(k, ox, oy, i, "Back", NULL, r == cursor); continue; }
            char lab[48], val[64];
            snprintf(lab, sizeof lab, "%s%s", r >= SC_ZELDA_COUNT && r < SC_WEAPON_FIRST ? "Beam: " : "", smz_shortcuts[r].name);
            snprintf(val, sizeof val, "%s  /  %s", sc_key(r) != SDLK_UNKNOWN ? SDL_GetKeyName(sc_key(r)) : "-", sc_pad(r) >= 0 ? pad_code_name(sc_pad(r)) : "-");
            row(k, ox, oy, i, lab, val, r == cursor);
        }
        text(k, ox, oy, 12, 32 + SHOWN * ROW_H - 1, sc_msg[0] ? 5 : 4, 0, sc_msg[0] ? sc_msg : "Equips the item on Y, or turns the beam on or off");
        hint = "A bind   B back   L R page";
        if (screen == S_SCBIND) {
            rect(k, ox, oy, 20, 80, 280, 80, 2);
            rect(k, ox, oy, 20, 80, 280, 3, 5);
            char msg[96];
            snprintf(msg, sizeof msg, "Press a key or a button for %s", smz_shortcuts[rebind_action].name);
            text(k, ox, oy, 160, 98, 3, 1, msg);
            text(k, ox, oy, 160, 122, 4, 1, "Delete clears it   Esc or wait to cancel");
            int left = (int)(rebind_until - SDL_GetTicks());
            if (left <= 0) screen = S_SC;
            else rect(k, ox, oy, 20, 154, 280.0f * left / 6000.0f, 6, 5);
        }
    } else if (screen == S_PAD || screen == S_REBIND) {
        for (int i = 0; i < SHOWN && scroll + i < PA_COUNT + 2; i++) {
            int r = scroll + i;
            char val[64];
            if (r < PA_TRACKER) snprintf(val, sizeof val, "%s  /  %s", pad_key(r) != SDLK_UNKNOWN ? SDL_GetKeyName(pad_key(r)) : "-", pad_code_name(pad_code(r)));
            if (r < PA_COUNT) row(k, ox, oy, i, pad_action_name(r), r < PA_TRACKER ? val : pad_code_name(pad_code(r)), r == cursor);
            else row(k, ox, oy, i, r == PA_COUNT ? "Reset to defaults" : "Back", NULL, r == cursor);
        }
        if (sc_msg[0]) text(k, ox, oy, 12, 32 + SHOWN * ROW_H - 1, 5, 0, sc_msg);
        hint = "A change   B back";
        if (screen == S_REBIND) {
            rect(k, ox, oy, 20, 80, 280, 80, 2);
            rect(k, ox, oy, 20, 80, 280, 3, 5);
            char msg[96];
            int keyed = rebind_action < PA_TRACKER;
            snprintf(msg, sizeof msg, keyed ? "Press a key or a button for %s" : "Press the button for %s", pad_action_name(rebind_action));
            text(k, ox, oy, 160, 98, 3, 1, msg);
            text(k, ox, oy, 160, 122, 4, 1, keyed ? "Delete clears the key   Esc or wait to cancel" : "Esc or wait to cancel");
            int left = (int)(rebind_until - SDL_GetTicks());
            if (left <= 0) screen = S_PAD;
            else rect(k, ox, oy, 20, 154, 280.0f * left / 6000.0f, 6, 5);
        }
    } else {
        char shown[60];
        int n = file_rows();
        for (int i = 0; i < SHOWN && scroll + i < n; i++) {
            int r = scroll + i;
            if (r == 0) row(k, ox, oy, i, "[ Use this folder ]", NULL, r == cursor);
            else if (r == 1) row(k, ox, oy, i, "..", NULL, r == cursor);
            else {
                const Entry *e = &entries[r - 2];
                snprintf(shown, sizeof shown, "%s%s", e->name, e->dir ? "/" : "");
                row(k, ox, oy, i, shown, NULL, r == cursor);
            }
        }
        size_t l = strlen(cwd);
        text(k, ox, oy, 12, 206, 4, 0, l > 44 ? cwd + l - 44 : cwd);
        hint = "A open   B close   L R page";
    }
    help_bar_hint(k, ox, oy, hint);
}
