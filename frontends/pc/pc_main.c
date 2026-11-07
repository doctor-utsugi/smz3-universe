/* smz3: the PC frontend. A window with the run menu, the game and the tracker panels; also a small command line. */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include "pc_compat.h"
#include "smz3_version.h"
#include <SDL.h>
#include "pc_emu.h"
#include "pc_assets.h"
#include "pc_sfx.h"
#include "smz3_counters.h"
#include "pc_gfx.h"
#include "pc_help.h"
#include "stb_image.h"
#include "pc_dungeon.h"
#include "pc_map.h"
#include "pc_music.h"
#include "pc_pad.h"
#include "pc_settings.h"
#include "pc_dual.h"
#include "pc_finish.h"
#include "pc_hints.h"
#include "pc_hotkeys.h"
#include "pc_shortcuts.h"
#include "pc_welcome.h"
#include "smz3_dungeonmaps.h"
#include "smz3_goalicons.h"
#include "smz3_romderived.h"
#include "pc_online.h"
#include "pc_race.h"
#include "pc_store.h"
#include "smz3_host.h"
#include "smz3_rom.h"
#include "smz3_md5.h"
#include "smz3_discovery.h"
#include "smz3_proof.h"
#include "smz3_rewind.h"
#include "smz3_romsetup.h"
#include "pc_idle.h"
#include "smz3_runlogic.h"
#include "smz3_tracker.h"
#include "smz3_ui_gen.h"

static SmzUi ui;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Window *win2;       /* dual-screen mode (Linux handhelds): the tracker's window on the bottom display */
static SDL_Renderer *ren2;
static SDL_GameController *pad;

/* ---------- generation ---------- */

typedef struct {
    int slot;               /* 0-based */
    int rc;
    char err[160];
    SmzRun run;
    int cli;                /* the command line wrote nothing to the run store */
    const uint8_t *sm, *z3;
    uint8_t *rom;
    uint8_t *link, *samus; size_t link_len, samus_len;
    int32_t seed_if_random;
    const SmzUi *u;         /* the options to generate from; NULL: the menu's */
} GenJob;

static uint8_t *read_sprite(const char *kind, const char *name, size_t *len)
{
    *len = 0;
    if (!name[0] || !strcmp(name, "Original")) return NULL;
    return store_read(store_path("sprites/%s/%s.rdc", kind, name), len);
}

static void *generate_thread(void *arg)
{
    GenJob *j = arg;
    const SmzUi *gu = j->u ? j->u : &ui;
    char link[32], samus[32], json[4096], seed[16];
    smz_host_sprites(gu, link, samus);
    j->link = read_sprite("link", link, &j->link_len);
    j->samus = read_sprite("samus", samus, &j->samus_len);
    j->rom = malloc(SMZ_HOST_ROM_SIZE);
    j->rc = smz_host_generate(gu, j->seed_if_random, j->sm, j->z3, j->link, j->link_len, j->samus, j->samus_len, store_ips, NULL,
                              j->rom, json, sizeof json, seed, sizeof seed, NULL);
    if (j->rc) { snprintf(j->err, sizeof j->err, "generation failed (%d)", j->rc); return NULL; }
    memset(&j->run, 0, sizeof j->run);
    snprintf(j->run.name, sizeof j->run.name, "%s", ui_run_label(gu));
    snprintf(j->run.seed, sizeof j->run.seed, "%s", seed);
    snprintf(j->run.settings, sizeof j->run.settings, "%s", json);
    for (unsigned i = 0; i < smz3_location_count; i++)
        if (smz3_locations[i].game == SMZ3_GAME_ZELDA) j->run.zelda_total++; else j->run.metroid_total++;
    return NULL;
}

/* The generator needs more stack than a default thread has. */
static int run_big(void *(*fn)(void *), void *arg)
{
    pthread_attr_t at;
    pthread_t t;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64u << 20);
    if (pthread_create(&t, &at, fn, arg)) return -1;
    pthread_join(t, NULL);
    return 0;
}

static void generate(GenJob *j)
{
    if (run_big(generate_thread, j)) { j->rc = -1; snprintf(j->err, sizeof j->err, "cannot start the generator"); }
}

typedef struct { SmzLogic *logic; const char *json; int rc; } LogicJob;
static void *logic_thread(void *arg) { LogicJob *j = arg; j->rc = smz_logic_build(j->logic, j->json); return NULL; }

static void rom_file(const SmzRun *r, char *out, size_t cap)
{
    char name[64];
    smz_run_rom_name(r->settings, r->seed, name, sizeof name);
    snprintf(out, cap, "%s", store_path("roms/%s", name));
}

static void gen_free(GenJob *j) { free(j->rom); free(j->link); free(j->samus); }

/* ---------- command line ---------- */

static int cmd_gen(int argc, char **argv)
{
    const char *preset = "cozy", *out = NULL, *smp = NULL, *z3p = NULL, *spec = "";
    long seed = 0;
    char specbuf[1024] = "";
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--sm") && i + 1 < argc) smp = argv[++i];
        else if (!strcmp(argv[i], "--z3") && i + 1 < argc) z3p = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) { size_t n = strlen(specbuf); snprintf(specbuf + n, sizeof specbuf - n, "%s%s", n ? "," : "", argv[++i]); }
        else { fprintf(stderr, "gen: unknown argument %s\n", argv[i]); return 2; }
    }
    spec = specbuf;
    if (!out) { fprintf(stderr, "gen: --out FILE is required\n"); return 2; }
    if (smp || z3p) {
        char a[512], b[512];
        store_roms_get(a, b);
        store_roms_set(smp ? smp : a, z3p ? z3p : b);
    }
    GenJob j;
    memset(&j, 0, sizeof j);
    uint8_t *sm, *z3;
    char err[200];
    if (store_load_base(&sm, &z3, err, sizeof err)) { fprintf(stderr, "gen: %s\n(give --sm and --z3 once, they are remembered)\n", err); return 1; }
    ui_init(&ui);
    int mode = !strcmp(preset, "original") ? 1 : !strcmp(preset, "custom") ? 2 : 0;
    if (strcmp(preset, "cozy") && strcmp(preset, "original") && strcmp(preset, "custom")) { fprintf(stderr, "gen: preset must be cozy, original or custom\n"); return 2; }
    ui_preset(&ui, mode);
    ui_apply_spec(&ui, spec);
    ui.value[0] = (int)seed;
    j.sm = sm; j.z3 = z3;
    j.seed_if_random = (int32_t)((uint32_t)time(NULL) * 2654435761u >> 1) % 2147483646 + 1;
    generate(&j);
    if (j.rc) { fprintf(stderr, "gen: %s\n", j.err); return 1; }
    if (store_write(out, j.rom, SMZ_HOST_ROM_SIZE)) { fprintf(stderr, "gen: cannot write %s\n", out); return 1; }
    printf("%s  seed %s  %s\n", out, j.run.seed, j.run.name);
    gen_free(&j); free(sm); free(z3);
    return 0;
}

static int cmd_new(int argc, char **argv)
{
    const char *preset = "cozy", *name = NULL;
    long seed = 0;
    char specbuf[1024] = "";
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--preset") && i + 1 < argc) preset = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) { size_t n = strlen(specbuf); snprintf(specbuf + n, sizeof specbuf - n, "%s%s", n ? "," : "", argv[++i]); }
        else { fprintf(stderr, "new: unknown argument %s\n", argv[i]); return 2; }
    }
    int mode = !strcmp(preset, "original") ? 1 : !strcmp(preset, "custom") ? 2 : 0;
    int slot = store_free_slot();
    if (slot < 0) { fprintf(stderr, "new: all %d slots are used\n", UI_SLOTS); return 1; }
    uint8_t *sm, *z3;
    char err[200];
    if (store_load_base(&sm, &z3, err, sizeof err)) { fprintf(stderr, "new: %s\n", err); return 1; }
    static GenJob j;
    memset(&j, 0, sizeof j);
    ui_init(&ui);
    ui_preset(&ui, mode);
    ui_apply_spec(&ui, specbuf);
    ui.value[0] = (int)seed;
    j.sm = sm; j.z3 = z3;
    j.seed_if_random = (int32_t)((uint32_t)time(NULL) * 2654435761u >> 1) % 2147483646 + 1;
    generate(&j);
    if (j.rc) { fprintf(stderr, "new: %s\n", j.err); return 1; }
    if (name) snprintf(j.run.name, sizeof j.run.name, "%s", name);
    rom_file(&j.run, j.run.rom_path, sizeof j.run.rom_path);
    j.run.created = j.run.last_played = (int64_t)time(NULL) * 1000;
    if (store_write(j.run.rom_path, j.rom, SMZ_HOST_ROM_SIZE) || store_run_save(slot, &j.run)) { fprintf(stderr, "new: cannot write the run files\n"); return 1; }
    printf("slot %d  %s  seed %s\n", slot, j.run.name, j.run.seed);
    return 0;
}

static int cmd_online(void)
{
    char label[16];
    int seed;
    if (online_monthly(label, &seed)) printf("monthly seed %d (%s), %d days left\n", seed, label, online_days_left());
    else printf("no monthly seed (no connection, or curl is missing)\n");
    if (online_weekly(label, &seed)) printf("weekly seed %d (%s), %d days left\n", seed, label, online_weekly_days_left());
    for (int i = 1; i <= UI_SLOTS; i++) if (online_slot_kind(i)) printf("slot %d is on the %s ladder\n", i, online_slot_kind(i) == 3 ? "weekly" : online_slot_kind(i) == 2 ? "monthly" : "original");
    return 0;
}

static int cmd_runs(void)
{
    int n = 0;
    for (int i = 1; i <= UI_SLOTS; i++) {
        SmzRun r;
        if (store_run_load(i, &r)) continue;
        char sum[64];
        smz_run_summary(&r, sum, sizeof sum);
        printf("%2d  %-24s seed %-10s %s\n", i, r.name, r.seed, sum);
        n++;
    }
    if (!n) printf("no runs yet (%s)\n", store_dir());
    return 0;
}

static int cmd_config(int argc, char **argv)
{
    char sm[512], z3[512];
    store_roms_get(sm, z3);
    for (int i = 0; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--sm")) snprintf(sm, sizeof sm, "%s", argv[i + 1]);
        else if (!strcmp(argv[i], "--z3")) snprintf(z3, sizeof z3, "%s", argv[i + 1]);
    }
    if (argc) store_roms_set(sm, z3);
    printf("data  %s\nsm    %s\nz3    %s\n", store_dir(), sm, z3);
    uint8_t *a, *b;
    char err[200];
    if (store_load_base(&a, &b, err, sizeof err)) printf("check %s\n", err);
    else { printf("check both base ROMs are the right revision\n"); free(a); free(b); }
    return 0;
}

/* Runs a ROM for some frames without a window and reports what the tracker reads: a quick check of the emulator link. */
static int cmd_selftest(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "selftest ROM [frames]\n"); return 2; }
    size_t n;
    uint8_t *rom = store_read(argv[0], &n);
    if (!rom || emu_open(rom, n, argv[0])) { fprintf(stderr, "cannot load %s\n", argv[0]); return 1; }
    int frames = argc > 1 ? atoi(argv[1]) : 600;
    if (getenv("SMZ3_SELFTEST_SRAM")) {   /* test hook: start from a game save */
        size_t sn, sl;
        uint8_t *save = store_read(getenv("SMZ3_SELFTEST_SRAM"), &sn), *sram = emu_sram(&sl);
        if (save && sram) memcpy(sram, save, sn < sl ? sn : sl);
        free(save);
    }
    static SmzTracker t;
    smz_tracker_reset(&t, 0, 0, 0, 0);
    if (getenv("SMZ3_SELFTEST_GOAL")) smz_goal_parse(getenv("SMZ3_SELFTEST_GOAL"), &t.goal);   /* test hook: say when a daily goal reads as met */
    int goal_said = 0;
    uint64_t sum = 0;
    size_t audio_total = 0;
    const char *script = getenv("SMZ3_SELFTEST_PAD");   /* test hook: "first-last:bits,..." holds pad bits over frame ranges */
    for (int f = 0; f < frames; f++) {
        int pad = 0;
        for (const char *p = script; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL) {
            int a0, a1, m;
            if (sscanf(p, "%d-%d:%d", &a0, &a1, &m) == 3 && f >= a0 && f <= a1) pad |= m;
        }
        emu_set_pad(pad);
        for (const char *p = getenv("SMZ3_SELFTEST_POKE"); p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL) {   /* test hook: "frame:addr=value,..." writes the work RAM */
            int at; unsigned addr, v;
            size_t wl;
            uint8_t *w = (uint8_t *)emu_wram(&wl);
            if (sscanf(p, "%d:%x=%x", &at, &addr, &v) == 3 && at == f && addr < wl) w[addr] = (uint8_t)v;
        }
        emu_run_frame();
        size_t a;
        emu_take_audio(&a);
        audio_total += a;
        if (f % 30 == 29) {
            size_t wl, sl;
            const uint8_t *wram = emu_wram(&wl);
            uint8_t *sram = emu_sram(&sl);
            if (getenv("SMZ3_SELFTEST_DEAD")) smz_prepared_apply(atoi(getenv("SMZ3_SELFTEST_DEAD")), (uint8_t *)wram, wl, sram, sl);   /* test hook: a mid-game start's dead bosses */
            smz_tracker_tick(&t, wram, wl, sram, sl, 500, 1);
            if (t.goal_met && !goal_said) {
                goal_said = 1;
                printf("goal met at frame %d: sram 33FE=%02x  wram 10=%02x 0998=%02x\n", f, sl > 0x33FE ? sram[0x33FE] : 0, wram[0x10], wram[0x998]);
            }
        }
    }
    int w, h, pitch;
    const uint32_t *pic = emu_picture(&w, &h, &pitch);
    for (int i = 0; pic && i < w * h; i++) sum = sum * 31 + pic[i];
    size_t wl, sl;
    const uint8_t *wram = emu_wram(&wl);
    uint8_t *sram = emu_sram(&sl);
    if (getenv("SMZ3_SELFTEST_SHOT") && pic) {   /* test hook: the last frame as a BMP */
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom((void *)pic, w, h, 32, pitch * 4, SDL_PIXELFORMAT_ARGB8888);
        if (sf) { SDL_SaveBMP(sf, getenv("SMZ3_SELFTEST_SHOT")); SDL_FreeSurface(sf); }
    }
    if (getenv("SMZ3_SELFTEST_DUMP")) {   /* test hook: write the work RAM and the save RAM to <prefix>.wram and <prefix>.sram */
        char path[600];
        snprintf(path, sizeof path, "%s.wram", getenv("SMZ3_SELFTEST_DUMP"));
        FILE *df = fopen(path, "wb");
        if (df) { fwrite(wram, 1, wl, df); fclose(df); }
        snprintf(path, sizeof path, "%s.sram", getenv("SMZ3_SELFTEST_DUMP"));
        if ((df = fopen(path, "wb"))) { fwrite(sram, 1, sl, df); fclose(df); }
    }
    printf("frames %d  picture %dx%d  checksum %016llx  audio %zu samples at %d Hz  wram %zu sram %zu  started %d game %d\n", frames, w, h,
           (unsigned long long)sum, audio_total, emu_sample_rate(), wl, sl, t.started, (int)t.game);
    emu_close();
    return pic ? 0 : 1;
}

static void usage(void)
{
    puts("smz3 - Super Metroid + A Link to the Past randomizer\n\n"
         "  smz3                          open the window (menu, game, tracker)\n"
         "  smz3 play N                   open the window and continue run N\n"
         "  smz3 runs                     list the saved runs\n"
         "  smz3 new [--preset P] [--seed N] [--name S] [--set Key=value]   make a run in the first free slot\n"
         "  smz3 setup <rom|zip|folder>...  find the two base ROMs and keep a copy in the data folder\n"
         "  smz3 online                   show the monthly seed and which runs are on the online ladder\n"
         "  smz3 config [--sm F] [--z3 F] show or set the base ROM files (Super Metroid JU, ALttP JP v1.0)\n"
         "  smz3 gen --out FILE [--preset cozy|original|custom] [--seed N] [--set Key=value ...]\n"
         "                                make a randomized ROM file without opening a window\n\n"
         "Drop the two base ROM files on the window the first time instead of using config.\n"
         "Window keys: arrows move, X A button, Z B, S X, A Y, Q/W shoulders, Enter start, Backspace select,\n"
         "Space fast forward, R rewind (runs that allow it), Tab tracker, Esc leave the run, F11 full screen.");
}

/* ---------- menu helpers ---------- */

static int base_ok(void);
static void refill_sprites(void)
{
    base_ok();   /* first run after setup: writes the Original previews the pickers show */
    static const char *const KIND[2] = {"link", "samus"};
    for (int g = 0; g < 2; g++) {
        static char names[UI_MAX_SPRITES][32];
        const char *list[UI_MAX_SPRITES];
        int n = 0;
        DIR *d = opendir(store_path("sprites/%s/x", KIND[g]) ? store_path("sprites/%s", KIND[g]) : ".");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) && n < UI_MAX_SPRITES - 2) {
                size_t l = strlen(e->d_name);
                if (l > 4 && !strcmp(e->d_name + l - 4, ".rdc") && l - 4 < 32) {
                    snprintf(names[n], 32, "%.*s", (int)(l - 4), e->d_name);
                    list[n] = names[n]; n++;
                }
            }
            closedir(d);
        }
        ui_set_sprites(&ui, g, list, n);
    }
    static char packs[UI_MAX_SPRITES][32];
    const char *plist[UI_MAX_SPRITES];
    int np = music_packages(packs, UI_MAX_SPRITES - 3);
    for (int i = 0; i < np; i++) plist[i] = packs[i];
    ui_set_sprites(&ui, 3, plist, np);
}

static void goal_icon_out(const char *name, const uint8_t *pv, size_t n, void *ctx)
{
    (void)ctx;
    store_write(store_path("goals/%s.pv", name), pv, n);
}

/* Reads the Super Metroid room table, the Original sprite previews and the world goal icons out of the base ROMs the first time (kept in the data folder). */
static void derive_data(const uint8_t *sm, const uint8_t *z3)
{
    static int rooms[SMZ_SM_ROOMS_MAX * 6];
    const char *rp = store_path("derived/sm_rooms.bin");
    if (access(rp, F_OK) != 0) {
        int n = smz_sm_rooms(sm, 0x300000, rooms, SMZ_SM_ROOMS_MAX);
        if (n > 0) store_write(rp, rooms, (size_t)n * 6 * sizeof(int));
    }
    const char *dp = store_path("derived/dungeons2.bin");   /* 2: Castle Tower without its empty bottom floor */
    if (access(dp, F_OK) != 0) {
        size_t atlas = (size_t)SMZ_DM_ATLAS_W * SMZ_DM_ATLAS_H * 4;
        SmzDungeonMeta *dm = malloc(sizeof *dm + 2 * atlas);
        if (dm && smz_dungeon_maps(z3, 0x100000, dm, (uint8_t *)(dm + 1), (uint8_t *)(dm + 1) + atlas)) store_write(dp, dm, sizeof *dm + 2 * atlas);
        free(dm);
    }
    char lp[512], sp[512];
    snprintf(lp, sizeof lp, "%s", store_path("sprites/link/Original.pv"));
    snprintf(sp, sizeof sp, "%s", store_path("sprites/samus/Original.pv"));
    if (access(lp, F_OK) != 0 || access(sp, F_OK) != 0) {
        uint8_t l[SMZ_PV_LINK_LEN], s[SMZ_PV_SAMUS_LEN];
        if (smz_original_previews(sm, 0x300000, z3, 0x100000, l, s)) { store_write(lp, l, sizeof l); store_write(sp, s, sizeof s); }
    }
    if (access(store_path("goals/" SMZ_GOAL_ICONS_MARK ".pv"), F_OK) != 0) smz_goal_icons(sm, 0x300000, z3, 0x100000, goal_icon_out, NULL);
}

/* The menu canvas as wide as the window's shape allows (320 for 4:3, 426 for 16:9). */
static void fit_width(SDL_Renderer *ren)
{
    int w, h;
    if (settings_active() || welcome_active()) ui_set_width(320);   /* drawn for 320 */
    else if (SDL_GetRendererOutputSize(ren, &w, &h) == 0 && h > 0) ui_set_width((int)((long long)w * UI_H / h));
}

/* The map data built into the program, with the room table taken from the data folder when setup has written it. */
static SmzMapData map_data(void)
{
    static SmzSmRoom rooms[SMZ_SM_ROOMS_MAX];
    SmzMapData d = PC_MAP_DATA;
    size_t n;
    int *raw = (int *)store_read(store_path("derived/sm_rooms.bin"), &n);
    if (raw && n >= 6 * sizeof(int)) {
        int count = (int)(n / (6 * sizeof(int)));
        if (count > SMZ_SM_ROOMS_MAX) count = SMZ_SM_ROOMS_MAX;
        for (int i = 0; i < count; i++) rooms[i] = (SmzSmRoom){raw[i * 6], raw[i * 6 + 1], raw[i * 6 + 2], raw[i * 6 + 3], raw[i * 6 + 4], raw[i * 6 + 5]};
        d.rooms = rooms; d.n_rooms = count;
    }
    free(raw);
    return d;
}

static int g_base_checked;   /* 0 unknown, 1 ready, -1 not ready; reset when ROMs are added */
static int base_ok(void)
{
    if (g_base_checked) return g_base_checked > 0;
    uint8_t *a, *b;
    char err[64];
    if (store_load_base(&a, &b, err, sizeof err)) { g_base_checked = -1; return 0; }
    derive_data(a, b);
    free(a); free(b);
    g_base_checked = 1;
    return 1;
}

/* Keeps a copy of a recognised ROM (without copier header) in the data folder, so the originals can move or go away. */
static int keep_rom(const uint8_t *data, size_t n, int kind, char sm[512], char z3[512])
{
    const uint8_t *start;
    size_t len;
    smz_rom_classify(data, n, &start, &len);
    const char *path = store_path(kind == SMZ_ROM_SM ? "base/supermetroid.sfc" : "base/alttp.sfc");
    if (store_write(path, start, len)) return -1;
    snprintf(kind == SMZ_ROM_SM ? sm : z3, 512, "%s", path);
    return 0;
}

static const char *g_source = "";   /* the file being judged, for the welcome screen */

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : path;
}

static void setup_image(const uint8_t *data, size_t n, char sm[512], char z3[512], const char **msg, int *found)
{
    const uint8_t *start = data;
    size_t len = n;
    int kind = smz_rom_classify(data, n, &start, &len);
    char md5[33];
    smz_md5_hex(kind ? start : data, kind ? len : n, md5);
    int kept = 0;
    if (kind == SMZ_ROM_SM || kind == SMZ_ROM_Z3) {
        if (keep_rom(data, n, kind, sm, z3) == 0) { *msg = smz_rom_message(kind); (*found)++; kept = 1; }
        else *msg = "cannot write into the data folder";
    } else if (!*found) *msg = smz_rom_message(kind);
    welcome_report(g_source, kind, md5, kept);
}

/* One file of a folder: a zip is opened, a ROM-sized file is checked; anything else is skipped without being read. */
static void setup_file(const char *full, const char *name, const char *sm, const char *z3, int *found)
{
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) return;
    size_t len = strlen(name);
    int zip = len > 4 && !strcasecmp(name + len - 4, ".zip");
    if (zip ? st.st_size > 200 * 1024 * 1024 : (st.st_size < 0x100000 || st.st_size > 0x300200)) return;
    size_t n;
    uint8_t *f = store_read(full, &n);
    if (!f) return;
    g_source = name;
    if (smz_is_zip(f, n)) {
        for (int i = 0;; i++) {
            size_t k;
            uint8_t *img = smz_zip_entry(f, n, i, &k);
            if (!img) break;
            const char *m = NULL;
            setup_image(img, k, (char *)sm, (char *)z3, &m, found);
            free(img);
        }
    } else { const char *m = NULL; setup_image(f, n, (char *)sm, (char *)z3, &m, found); }
    free(f);
}


static int g_sprites_dirty;

/* ---- sprite import: .rdc files (with their .png or .pv picture) from a folder tree into the data folder ---- */
static int copy_file(const char *from, const char *to)
{
    size_t n;
    uint8_t *b = store_read(from, &n);
    int ok = b && store_write(to, b, n) == 0;
    free(b);
    return ok;
}

/* The picker picture: the visible part of the PNG, whole-number nearest when it grows, box filter when it shrinks, at most 56 x 56. */
static int make_preview(const char *png, const char *pv)
{
    int w, h, ch;
    uint8_t *im = stbi_load(png, &w, &h, &ch, 4);
    if (!im) return 0;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (im[(y * w + x) * 4 + 3]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) { stbi_image_free(im); return 0; }
    int cw = x1 - x0 + 1, chh = y1 - y0 + 1, ow, oh;
    if (cw <= 56 && chh <= 56) { int k = 56 / cw < 56 / chh ? 56 / cw : 56 / chh; if (k < 1) k = 1; ow = cw * k; oh = chh * k; }
    else { float k = fminf(56.0f / cw, 56.0f / chh); ow = (int)lroundf(cw * k); oh = (int)lroundf(chh * k); if (ow < 1) ow = 1; if (oh < 1) oh = 1; }
    uint8_t *out = malloc(2 + (size_t)ow * oh * 4);
    if (!out) { stbi_image_free(im); return 0; }
    out[0] = (uint8_t)ow; out[1] = (uint8_t)oh;
    for (int oy = 0; oy < oh; oy++)
        for (int ox = 0; ox < ow; ox++) {
            int sx0 = ox * cw / ow, sx1 = (ox + 1) * cw / ow, sy0 = oy * chh / oh, sy1 = (oy + 1) * chh / oh;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            long acc[4] = {0, 0, 0, 0}, cnt = 0;
            for (int y = sy0; y < sy1; y++) for (int x = sx0; x < sx1; x++) { const uint8_t *px = im + ((y0 + y) * w + x0 + x) * 4; for (int c = 0; c < 4; c++) acc[c] += px[c]; cnt++; }
            for (int c = 0; c < 4; c++) out[2 + ((size_t)oy * ow + ox) * 4 + c] = (uint8_t)(acc[c] / cnt);
        }
    int ok = store_write(pv, out, 2 + (size_t)ow * oh * 4) == 0;
    free(out);
    stbi_image_free(im);
    return ok;
}

static int icase_has(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    for (; *s; s++) if (!strncasecmp(s, needle, n)) return 1;
    return 0;
}

/* Walks a folder; a .rdc goes to sprites/samus when its path says Samus, else to sprites/link. Returns how many were copied. */
static int sprites_import(const char *path, int depth)
{
    DIR *d = opendir(path);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 4) n += sprites_import(full, depth + 1); continue; }
        size_t l = strlen(e->d_name);
        if (l < 5 || l > 35 || strcasecmp(e->d_name + l - 4, ".rdc")) continue;
        const char *kind = icase_has(full, "samus") ? "samus" : "link";
        char base[40], dst[1100], src[1100];
        snprintf(base, sizeof base, "%.*s", (int)(l - 4), e->d_name);
        snprintf(dst, sizeof dst, "%s", store_path("sprites/%s/%s.rdc", kind, base));
        if (!copy_file(full, dst)) continue;
        snprintf(src, sizeof src, "%s/%s.png", path, base);
        snprintf(dst, sizeof dst, "%s", store_path("sprites/%s/%s.pv", kind, base));
        if (!make_preview(src, dst)) { snprintf(src, sizeof src, "%s/%s.pv", path, base); copy_file(src, dst); }
        n++;
    }
    closedir(d);
    return n;
}

/* First start: the sprites shipped beside the program (share/smz3/sprites, assets/sprites) are copied into the data folder. */
static void install_bundled_sprites(void)
{
    static const char *const KIND[2] = {"link", "samus"};
    int have = 0;
    DIR *d = opendir(store_path("sprites/link/x") ? store_path("sprites/link") : ".");
    if (d) { struct dirent *e; while ((e = readdir(d))) { size_t l = strlen(e->d_name); if (l > 4 && !strcasecmp(e->d_name + l - 4, ".rdc")) have++; } closedir(d); }
    if (have > 5) return;
    static const char *const ROOTS[] = {"%s/../share/smz3/sprites", "%s/sprites", "%s/assets/sprites", "%s/../../assets/sprites"};
    for (unsigned r = 0; r < sizeof ROOTS / sizeof ROOTS[0]; r++) {
        char root[1024];
        snprintf(root, sizeof root, ROOTS[r], store_exe_dir());
        for (int g = 0; g < 2; g++) {
            char dir[1100];
            snprintf(dir, sizeof dir, "%s/%s", root, KIND[g]);
            DIR *sd = opendir(dir);
            if (!sd) continue;
            struct dirent *e;
            while ((e = readdir(sd))) {
                size_t l = strlen(e->d_name);
                if (l < 4 || e->d_name[0] == '.' || (strcasecmp(e->d_name + l - 3, ".pv") && strcasecmp(e->d_name + l - 4, ".rdc"))) continue;
                char from[1400], to[1100];
                snprintf(from, sizeof from, "%s/%s", dir, e->d_name);
                snprintf(to, sizeof to, "%s", store_path("sprites/%s/%s", KIND[g], e->d_name));
                if (access(to, F_OK) != 0) copy_file(from, to);
            }
            closedir(sd);
        }
        if (store_path("sprites/link/x")) { /* keep going only when nothing was found */ }
        d = opendir(store_path("sprites/link"));
        int n = 0;
        if (d) { struct dirent *e; while ((e = readdir(d))) n++; closedir(d); }
        if (n > 10) return;
    }
}

static void setup_dir(const char *path, char *sm, char *z3, int *found, int depth)
{
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && *found < 2) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 3) setup_dir(full, sm, z3, found, depth + 1); }
        else setup_file(full, e->d_name, sm, z3, found);
    }
    closedir(d);
}

/* Takes a file, a zip or a folder (searched three levels deep), keeps what is Super Metroid (JU) or ALttP (JP v1.0) and records it. Returns a message. */
static const char *setup_path(const char *path)
{
    char sm[512], z3[512];
    store_roms_get(sm, z3);
    const char *msg = "cannot read that file";
    int found = 0;
    struct stat pst;
    if (stat(path, &pst) == 0 && S_ISDIR(pst.st_mode)) {
        static char imported[48];
        msg = "no Super Metroid or ALttP ROM in that folder";
        int sp = sprites_import(path, 0);
        if (sp) { snprintf(imported, sizeof imported, "%d sprites imported", sp); msg = imported; g_sprites_dirty = 1; }
        setup_dir(path, sm, z3, &found, 0);
        if (found) msg = found > 1 ? "both ROMs found" : "one ROM found";
    } else {
        size_t n;
        uint8_t *f = store_read(path, &n);
        if (f) {
            g_source = base_name(path);
            if (smz_is_zip(f, n)) {
                msg = "no Super Metroid or ALttP ROM in that zip";
                for (int i = 0;; i++) {
                    size_t k;
                    uint8_t *img = smz_zip_entry(f, n, i, &k);
                    if (!img) break;
                    const char *m = NULL;
                    setup_image(img, k, sm, z3, &m, &found);
                    if (m && found) msg = m;
                    free(img);
                }
            } else setup_image(f, n, sm, z3, &msg, &found);
            free(f);
        }
    }
    store_roms_set(sm, z3);
    return msg;
}

static const char *accept_rom(const char *path) { return setup_path(path); }

static int cmd_flush(void) { online_flush(); printf("finished runs waiting were sent\n"); return 0; }

static int cmd_setup(int argc, char **argv)
{
    if (!argc) { fprintf(stderr, "usage: smz3 setup <rom | zip | folder> ...\n"); return 2; }
    for (int i = 0; i < argc; i++) printf("%s: %s\n", argv[i], setup_path(argv[i]));
    uint8_t *a, *b;
    char err[200];
    if (store_load_base(&a, &b, err, sizeof err)) { printf("not ready: %s\n", err); return 1; }
    derive_data(a, b);
    free(a); free(b);
    printf("ready: both base ROMs are kept in %s\n", store_path("base"));
    return 0;
}

/* Test hooks: SMZ3_SHOT=file.bmp [SMZ3_SHOT_AFTER=n] saves the window after n presents and exits (SMZ3_SHOT2=file.bmp also
 * saves the tracker's screen in dual-screen mode); SMZ3_QUIT_AFTER=n closes the window normally after n presents; SMZ3_TAB=k
 * opens the tracker on tab k; SMZ3_SCREENS=0/1/2 overrides the Screens option. */
static int shot_frames = -1, shot_after;

static void save_shot(SDL_Renderer *r, const char *path)
{
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
}

/* the tracker's screen; drawn before present(), so its shot is taken one frame ahead of the game's */
static void present2(void)
{
    if (shot_frames >= 0 && shot_frames + 1 == shot_after && getenv("SMZ3_SHOT2")) save_shot(ren2, getenv("SMZ3_SHOT2"));
    SDL_RenderPresent(ren2);
}

static void present(void)
{
    if (shot_frames == -1) { const char *a = getenv("SMZ3_SHOT_AFTER"); shot_after = a ? atoi(a) : 60; shot_frames = getenv("SMZ3_SHOT") ? 0 : -2; }
    if (shot_frames >= 0 && ++shot_frames == shot_after) { save_shot(ren, getenv("SMZ3_SHOT")); exit(0); }
    SDL_RenderPresent(ren);
    static int quit_after = -1, frames;
    if (quit_after < 0) { const char *q = getenv("SMZ3_QUIT_AFTER"); quit_after = q ? atoi(q) : 0; }
    if (quit_after && ++frames == quit_after) { SDL_Event q = {.type = SDL_QUIT}; SDL_PushEvent(&q); }
}

static int g_quit;

/* the monthly seed is fetched in the background; the menu takes it when it arrives */
static char g_month[16];
static int g_month_seed, g_month_ready;
static char g_week[16];
static int g_week_seed, g_week_ready;
/* the three ladder tables are fetched in the background when the player opens them */
static char g_ladder[3][16384];
static int g_ladder_state[3];   /* 0 idle, 1 fetching, 2 ready, 3 failed */
static char g_goals[8192];
static int g_goals_state;   /* as g_ladder_state */
static void *goals_thread(void *arg)
{
    (void)arg;
    static char buf[8192];
    int ok = online_goals_json(buf, sizeof buf);
    if (ok) memcpy(g_goals, buf, sizeof buf);
    g_goals_state = ok ? 2 : 3;
    return NULL;
}

static void *ladder_thread(void *arg)
{
    (void)arg;
    for (int t = 0; t < 3; t++) {
        static char buf[16384];
        int ok = online_ladder_json(t, buf, sizeof buf);
        if (ok) memcpy(g_ladder[t], buf, sizeof buf);
        g_ladder_state[t] = ok ? 2 : 3;
    }
    return NULL;
}

static void *monthly_thread(void *arg) { (void)arg; char l[16]; int s; if (online_monthly(l, &s)) { snprintf(g_month, sizeof g_month, "%s", l); g_month_seed = s; g_month_ready = 1; }
    if (online_weekly(l, &s)) { snprintf(g_week, sizeof g_week, "%s", l); g_week_seed = s; g_week_ready = 1; } return NULL; }
static void save_proof(int slot, const SmzProof *p)
{
    static char text[SMZ_PROOF_MAX * 24];
    size_t n = smz_proof_format(p, text, sizeof text);
    store_write(store_path("runs/slot%d.proof", slot), text, n);
}

static void *flush_thread(void *arg) { (void)arg; online_flush(); return NULL; }
/* the daily challenge's state, fetched in the background for the menu */
static SmzDailyInfo g_daily;
static volatile int g_daily_ready;
static void *daily_thread(void *arg) { (void)arg; SmzDailyInfo d; online_daily_info(&d); g_daily = d; g_daily_ready = 1; return NULL; }
/* the hints of the attempt being played, asked for in the background: 0 idle, 1 asking, 2 answered */
static SmzDailyRun g_hint_run;
static volatile int g_hint_state;
static void *hints_thread(void *arg) { (void)arg; g_hint_state = online_daily_hints(&g_hint_run) == 0 ? 2 : 0; return NULL; }
static void *token_thread(void *arg) { online_ensure_token((int)(intptr_t)arg); return NULL; }
static void detach(void *(*fn)(void *), void *arg) { pthread_t t; if (!pthread_create(&t, NULL, fn, arg)) pthread_detach(t); }
static void refill_slots(void) { store_fill_slots(&ui); for (int i = 0; i < UI_SLOTS; i++) ui_set_slot_online(&ui, i, online_slot_kind(i + 1)); }
static char g_note[200];
static Uint32 g_note_until;
static void note(const char *msg) { snprintf(g_note, sizeof g_note, "%s", msg); g_note_until = SDL_GetTicks() + 6000; }

#define QSLOTS 4
#define LAUNCH_MS 2000       /* from Continue or a new run to the game */
#define LAUNCH_FADE_MS 500   /* the menu reaches black, and stays there for the rest */
/* Quick states of a run, kept as runs/slotN.qK.state. */
/* Returns 1 when it saved or loaded. */
static SmzCounters g_goalc;   /* the world goal counters of the run being played */

typedef struct { int slot, online; int64_t created; char text[640]; } GoalReport;
static void *goals_report_thread(void *arg)
{
    GoalReport *g = arg;
    online_goals_report(g->slot, g->created, g->online, g->text);
    free(g);
    return NULL;
}

static void goals_report(int slot, const SmzRun *run)
{
    GoalReport *g = calloc(1, sizeof *g);
    if (!g) return;
    g->slot = slot; g->created = run->created; g->online = online_slot_kind(slot) > 0;
    if (smz_counters_text(&g_goalc, g->text, sizeof g->text) <= 0) { free(g); return; }
    detach(goals_report_thread, g);
}

static int quick_state(int slot, int q, int save)
{
    char m[64];
    const char *path = store_path("runs/slot%d.q%d.state", slot + 1, q + 1);
    if (save) {
        size_t n = emu_state_size();
        uint8_t *buf = malloc(n);
        int ok = buf && emu_state_save(buf, n) == 0 && store_write(path, buf, n) == 0;
        free(buf);
        char t[640];
        if (ok && smz_counters_text(&g_goalc, t, sizeof t) >= 0) store_write(store_path("runs/slot%d.q%d.goals", slot + 1, q + 1), t, strlen(t));   /* loading it puts the counters back here */
        snprintf(m, sizeof m, ok ? "Saved state %d" : "Could not save state %d", q + 1);
    } else {
        size_t n;
        uint8_t *st = store_read(path, &n);
        int ok = st && emu_state_load(st, n) == 0;
        free(st);
        size_t gn;
        char *gt = ok ? (char *)store_read(store_path("runs/slot%d.q%d.goals", slot + 1, q + 1), &gn) : NULL;
        if (gt) { gt[gn] = 0; smz_counters_load(&g_goalc, gt); free(gt); }
        snprintf(m, sizeof m, ok ? "Loaded state %d" : "State %d is empty", q + 1);
        note(m);
        return ok;
    }
    note(m);
    return 1;
}

/* ---------- playing ---------- */

typedef struct { const char *name; int bit; } Cell;

static int json_num(const char *json, const char *key, int def)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(json, k);
    return p ? atoi(p + strlen(k)) : def;
}

static int json_flag(const char *json, const char *key, int def)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    return !strncmp(p, "true", 4);
}

static float shape_aspect(const char *json)
{
    if (strstr(json, "\"screenShape\":\"wide\"")) return 16.0f / 9;
    if (strstr(json, "\"screenShape\":\"pixel\"")) return 8.0f / 7;
    return 4.0f / 3;
}

static int pad_bits(const Uint8 *k)
{
    int b = 0;
    if (k[SDL_SCANCODE_UP]) b |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    static const int BIT[8] = {PAD_B, PAD_A, PAD_Y, PAD_X, PAD_L, PAD_R, PAD_START, PAD_SELECT};
    for (int a = 0; a < 8; a++) {
        SDL_Keycode key = pad_key(a);
        if (key != SDLK_UNKNOWN && k[SDL_GetScancodeFromKey(key)]) b |= BIT[a];
    }
    b |= pad_snes_bits(pad);
    return b;
}

static SDL_Color C(int r, int g, int b) { SDL_Color c = {r, g, b, 255}; return c; }

static void fmt_time(int64_t ms, char *out, size_t cap)
{
    long long s = ms / 1000;
    snprintf(out, cap, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
}

/* The final time of a run, with hundredths: "1:02:03.45". */
static void fmt_time_cs(int64_t ms, char *out, size_t cap)
{
    long long s = ms / 1000;
    snprintf(out, cap, "%lld:%02lld:%02lld.%02lld", s / 3600, s / 60 % 60, s % 60, ms % 1000 / 10);
}

static void draw_bar(float x, float y, float w, float h, float frac, SDL_Color fill)
{
    gfx_rect(x, y, w, h, C(0x1D, 0x24, 0x30));
    if (frac > 0) gfx_rect(x, y, w * (frac > 1 ? 1 : frac), h, fill);
}


typedef struct {
    const SmzDiscovery *disc; int discovered_only; const uint8_t *in_logic; const uint8_t *wram; size_t wram_len;
    const uint8_t *sram; size_t sram_len; MapView *mv; DungeonView *dv; const DungeonLive *live;
} MiniMap;

/* The small map that follows the player, like the Run tab on the handheld: the dungeon floor while inside one. */
static void mini_map(const SmzTracker *t, const MiniMap *m, float x, float y, float side)
{
    SDL_Rect area = {(int)x, (int)y, (int)side, (int)side};
    if (dungeon_showing(m->dv, m->live)) dungeon_draw(ren, m->dv, area, t, m->in_logic, m->live, m->wram, m->wram_len, m->sram, m->sram_len, !m->discovered_only, SDL_GetTicks());
    else map_draw(ren, m->mv, area, t, m->disc, m->discovered_only, m->in_logic, m->wram, m->wram_len, SDL_GetTicks());
    SDL_RenderSetClipRect(ren, NULL);
    gfx_rect(x - 2, y - 2, side + 4, 2, C(0x25, 0x32, 0x4A));
    gfx_rect(x - 2, y + side, side + 4, 2, C(0x25, 0x32, 0x4A));
    gfx_rect(x - 2, y, 2, side, C(0x25, 0x32, 0x4A));
    gfx_rect(x + side, y, 2, side, C(0x25, 0x32, 0x4A));
}

/* The minimap at the bottom of a side panel whose content ends at y, sized by the video settings. */
static void minimap_at_bottom(const SmzTracker *t, const MiniMap *mm, float x, float w, float h, float y)
{
    float u = fminf(h / 720.0f, w / 230.0f), pad = 18 * u, side = (w - 2 * pad) * settings_minimap_scale();
    if (!mm || h - y < side + 60 * u) return;
    gfx_text(x + pad, h - side - 34 * u, 20 * u, C(0x87, 0x93, 0xA6), 0, "Map");
    mini_map(t, mm, x + pad, h - side - 8 * u, side);
}

/* clock_ms: the time to show instead of the play time (a daily challenge's real clock), or -1 */
/* The daily challenge in the left panel, under the clock: the window, the goal (wrapped) and the next hint; each can be hidden
 * in the video options. Returns the y below it. */
static float daily_panel(const SmzDailyRun *r, int finished, float x, float w, float y, float u)
{
    int64_t now = online_now_ms(), left = r->closes_at - now;
    float px = 18 * u, lh = 25 * u;
    char t[32], s[160];
    if (settings_challenge_show(0)) {
        smz_daily_span(left, t, sizeof t);
        snprintf(s, sizeof s, left > 0 ? "Closes in %s" : "Window closed", t);
        gfx_text(x, y, px, left < 15 * 60000 ? C(0xE0, 0x5A, 0x4F) : C(0xE8, 0xEC, 0xF2), 0, s);
        y += lh;
    }
    if (settings_challenge_show(2)) {   /* word wrap to the panel */
        const char *p = r->goal;
        while (*p) {
            int fit = 0, i = 0;
            while (p[i]) {
                int j = i;
                while (p[j] && p[j] != ' ') j++;
                snprintf(s, sizeof s, "%.*s", j, p);
                if (fit && gfx_text_width(px, s) > w) break;
                fit = j;
                i = p[j] ? j + 1 : j;
            }
            if (!fit) fit = (int)strlen(p);
            snprintf(s, sizeof s, "%.*s", fit, p);
            gfx_text(x, y, px, C(0xF2, 0xA3, 0x3A), 0, s);
            y += lh;
            p += fit;
            while (*p == ' ') p++;
        }
    }
    if (settings_challenge_show(3) && r->next_hint_at && !finished) {
        smz_daily_span(r->next_hint_at - now, t, sizeof t);
        snprintf(s, sizeof s, "Next hint in %s", t);
        gfx_text(x, y, px, C(0x87, 0x93, 0xA6), 0, s);
        y += lh;
    }
    return y;
}

static void panel_left(const SmzTracker *t, const SmzRun *r, float x, float w, float h, const MiniMap *mm, int64_t clock_ms, const SmzDailyRun *dr, int finished)
{
    float u = fminf(h / 720.0f, w / 230.0f), pad = 18 * u, y = 40 * u;
    if (w < 90) return;
    unsigned done = t->zelda_cleared + t->metroid_cleared, total = t->zelda_total + t->metroid_total;
    char s[64];
    snprintf(s, sizeof s, "%u%%", total ? done * 100 / total : 0);
    gfx_text(x + pad, y, 64 * u, C(0xE9, 0xA2, 0x3B), 0, s);
    y += 84 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Zelda");
    snprintf(s, sizeof s, "%u/%u", t->zelda_cleared, t->zelda_total);
    gfx_text(x + w - pad, y, 20 * u, C(0xE8, 0xEC, 0xF2), 2, s);
    y += 26 * u;
    draw_bar(x + pad, y, w - 2 * pad, 10 * u, t->zelda_total ? (float)t->zelda_cleared / t->zelda_total : 0, C(0x4C, 0xC3, 0x8A));
    y += 30 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Metroid");
    snprintf(s, sizeof s, "%u/%u", t->metroid_cleared, t->metroid_total);
    gfx_text(x + w - pad, y, 20 * u, C(0xE8, 0xEC, 0xF2), 2, s);
    y += 26 * u;
    draw_bar(x + pad, y, w - 2 * pad, 10 * u, t->metroid_total ? (float)t->metroid_cleared / t->metroid_total : 0, C(0xE0, 0x5A, 0x4F));
    y += 44 * u;
    fmt_time(clock_ms >= 0 ? clock_ms : t->elapsed_ms ? t->elapsed_ms : r->play_ms, s, sizeof s);
    gfx_text(x + pad, y, 34 * u, C(0xE8, 0xEC, 0xF2), 0, s);
    y += 50 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, r->name);
    if (dr) y = daily_panel(dr, finished, x + pad, w - 2 * pad, y + 34 * u, u) - 30 * u;
    minimap_at_bottom(t, mm, x, w, h, y + 30 * u);
}

static void panel_right(const SmzTracker *t, float x, float w, float h, const MiniMap *mm)
{
    float u = fminf(h / 720.0f, w / 230.0f), pad = 18 * u, y = 40 * u;
    if (w < 90) return;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Crystals");
    y += 28 * u;
    float cell = (w - 2 * pad - 6 * 4 * u) / 7;
    if (cell > 30 * u) cell = 30 * u;
    for (int i = 0; i < 7; i++) {
        float cx = x + pad + i * (cell + 4 * u);
        int has = (t->crystal_mask >> i) & 1;
        gfx_rect(cx, y, cell, cell, has ? C(0x4F, 0xC3, 0xF7) : C(0x1D, 0x24, 0x30));
        char d[2] = {(char)('1' + i), 0};
        gfx_text(cx + cell / 2, y + 4 * u, 22 * u, has ? C(0x10, 0x14, 0x1C) : C(0x87, 0x93, 0xA6), 1, d);
    }
    y += cell + 26 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Bosses");
    y += 28 * u;
    static const char *const B = "KPDR";
    for (int i = 0; i < 4; i++) {
        float cx = x + pad + i * (cell + 4 * u);
        int has = (t->boss_mask >> i) & 1;
        gfx_rect(cx, y, cell, cell, has ? C(0xE0, 0x5A, 0x4F) : C(0x1D, 0x24, 0x30));
        char d[2] = {B[i], 0};
        gfx_text(cx + cell / 2, y + 4 * u, 22 * u, has ? C(0x10, 0x14, 0x1C) : C(0x87, 0x93, 0xA6), 1, d);
    }
    y += cell + 26 * u;
    gfx_text(x + pad, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "Last items");
    y += 28 * u;
    for (int i = 0; i < t->n_recent_items && i < 6; i++, y += 30 * u) {
        const smz3_inv_item *it = &smz3_inventory_items[t->recent_items[i]];
        SDL_Texture *ic = map_item_icon(ren, it->icon);
        float ix = x + pad;
        if (ic) { SDL_SetTextureAlphaMod(ic, 255); SDL_FRect d = {ix, y - 2 * u, 26 * u, 26 * u}; SDL_RenderCopyF(ren, ic, NULL, &d); ix += 32 * u; }
        gfx_text(ix, y, 18 * u, C(0xE8, 0xEC, 0xF2), 0, it->name);
    }
    minimap_at_bottom(t, mm, x, w, h, y);
}

enum { TAB_RUN, TAB_ITEMS, TAB_CHECKS, TAB_MAP, TAB_HELP, TAB_COUNT, TAB_DAILY = TAB_COUNT };   /* the Daily tab only in a daily run */
static const char *const TAB_NAMES[TAB_COUNT + 1] = {"RUN", "ITEMS", "CHECKS", "MAP", "HELP", "DAILY"};

/* The daily challenge over the top-right corner of the game: the window clock (red in its last 15 minutes), the own time
 * from Start, the goal and the time to the next hint; each can be hidden in the video options. */
/* The rewind bar under the game: a direction mark, how far back the cursor is and the minute kept as a track. The picture
 * stays as it is, so the moment can be read. */
static void rewind_bar(SmzRewind *rw, int dir, float gx, float gw, int wh)
{
    float u = wh / 720.0f, bw = fminf(gw - 40 * u, 820 * u), bh = 96 * u, bx = gx + (gw - bw) / 2, by = wh - bh - 22 * u;
    int n = smz_rewind_count(rw), at = smz_rewind_cursor(rw);
    SDL_Color amber = C(0xF2, 0xA3, 0x3A), text = C(0xE8, 0xEC, 0xF2), dim = C(0x87, 0x93, 0xA6);
    gfx_rect(bx, by, bw, bh, (SDL_Color){0x10, 0x16, 0x1F, 228});
    gfx_rect(bx, by, bw, 3 * u, amber);
    float ix = bx + 22 * u, iy = by + 22 * u, is = 16 * u;   /* two triangles one way or the other, or two bars while still */
    if (dir) {
        for (int k = 0; k < 2; k++) {
            float x0 = ix + k * is * 0.9f, tip = dir < 0 ? x0 : x0 + is, base = dir < 0 ? x0 + is : x0;
            float xy[8] = {base, iy, tip, iy + is / 2, tip, iy + is / 2, base, iy + is};
            SDL_Color c4[4] = {amber, amber, amber, amber};
            gfx_quad(xy, c4);
        }
    } else {
        gfx_rect(ix + 2 * u, iy, 6 * u, is, amber);
        gfx_rect(ix + 14 * u, iy, 6 * u, is, amber);
    }
    gfx_text(ix + 42 * u, iy - 3 * u, 22 * u, text, 0, dir < 0 ? "Rewinding" : dir > 0 ? "Forward" : "Rewind");
    char back[24];
    if (at == n - 1) snprintf(back, sizeof back, "Now");
    else snprintf(back, sizeof back, "-%.1f s", (n - 1 - at) / 60.0f);
    gfx_text(bx + bw - 22 * u, iy - 3 * u, 22 * u, at == n - 1 ? dim : text, 2, back);
    float tx = bx + 22 * u, tw = bw - 44 * u, ty = by + 50 * u, f = n > 1 ? (float)at / (n - 1) : 1;
    gfx_rect(tx, ty, tw, 6 * u, C(0x2A, 0x33, 0x44));
    gfx_rect(tx, ty, tw * f, 6 * u, amber);
    gfx_rect(tx + tw * f - 3 * u, ty - 6 * u, 6 * u, 18 * u, text);
    static const HelpItem RW[] = {{"LR", "<", "Back / forward"}, {"A", "Enter", "Play from here"}, {"B", "Esc", "Back to now"}};
    help_prompt(gx + gw / 2, by + 66 * u, 24 * u, RW, 3);
}

static void daily_hud(const SmzDailyRun *r, int finished, float gx, float gw, int wh)
{
    float u = wh / 720.0f, px = 19 * u, lh = 25 * u, pad = 10 * u;
    int64_t now = online_now_ms(), left = r->closes_at - now;
    char line[4][140], t[32];
    SDL_Color col[4];
    int n = 0;
    if (settings_challenge_show(0)) {
        smz_daily_span(left, t, sizeof t);
        snprintf(line[n], sizeof line[n], left > 0 ? "Window  %s" : "Window closed", t);
        col[n++] = left < 15 * 60000 ? C(0xE0, 0x5A, 0x4F) : C(0xE8, 0xEC, 0xF2);
    }
    if (settings_challenge_show(1) && !finished) {
        fmt_time(now - smz_daily_clock_from(r), t, sizeof t);
        snprintf(line[n], sizeof line[n], "Time  %s", t);
        col[n++] = C(0xE8, 0xEC, 0xF2);
    }
    if (settings_challenge_show(2)) { snprintf(line[n], sizeof line[n], "%.60s", r->goal); col[n++] = C(0xF2, 0xA3, 0x3A); }
    if (settings_challenge_show(3) && r->next_hint_at && !finished) {
        smz_daily_span(r->next_hint_at - now, t, sizeof t);
        snprintf(line[n], sizeof line[n], "Next hint in %s", t);
        col[n++] = C(0x87, 0x93, 0xA6);
    }
    if (!n) return;
    float w = 0;
    for (int i = 0; i < n; i++) w = fmaxf(w, gfx_text_width(px, line[i]));
    float x = gx + gw - w - 2 * pad - 8 * u, y = 8 * u;
    gfx_rect(x, y, w + 2 * pad, n * lh + pad, (SDL_Color){0x08, 0x0B, 0x11, 190});
    for (int i = 0; i < n; i++) gfx_text(x + pad, y + pad / 2 + i * lh, px, col[i], 0, line[i]);
}

static void overlay(const SmzTracker *t, const SmzRun *r, int tab, int scroll, int pause, int win_w, int win_h, float ax, float aw, MapView *mv,
                    const SmzDiscovery *disc, int discovered_only, const uint8_t *in_logic, const uint8_t *wram, size_t wram_len,
                    const uint8_t *sram, size_t sram_len, DungeonView *dv, const DungeonLive *live, const SmzDailyRun *dr)
{
    /* ax..ax+aw: the room between the side panels (the whole window when they have none) */
    if (aw < win_w * 0.5f) { ax = win_w * 0.08f; aw = win_w * 0.84f; }
    float u = win_h / 720.0f, x0 = ax + 24 * u, w = aw - 48 * u, y0 = 40 * u;
    SDL_Color back = {0x08, 0x0B, 0x11, 250};
    gfx_rect(x0 - 12 * u, y0 - 16 * u, w + 24 * u, win_h - 2 * y0 + 32 * u, back);
    float x = x0;
    int ntabs = dr ? TAB_COUNT + 1 : TAB_COUNT;
    for (int i = 0; i < ntabs; i++) {
        float tw = fminf(130 * u, w / ntabs);
        gfx_rect(x, y0, tw - 6 * u, 36 * u, i == tab ? C(0x2E, 0x4A, 0x6B) : C(0x1D, 0x24, 0x30));
        gfx_text(x + (tw - 6 * u) / 2, y0 + 5 * u, 24 * u, i == tab ? C(0xE9, 0xA2, 0x3B) : C(0x87, 0x93, 0xA6), 1, TAB_NAMES[i]);
        x += tw;
    }
    float y = y0 + 56 * u, line = 26 * u, bottom = win_h - y0 - 10 * u;
    char s[96];
    if (tab == TAB_RUN) {
        fmt_time(t->elapsed_ms ? t->elapsed_ms : r->play_ms, s, sizeof s);
        gfx_text(x0, y, 30 * u, C(0xE8, 0xEC, 0xF2), 0, r->name); y += 44 * u;
        char row[160];
        snprintf(row, sizeof row, "Time %s    Zelda %u/%u    Metroid %u/%u", s, t->zelda_cleared, t->zelda_total, t->metroid_cleared, t->metroid_total);
        gfx_text(x0, y, 22 * u, C(0x87, 0x93, 0xA6), 0, row); y += 44 * u;
        gfx_text(x0, y, 22 * u, C(0xE9, 0xA2, 0x3B), 0, "Last checks"); y += 32 * u;
        for (int i = 0; i < t->n_recent_checks; i++, y += line) {
            const smz3_location *l = &smz3_locations[t->recent_checks[i]];
            snprintf(row, sizeof row, "%s  -  %s", l->region, l->name);
            gfx_text(x0, y, 20 * u, C(0xE8, 0xEC, 0xF2), 0, row);
        }
        if (t->game == 3) gfx_text(x0, y + 10 * u, 24 * u, C(0x4C, 0xC3, 0x8A), 0, "Run finished");
    } else if (tab == TAB_DAILY && dr) {
        int64_t now = online_now_ms(), left = dr->closes_at - now;
        char a[32], b[32], row[200];
        gfx_text(x0, y, 30 * u, C(0xF2, 0xA3, 0x3A), 0, dr->goal); y += 44 * u;
        smz_daily_span(left, a, sizeof a);
        fmt_time(now - smz_daily_clock_from(dr), b, sizeof b);
        if (left > 0) snprintf(row, sizeof row, "Window closes in %s    Own time %s", a, b);
        else snprintf(row, sizeof row, "The window has closed: no prize    Own time %s", b);
        gfx_text(x0, y, 22 * u, left < 15 * 60000 ? C(0xE0, 0x5A, 0x4F) : C(0x87, 0x93, 0xA6), 0, row); y += 44 * u;
        gfx_text(x0, y, 22 * u, C(0xE9, 0xA2, 0x3B), 0, "Hints"); y += 32 * u;
        if (!dr->n_hints) { gfx_text(x0, y, 20 * u, C(0x87, 0x93, 0xA6), 0, "None yet"); y += line; }
        int first = scroll < dr->n_hints ? scroll : dr->n_hints - 1;
        for (int i = first < 0 ? 0 : first; i < dr->n_hints && y < bottom - 2 * line; i++) {
            snprintf(row, sizeof row, "%3lld min   %s", (long long)((dr->hint_at[i] - dr->started_at) / 60000), dr->hint[i]);
            gfx_text(x0, y, 20 * u, C(0xE8, 0xEC, 0xF2), 0, row);
            y += line;
        }
        if (dr->next_hint_at) {
            smz_daily_span(dr->next_hint_at - now, a, sizeof a);
            snprintf(row, sizeof row, "Next hint in %s", a);
            gfx_text(x0, y + 10 * u, 20 * u, C(0x87, 0x93, 0xA6), 0, row);
        } else gfx_text(x0, y + 10 * u, 20 * u, C(0x87, 0x93, 0xA6), 0, "Every hint is out");
    } else if (tab == TAB_ITEMS) {
        float cell = 56 * u, gap = 10 * u;
        int per = (int)((w + gap) / (cell + gap)), k = 0, last_game = 0;
        if (per < 1) per = 1;
        for (unsigned i = 0; i < smz3_inventory_count && i < SMZ_TRK_MAX_INV; i++) {
            const smz3_inv_item *it = &smz3_inventory_items[i];
            if (it->game != last_game) { if (last_game) y += (k + per - 1) / per * (cell + gap) + 14 * u; k = 0; last_game = it->game; }
            float cx = x0 + (k % per) * (cell + gap), cy = y + (k / per) * (cell + gap);
            int level = t->inventory[i];
            gfx_rect(cx, cy, cell, cell, C(0x1D, 0x24, 0x30));
            SDL_Texture *ic = map_item_icon(ren, it->icon);
            if (ic) {
                SDL_SetTextureAlphaMod(ic, level > 0 ? 255 : 70);
                SDL_FRect d = {cx + 4 * u, cy + 4 * u, cell - 8 * u, cell - 8 * u};
                SDL_RenderCopyF(ren, ic, NULL, &d);
            } else gfx_text(cx + cell / 2, cy + cell / 2 - 8 * u, 12 * u, level > 0 ? C(0xE8, 0xEC, 0xF2) : C(0x4A, 0x55, 0x66), 1, it->name);
            if (level > 1) {
                snprintf(s, sizeof s, "%d", level);
                gfx_text(cx + cell - 3 * u, cy + cell - 20 * u, 16 * u, C(0xE9, 0xA2, 0x3B), 2, s);
            }
            k++;
        }
    } else if (tab == TAB_CHECKS) {
        int rows = (int)((bottom - y) / line), first = scroll;
        if (first > (int)smz3_location_count - rows) first = (int)smz3_location_count - rows;
        if (first < 0) first = 0;
        for (int i = 0; i < rows && first + i < (int)smz3_location_count && first + i < SMZ_TRK_MAX_LOCS; i++) {
            const smz3_location *l = &smz3_locations[first + i];
            int done = t->cleared[first + i];
            snprintf(s, sizeof s, "%s %s  -  %s", done ? "[x]" : "[ ]", l->game == SMZ3_GAME_ZELDA ? "Z" : "M", l->name);
            gfx_text(x0, y + i * line, 19 * u, done ? C(0x4C, 0xC3, 0x8A) : in_logic && !in_logic[first + i] ? C(0x5A, 0x66, 0x78) : C(0xE8, 0xEC, 0xF2), 0, s);
        }
        snprintf(s, sizeof s, "%d-%d of %u", first + 1, first + rows, smz3_location_count);
        gfx_text(x0 + w, bottom, 18 * u, C(0x87, 0x93, 0xA6), 2, s);
    } else if (tab == TAB_MAP) {
        SDL_Rect area = {(int)x0, (int)y, (int)w, (int)(bottom - y - 30 * u)};
        if (dungeon_showing(dv, live)) dungeon_draw(ren, dv, area, t, in_logic, live, wram, wram_len, sram, sram_len, !discovered_only, SDL_GetTicks());
        else {
            map_draw(ren, mv, area, t, disc, discovered_only, in_logic, wram, wram_len, SDL_GetTicks());
            map_legend(ren, x0 + w, area.y + area.h + 4 * u, 18 * u, 0, in_logic != NULL);
        }
    } else {
        static const HelpItem LEFT[] = {
            {"", "", "#Tracker"}, {"R3", "Tab", "Open and close"}, {"LR2", "Q E", "Change tab"}, {NULL, "P", "Pause the game while open"},
            {"", "", "#Playing"}, {"RT", "Space", "Fast forward"}, {"LT", "R", "Rewind (when the run allows it)"}, {"L3", "Esc", "Leave the run (saved)"}, {NULL, "F11", "Full screen"}, {"X", "V", "Video options"},
            {NULL, "F5", "Save state"}, {NULL, "F7", "Load state"}, {NULL, "F6 F8", "State slot"},
            {"SELECT", NULL, "Hold + R1 / L1: save / load state"}, {"SELECT", NULL, "Hold + Left / Right: state slot"},
        };
        static const HelpItem RIGHT[] = {
            {"", "", "#Map"}, {"STICK", "<", "Move the map"}, {"RT", "+", "Zoom in"}, {"LT", "-", "Zoom out"}, {"STICK", NULL, "Right stick up / down: zoom"}, {"A", "F", "Follow the player"},
            {"Y", "D", "Dungeon pictures"}, {"DPAD", "^", "Pick dungeon and floor"}, {"A", "H", "Back to Link's floor"},
            {"", "", "#Keyboard as a pad"}, {NULL, "^", "D-pad (arrows)"}, {NULL, "X", "A"}, {NULL, "Z", "B"}, {NULL, "S", "X"}, {NULL, "A", "Y"},
            {NULL, "Q W", "L and R"}, {NULL, "Enter", "Start"}, {NULL, "Backspace", "Select"},
        };
        int nl = sizeof LEFT / sizeof LEFT[0], nr = sizeof RIGHT / sizeof RIGHT[0];
        float rh = 30 * u, top = y0 + 36 * u, bot = win_h - 44 * u;
        float hl = help_list(0, 0, rh, LEFT, nl, 0), hr = help_list(0, 0, rh, RIGHT, nr, 0), hh = hl > hr ? hl : hr;
        if (hh > bot - top - 20 * u) rh *= (bot - top - 20 * u) / hh, hh = bot - top - 20 * u;   /* small windows: shrink to fit */
        float ty = top + (bot - top - hh) / 2;
        help_list(x0 + w * 0.06f, ty, rh, LEFT, nl, 1);
        help_list(x0 + w * 0.54f, ty, rh, RIGHT, nr, 1);
    }
    HelpItem bar[10];
    int nb = 0;
    bar[nb++] = (HelpItem){"LR2", "Q E", "Tabs"};
    if (tab == TAB_CHECKS) { bar[nb++] = (HelpItem){"UD", "^", "Scroll"}; bar[nb++] = (HelpItem){NULL, "PgUp", "Page"}; }
    else if (tab == TAB_ITEMS || tab == TAB_RUN || tab == TAB_HELP || tab == TAB_DAILY) { bar[nb++] = (HelpItem){"UD", "^", "Scroll"}; }
    else if (tab == TAB_MAP && dungeon_showing(dv, live)) {
        bar[nb++] = (HelpItem){"LR", "<", "Dungeon"}; bar[nb++] = (HelpItem){"UD", "^", "Floor"};
        bar[nb++] = (HelpItem){"A", "H", "Link's floor"}; bar[nb++] = (HelpItem){"Y", "D", "World map"};
    } else if (tab == TAB_MAP) {
        bar[nb++] = (HelpItem){"STICK", "<", "Pan"}; bar[nb++] = (HelpItem){"RT", "+", "Zoom in"}; bar[nb++] = (HelpItem){"LT", "-", "Zoom out"};
        bar[nb++] = (HelpItem){"A", "F", mv->follow ? "Following" : "Follow"}; bar[nb++] = (HelpItem){"Y", "D", "Dungeons"};
    }
    bar[nb++] = (HelpItem){NULL, "P", pause ? "Game paused" : "Game running"};
    bar[nb++] = (HelpItem){"X", "V", "Video"};
    bar[nb++] = (HelpItem){"R3", "Tab", "Close"};
    help_bar(0, win_h - 44 * u, (float)win_w, 44 * u, bar, nb);
}

static void snapshot_run(SmzRun *r, SmzTracker *t, int finished)
{
    r->last_played = (int64_t)time(NULL) * 1000;
    if (r->game == 3) {   /* a finished run played again keeps its final result; its rewards only add up (an earlier build read them in the credits) */
        if (t->started && t->game != 3) { r->boss_mask |= t->boss_mask; r->crystal_mask |= t->crystal_mask; }
        return;
    }
    if (!finished) r->play_ms = t->base_ms + t->pending_ms;
    if (t->started) {
        r->game = (int)t->game;
        r->zelda_cleared = (int)t->zelda_cleared; r->metroid_cleared = (int)t->metroid_cleared;
        r->boss_mask = t->boss_mask; r->crystal_mask = t->crystal_mask;
    }
    r->zelda_total = (int)t->zelda_total; r->metroid_total = (int)t->metroid_total;
    r->save_ms = t->save_ms; r->save_zelda = t->save_zelda; r->save_metroid = t->save_metroid;
}

/* mode 0: the autosave state (or the game save when there is none), 1: the game save only */
/* An online race in play: the rivals' news in a corner, their chat in a fading box, and the result card at the end. */
static struct {
    int on, finish_sent, card, ladder, ended, bosses, crystals, need, last_count;
    int64_t shown;
    char news[96], say[112];
    Uint32 news_until, say_at;
} rg;

static void race_news(const char *text) { snprintf(rg.news, sizeof rg.news, "%s", text); rg.news_until = SDL_GetTicks() + 4000; sfx_play(SFX_OPEN); }

/* First place; with a time it can go to the ladder (a win because the others left has none). */
static int race_first(void) { const SmzRacePlayer *me = smz_race_me(&ui.race.room); return me && me->place == 1; }
static int race_won(void) { const SmzRacePlayer *me = smz_race_me(&ui.race.room); return race_first() && me->time_ms >= 0; }

/* A on the result card: the winner's time goes to the ladder once; otherwise the card closes and the game goes on. */
static void race_card_a(void)
{
    if (race_won() && !rg.ladder && ui.race.room.state == RACE_FINISHED) { rg.ladder = 1; race_send("ladder", "", 1); sfx_play(SFX_CONFIRM); }
    else rg.card = 0;
}

/* The race's answers while in the game, and what they bring: new lines for the corner and the chat box, the end. */
static void race_in_game(void)
{
    static char ans[32768];
    SmzRace *r = &ui.race.room;
    int k;
    while ((k = race_take(ans, sizeof ans)) >= 0) {
        if (k == RACE_ANS_ERROR) race_news(ans);
        else smz_race_read(r, ans, online_now_ms());
    }
    race_set_since(r->since);
    for (int i = 0; i < r->lines; i++) {
        const SmzRaceLine *l = &r->line[i];
        if (l->seq <= rg.shown) continue;
        rg.shown = l->seq;
        int mine = r->you[0] && !strcmp(l->pid, r->you);
        if (l->kind == RACE_LINE_CHAT) { if (!mine) { snprintf(rg.say, sizeof rg.say, "%s: %s", l->name, l->text); rg.say_at = SDL_GetTicks(); } }
        else if (l->kind == RACE_LINE_EVENT) { if (!mine) { char m[112]; snprintf(m, sizeof m, "%s %s", l->name, l->text); race_news(m); } }
        else if (!mine || l->kind == RACE_LINE_SYSTEM) race_news(l->text);
    }
    if (!rg.ended && (r->state == RACE_FINISHED || r->state == RACE_CANCELLED)) { rg.ended = 1; rg.card = 1; }
}

/* The countdown, the news, the chat box, the time left and the result card, over the game at gx..gx+gw. */
static void race_draw(int64_t go_in, float gx, float gw, int ww, int wh, int one_screen)
{
    const SmzRace *r = &ui.race.room;
    float u = wh / 720.0f;
    Uint32 now = SDL_GetTicks();
    if (go_in > 0 || (go_in > -1000 && r->state == RACE_RUNNING)) {
        int n = go_in > 0 ? (int)((go_in + 999) / 1000) : 0;
        if (n != rg.last_count) { rg.last_count = n; if (n <= 3) sfx_play(n ? SFX_TICK : SFX_CONFIRM); }
        if (n > 3) {
            char m[48];
            snprintf(m, sizeof m, "The race starts in %d", n);
            gfx_rect(gx + gw / 2 - 220 * u, wh / 2.0f - 34 * u, 440 * u, 68 * u, (SDL_Color){0x08, 0x0B, 0x11, 215});
            gfx_text(gx + gw / 2, wh / 2.0f - 18 * u, 32 * u, C(0xE8, 0xEC, 0xF2), 1, m);
        } else {
            char m[8];
            if (n) snprintf(m, sizeof m, "%d", n); else snprintf(m, sizeof m, "GO");
            gfx_text(gx + gw / 2, wh / 2.0f - 80 * u, 160 * u, n ? C(0xF2, 0xA3, 0x3A) : C(0x7F, 0xD8, 0x8F), 1, m);
        }
    }
    if (r->state == RACE_RUNNING && r->end_at) {   /* a multi race after the first finish */
        int64_t left = r->end_at - (online_now_ms() + r->offset);
        if (left < 0) left = 0;
        char m[48];
        snprintf(m, sizeof m, "Race ends in %d:%02d", (int)(left / 60000), (int)(left / 1000 % 60));
        float tw = gfx_text_width(22 * u, m);
        gfx_rect(gx + (gw - tw) / 2 - 14 * u, 14 * u, tw + 28 * u, 38 * u, (SDL_Color){0x08, 0x0B, 0x11, 215});
        gfx_text(gx + gw / 2, 21 * u, 22 * u, C(0xF2, 0xA3, 0x3A), 1, m);
    }
    if (now < rg.news_until) {   /* top right of the game */
        float px = 20 * u, tw = fminf(gfx_text_width(px, rg.news), gw * 0.6f);
        gfx_rect(gx + gw - tw - 40 * u, 16 * u, tw + 24 * u, 36 * u, (SDL_Color){0x08, 0x0B, 0x11, 215});
        gfx_rect(gx + gw - tw - 40 * u, 16 * u, 3 * u, 36 * u, (SDL_Color){0x4F, 0xC3, 0xF7, 255});
        gfx_text(gx + gw - 28 * u, 23 * u, px, C(0xE8, 0xEC, 0xF2), 2, rg.news);
    }
    if (one_screen && rg.say_at && now - rg.say_at < 2500) {   /* bottom right of the game: two seconds, then it fades */
        Uint32 t = now - rg.say_at;
        float a = t < 2000 ? 1 : 1 - (t - 2000) / 500.0f, px = 20 * u, tw = fminf(gfx_text_width(px, rg.say), gw * 0.6f);
        gfx_rect(gx + gw - tw - 40 * u, wh - 120 * u, tw + 24 * u, 36 * u, (SDL_Color){0x10, 0x16, 0x1F, (Uint8)(225 * a)});
        SDL_Color c = C(0xE8, 0xEC, 0xF2);
        c.a = (Uint8)(255 * a);
        gfx_text(gx + gw - 28 * u, wh - 113 * u, px, c, 2, rg.say);
    }
    if (!rg.card) return;
    const SmzRacePlayer *me = smz_race_me(r);
    int won = race_won(), cancelled = r->state == RACE_CANCELLED;
    gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){0, 0, 0, 180});
    float pw = 620 * u, ph = (150 + 34 * r->n + 70) * u, px = (ww - pw) / 2, py = (wh - ph) / 2;
    gfx_rect(px, py, pw, ph, (SDL_Color){0x10, 0x16, 0x1F, 240});
    gfx_rect(px, py, pw, 3 * u, won ? (SDL_Color){0xF2, 0xA3, 0x3A, 255} : (SDL_Color){0x87, 0x93, 0xA6, 255});
    gfx_text(ww / 2.0f, py + 22 * u, 44 * u, won ? C(0xF2, 0xA3, 0x3A) : cancelled ? C(0x87, 0x93, 0xA6) : C(0xE0, 0x5A, 0x4F), 1,
             cancelled ? "RACE CANCELLED" : race_first() ? "VICTORY" : "DEFEAT");
    gfx_text(ww / 2.0f, py + 78 * u, 20 * u, C(0x87, 0x93, 0xA6), 1, cancelled && r->end_why[0] ? r->end_why : smz_race_format_name(r->format));
    for (int pl = 0, row = 0; pl <= r->n; pl++) {   /* by place, then the ones without one */
        for (int i = 0; i < r->n; i++) {
            const SmzRacePlayer *p = &r->p[i];
            if ((pl < r->n && p->place != pl + 1) || (pl == r->n && p->place)) continue;
            float ry = py + (120 + 34 * row++) * u;
            char t[24], pos[8];
            if (p->time_ms >= 0) fmt_time_cs(p->time_ms, t, sizeof t); else snprintf(t, sizeof t, "%s", p->left ? "Left" : "-");
            snprintf(pos, sizeof pos, p->place ? "%d" : "", p->place);
            SDL_Color c = p == me ? C(0xF2, 0xA3, 0x3A) : C(0xE8, 0xEC, 0xF2);
            gfx_text(px + 40 * u, ry, 22 * u, c, 0, pos);
            gfx_text(px + 80 * u, ry, 22 * u, c, 0, p->name);
            gfx_text(px + pw - 40 * u, ry, 22 * u, c, 2, t);
        }
    }
    float by = py + ph - 56 * u;
    if (rg.ladder) {
        gfx_text(ww / 2.0f, by - 30 * u, 20 * u, C(0x4F, 0xC3, 0xF7), 1, "Saved to the ladder");
        static const HelpItem DONE[] = {{"B", "Esc", "Close"}};
        help_prompt(ww / 2.0f, by, 40 * u, DONE, 1);
    } else if (won && r->state == RACE_FINISHED) {
        static const HelpItem WIN[] = {{"A", "Enter", "Save to ladder"}, {"B", "Esc", "Close"}};
        help_prompt(ww / 2.0f, by, 40 * u, WIN, 2);
    } else {
        static const HelpItem LOSE[] = {{"A", "Enter", "Keep playing"}, {"B", "Esc", "Close"}};
        help_prompt(ww / 2.0f, by, 40 * u, LOSE, 2);
    }
}

static void play(int slot, int game_save_only)
{
    SmzRun run;
    if (store_run_load(slot + 1, &run)) { note("that run cannot be read"); return; }
    char romp[512];
    if (!run.rom_path[0]) rom_file(&run, romp, sizeof romp); else snprintf(romp, sizeof romp, "%s", run.rom_path);
    size_t rl;
    uint8_t *rom = store_read(romp, &rl);
    if (!rom || rl != SMZ_HOST_ROM_SIZE) { free(rom); note("the ROM of this run is missing; make a new run"); return; }
    music_prepare(romp, run.settings, run.seed);
    if (!smz_rom_shortcut_hooks(rom)) smz_rom_checksum(rom);   /* a run made before the item shortcuts */
    int ok = emu_open(rom, rl, romp) == 0;
    free(rom);
    if (!ok) { note("the emulator could not load the ROM"); return; }

    /* runs that send a time online (the ladders, the daily challenge) and finished runs never allow states, whatever their options */
    int states = json_flag(run.settings, "saveStates", 0) && !online_slot_kind(slot + 1) && !strstr(run.settings, "\"challenge\":true");
    int qslot = 0;
    size_t sl;
    uint8_t *sram = emu_sram(&sl);
    size_t fl;
    uint8_t *saved = store_read(store_path("runs/slot%d.srm", slot + 1), &fl);
    if (saved && sram) memcpy(sram, saved, fl < sl ? fl : sl);
    free(saved);
    if (states && !game_save_only) {
        size_t n;
        uint8_t *st = store_read(store_path("runs/slot%d.state", slot + 1), &n);
        if (st) { emu_state_load(st, n); free(st); }
    }

    SDL_AudioSpec want = {0}, have;
    want.freq = emu_sample_rate(); want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    static SmzDiscovery disc;
    static SmzMapData mapd;
    mapd = map_data();
    smz_disc_init(&disc, &mapd);
    {
        size_t n;
        char *text = (char *)store_read(store_path("runs/slot%d.discovery", slot + 1), &n);
        if (text) { text[n] = 0; smz_disc_load(&disc, text); free(text); }
    }
    int discovered_only = strstr(run.settings, "\"mapMode\":\"discovered\"") != NULL;
    MapView mv, minimv;
    map_view_init(&mv);
    map_view_init(&minimv);
    minimv.follow = 1; minimv.zoom = 1.3f; minimv.highlight = 1;
    DungeonView minidv;
    dungeon_view_init(&minidv);
    /* checks in logic: only when the run lets the tracker show them (all helps) and its seed can be rebuilt */
    static SmzLogic logic;
    uint8_t in_logic[512];
    int have_logic = 0, keysanity = strstr(run.settings, "\"keysanity\":\"Both\"") ? 3 : strstr(run.settings, "\"keysanity\":\"SuperMetroid\"") ? 2 : strstr(run.settings, "\"keysanity\":\"Zelda\"") ? 1 : 0;
    /* reachable checks are marked only when the run asks for it (runs made before the option: only the Cozy ones) */
    int reach = strstr(run.settings, "\"showReachable\"") ? json_flag(run.settings, "showReachable", 0) : !strcmp(run.name, "Cozy run");
    if (strstr(run.settings, "\"challenge\":true")) reach = 0;   /* a daily challenge: collected or not, nothing more */
    dungeon_set_fixed_keys(strstr(run.settings, "\"showFixedKeys\"") ? json_flag(run.settings, "showFixedKeys", 0) : !strcmp(run.name, "Cozy run"));
    if (reach && strstr(run.settings, "\"helpMode\":\"all\"")) {
        LogicJob lj = {&logic, run.settings, -1};
        run_big(logic_thread, &lj);
        have_logic = lj.rc == 0;
    }
    uint32_t logic_key = 0;
    Uint32 next_token = 0;
    int is_default = json_flag(run.settings, "defaultRules", 0), queued = 0;
    memset(in_logic, 0, sizeof in_logic);
    static SmzTracker trk;
    smz_tracker_reset(&trk, run.play_ms, run.save_ms, run.save_zelda, run.save_metroid);
    smz_counters_load(&g_goalc, run.goals);
    Uint32 next_goals = SDL_GetTicks() + 300000;
    smz_goal_from_settings(run.settings, &trk.goal);   /* a daily challenge ends at its goal */
    int challenge = strstr(run.settings, "\"challenge\":true") != NULL;   /* no fast forward (save states are off in its settings) */
    /* rewind, only when the run asks for it and sends nothing online: a state every frame for up to a minute, packed as
     * the changes between frames; without it nothing is kept or saved */
    size_t rw_size = emu_state_size();
    SmzRewind *rw = json_flag(run.settings, "rewind", 0) && !online_slot_kind(slot + 1) && !challenge && rw_size ? smz_rewind_new(rw_size, 64u << 20, 3600) : NULL;
    uint8_t *rw_buf = rw ? malloc(rw_size) : NULL;
    if (!rw_buf) { smz_rewind_free(rw); rw = NULL; }
    int rw_open = 0, rw_dir = 0, rw_shown = -1, rw_cmd = 0;   /* rw_cmd: 1 play from the cursor, 2 back to the present, 3 open */
    Uint32 rw_since = 0, rw_tick = 0;
    int dead_bosses = smz_daily_prepared_bosses(run.settings);   /* a mid-game start keeps these bosses dead */
    static SmzDailyRun dr;   /* the attempt of the daily slot: its clock, goal and hints */
    int daily = slot + 1 == DAILY_SLOT && online_daily_load(&dr) == 0, ntabs = daily ? TAB_COUNT + 1 : TAB_COUNT;
    int fail_popup = 0, fail_seen = run.game == 3, daily_queued = 0;
    Uint32 next_hints = 0, hint_until = 0;
    char hint_note[SMZ_DAILY_HINT_LEN + 8] = "";
    g_hint_state = 0;
    static SmzProof proof;
    {
        size_t pn;
        char *pt = (char *)store_read(store_path("runs/slot%d.proof", slot + 1), &pn);
        if (pt) pt[pn] = 0;
        smz_proof_parse(&proof, pt ? pt : "");
        free(pt);
    }
    int proof_dirty = 0;
    float aspect = shape_aspect(run.settings);
    SDL_Texture *tex = NULL;
    int tw = 0, th = 0, tex_mode = 0, tab = getenv("SMZ3_TAB") ? atoi(getenv("SMZ3_TAB")) % ntabs : TAB_RUN, scroll = 0, show = getenv("SMZ3_TAB") != NULL, pause_on_overlay = 1, leaving = 0, running = 1, finished = 0, fast = 0;
    if (ren2) { pause_on_overlay = 0; show = 0; }   /* dual screen: the tracker is always on its own screen, and the pad never leaves the game */
    if (win2) dual_open(slot + 1, &run);
    int done_popup = 0, finished_before = run.game == 3;   /* the run-complete card, shown once when the run ends in this session */
    int64_t final_ms = 0;
    memset(&rg, 0, sizeof rg);
    rg.on = slot + 1 == RACE_SLOT && race_joined();
    if (rg.on) {
        race_set_slow(1);
        rg.shown = ui.race.room.since;
        int gc = json_num(run.settings, "ganonCrystals", 7), tc = json_num(run.settings, "ganonsTowerCrystals", 7);
        rg.need = (gc > tc ? gc : tc) + json_num(run.settings, "tourianBosses", 4);
    }
    int64_t go_in = 0;   /* ms to the race's start, by the server's clock */
    if (getenv("SMZ3_DONE")) { done_popup = 1; finished = 1; final_ms = atoll(getenv("SMZ3_DONE")); }   /* test hook: the run-complete card */
    int vpanel = getenv("SMZ3_VIDEO") != NULL, vrow = 0, vchanges = 0;   /* the video panel over the game: open, row, changes made (for its sound) */
    Uint32 exit_at = 0;   /* when the player chose to leave: the screen closes before the run list comes back */
    if (getenv("SMZ3_LEAVE")) exit_at = SDL_GetTicks() + (Uint32)atoi(getenv("SMZ3_LEAVE"));   /* test hook: leave after n ms */
    DungeonView dv;
    DungeonLive live = {0};
    int have_live = 0;
    dungeon_available(ren);
    if (ren2) { gfx_use(ren2); dungeon_available(ren2); gfx_use(ren); }
    dungeon_view_init(&dv);
    if (getenv("SMZ3_DUNGEON")) { int dd = atoi(getenv("SMZ3_DUNGEON")); dungeon_step(&dv, NULL, dd - dv.d); }   /* test hook: open a dungeon picture */
    Uint64 freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter(), last_tick = next, last_save = next;
    int64_t last_tick_ms = 0;
    (void)last_tick_ms;
    while (running) {
        {
            size_t lwl;
            const uint8_t *lw = emu_wram(&lwl);
            have_live = lw ? dungeon_live(lw, lwl, &live) : 0;
        }
        const DungeonLive *lp = have_live ? &live : NULL;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int pcode = pad_event_code(&e);
            if (e.type == SDL_KEYDOWN) help_note_keyboard();
            else if (pcode >= 0 || e.type == SDL_CONTROLLERBUTTONDOWN) help_note_pad(pad);
            if (e.type == SDL_QUIT) { running = 0; g_quit = 1; }
            else if (win2 && dual_event(&e, win2, ren2)) continue;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (exit_at) continue;
                if (rg.card) { if (k == SDLK_RETURN || k == SDLK_SPACE) race_card_a(); else if (k == SDLK_ESCAPE) { rg.card = 0; exit_at = SDL_GetTicks(); } continue; }
                if (done_popup && (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE) && finish_skip()) continue;   /* the first press only ends the animation */
                if (done_popup) { if (k == SDLK_RETURN || k == SDLK_SPACE) { done_popup = 0; exit_at = SDL_GetTicks(); } else if (k == SDLK_ESCAPE) done_popup = 0; continue; }   /* main menu, or stay for the credits */
                if (fail_popup) { if (k == SDLK_RETURN || k == SDLK_SPACE) fail_popup = 0; else if (k == SDLK_ESCAPE) { fail_popup = 0; exit_at = SDL_GetTicks(); } continue; }
                if (rw_open) {
                    if (k == SDLK_RETURN || k == pad_key(PA_A)) rw_cmd = 1;
                    else if (k == SDLK_ESCAPE || k == pad_key(PA_B)) rw_cmd = 2;
                    continue;
                }
                if (rw && !show && !leaving && !vpanel && !finished && k == pad_key(PA_REWIND)) { rw_cmd = 3; continue; }
                if (vpanel) {
                    if (k == SDLK_ESCAPE || k == SDLK_v || k == SDLK_BACKSPACE) vpanel = 0;
                    else if (k == SDLK_UP) vrow = (vrow + settings_video_rows() - 1) % settings_video_rows();
                    else if (k == SDLK_DOWN) vrow = (vrow + 1) % settings_video_rows();
                    else if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_RETURN) { settings_video_change(vrow, k == SDLK_LEFT ? -1 : 1); vchanges++; }
                    continue;
                }
                if (!show && !leaving && sc_key_press(k)) continue;   /* an item shortcut; it acts only in normal play */
                if (k == SDLK_v && !leaving) { vpanel = 1; show = 0; continue; }
                if (leaving) { if (k == SDLK_RETURN) exit_at = SDL_GetTicks(); leaving = 0; continue; }
                if (k == SDLK_TAB && !ren2) show = !show;
                else if (states && !show && k == SDLK_F5) quick_state(slot, qslot, 1);
                else if (states && !show && k == SDLK_F7) { if (quick_state(slot, qslot, 0)) trk.state_loaded = 3; }
                else if (states && !show && (k == SDLK_F6 || k == SDLK_F8)) { qslot = (qslot + (k == SDLK_F8 ? 1 : QSLOTS - 1)) % QSLOTS; char m[32]; snprintf(m, sizeof m, "State slot %d", qslot + 1); note(m); }
                else if (k == SDLK_ESCAPE) { if (show) show = 0; else if (finished) exit_at = SDL_GetTicks(); else leaving = 1; }   /* a finished run has nothing to save */
                else if (k == SDLK_F11) SDL_SetWindowFullscreen(win, SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                else if (show && tab == TAB_MAP && k == SDLK_d) dungeon_toggle(&dv, lp);
                else if (show && tab == TAB_MAP && dungeon_showing(&dv, lp) && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN || k == SDLK_h)) {
                    if (k == SDLK_LEFT) dungeon_step(&dv, lp, -1);
                    else if (k == SDLK_RIGHT) dungeon_step(&dv, lp, 1);
                    else if (k == SDLK_UP) dungeon_floor(&dv, lp, 1);
                    else if (k == SDLK_DOWN) dungeon_floor(&dv, lp, -1);
                    else dungeon_here(&dv);
                }
                else if (show) {
                    if (k == SDLK_e || (k == SDLK_RIGHT && tab != TAB_MAP)) tab = (tab + 1) % ntabs;
                    else if (k == SDLK_q || (k == SDLK_LEFT && tab != TAB_MAP)) tab = (tab + ntabs - 1) % ntabs;
                    else if (k == SDLK_p) pause_on_overlay = !pause_on_overlay;
                    else if (k == SDLK_f && tab == TAB_MAP) mv.follow = !mv.follow;
                    else if (k == SDLK_DOWN && tab != TAB_MAP) scroll++;
                    else if (k == SDLK_UP && scroll > 0 && tab != TAB_MAP) scroll--;
                    else if (k == SDLK_PAGEDOWN && tab != TAB_MAP) scroll += 15;
                    else if (k == SDLK_PAGEUP && tab != TAB_MAP) scroll = scroll > 15 ? scroll - 15 : 0;
                }
            } else if (pcode >= 0) {
                int b = pcode;
                if (exit_at) continue;
                if (rg.card) { if (b == SDL_CONTROLLER_BUTTON_A) race_card_a(); else if (b == SDL_CONTROLLER_BUTTON_B) { rg.card = 0; exit_at = SDL_GetTicks(); } continue; }
                if (done_popup && (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_B) && finish_skip()) continue;
                if (done_popup) { if (b == SDL_CONTROLLER_BUTTON_A) { done_popup = 0; exit_at = SDL_GetTicks(); } else if (b == SDL_CONTROLLER_BUTTON_B) done_popup = 0; continue; }
                if (fail_popup) { if (b == SDL_CONTROLLER_BUTTON_A) fail_popup = 0; else if (b == SDL_CONTROLLER_BUTTON_B) { fail_popup = 0; exit_at = SDL_GetTicks(); } continue; }
                if (rw_open) {
                    if (b == SDL_CONTROLLER_BUTTON_A) rw_cmd = 1;
                    else if (b == SDL_CONTROLLER_BUTTON_B) rw_cmd = 2;
                    continue;
                }
                if (win2 && !leaving && !vpanel) {   /* dual screen: the Thor's hotkeys take the place of the PC's actions */
                    int hk = hk_press(pad, b);
                    if (hk == HK_SAVE || hk == HK_LOAD) {
                        if (!states) note("Save states are off in this run");
                        else if (hk == HK_SAVE) quick_state(slot, qslot, 1);
                        else if (quick_state(slot, qslot, 0)) trk.state_loaded = 3;
                    } else if (hk == HK_NEXT) { qslot = (qslot + 1) % QSLOTS; char m[32]; snprintf(m, sizeof m, "Save slot %d", qslot + 1); note(m); }
                    else if (hk == HK_FAST && challenge) note("No fast forward in a daily challenge");
                    else if (hk == HK_REWIND) { if (!rw) note("Rewind is off in this run"); else if (!finished) rw_cmd = 3; }
                    else if (hk == HK_LEAVE) { if (finished) exit_at = SDL_GetTicks(); else leaving = 1; }
                    if (hk >= 0) continue;
                }
                if (!win2 && rw && !show && !leaving && !vpanel && !finished && b == pad_code(PA_REWIND)) { rw_cmd = 3; continue; }
                if (vpanel) {
                    if (b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_X || b == pad_code(PA_TRACKER)) vpanel = 0;
                    else if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) vrow = (vrow + settings_video_rows() - 1) % settings_video_rows();
                    else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) vrow = (vrow + 1) % settings_video_rows();
                    else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || b == SDL_CONTROLLER_BUTTON_A) { settings_video_change(vrow, b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1); vchanges++; }
                    continue;
                }
                if (!show && !leaving && sc_pad_press(b)) continue;
                if (show && b == SDL_CONTROLLER_BUTTON_X) { vpanel = 1; show = 0; continue; }
                if (leaving) { if (b == SDL_CONTROLLER_BUTTON_A) exit_at = SDL_GetTicks(); leaving = 0; continue; }
                int combo = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK);
                int qs = b == pad_code(PA_QSAVE) || (combo && b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
                int ql = b == pad_code(PA_QLOAD) || (combo && b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
                int qp = b == pad_code(PA_QPREV) || (combo && b == SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                int qn = b == pad_code(PA_QNEXT) || (combo && b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                if (!win2 && states && !show && (qs || ql || qp || qn)) {
                    if (qs) quick_state(slot, qslot, 1);
                    else if (ql) { if (quick_state(slot, qslot, 0)) trk.state_loaded = 3; }
                    else { qslot = (qslot + (qn ? 1 : QSLOTS - 1)) % QSLOTS; char m[32]; snprintf(m, sizeof m, "State slot %d", qslot + 1); note(m); }
                }
                else if (b == pad_code(PA_TRACKER)) { if (!ren2) show = !show; }
                else if (b == pad_code(PA_LEAVE)) { if (finished) exit_at = SDL_GetTicks(); else leaving = 1; }
                else if (show && b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) tab = (tab + 1) % ntabs;
                else if (show && b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) tab = (tab + ntabs - 1) % ntabs;
                else if (show && b == SDL_CONTROLLER_BUTTON_DPAD_DOWN && tab != TAB_MAP) scroll++;
                else if (show && b == SDL_CONTROLLER_BUTTON_DPAD_UP && scroll > 0 && tab != TAB_MAP) scroll--;
                else if (show && b == SDL_CONTROLLER_BUTTON_Y && tab == TAB_MAP) dungeon_toggle(&dv, lp);
                else if (show && b == SDL_CONTROLLER_BUTTON_A && tab == TAB_MAP) { if (dungeon_showing(&dv, lp)) dungeon_here(&dv); else mv.follow = !mv.follow; }
            }
        }
        if (win2) {   /* the finish buttons on the tracker's screen */
            int act = dual_take();
            if (act == DUAL_MENU && !exit_at) { done_popup = 0; exit_at = SDL_GetTicks(); }
            else if (act == DUAL_CREDITS) done_popup = 0;
        }
        {   /* the panel's sounds, from what changed since the last frame */
            static int was_show = -1, was_tab, was_scroll, was_leaving, was_pause, was_follow, was_exit, was_vpanel, was_vrow, was_vchanges;
            static DungeonView was_dv;
            if (was_show < 0) { was_show = show; was_tab = tab; was_scroll = scroll; was_pause = pause_on_overlay; was_follow = mv.follow; was_dv = dv; }
            if (exit_at && !was_exit) sfx_play(SFX_CONFIRM);
            else if (vpanel != was_vpanel) sfx_play(vpanel ? SFX_OPEN : SFX_CLOSE);
            else if (vpanel && vchanges != was_vchanges) sfx_play(SFX_SELECT);
            else if (vpanel && vrow != was_vrow) sfx_play(SFX_MOVE);
            else if (leaving != was_leaving) sfx_play(leaving ? SFX_OPEN : SFX_BACK);
            else if (show != was_show) sfx_play(show ? SFX_OPEN : SFX_CLOSE);
            else if (show && tab != was_tab) sfx_play(SFX_TAB);
            else if (show && (pause_on_overlay != was_pause || mv.follow != was_follow || dv.world != was_dv.world)) sfx_play(SFX_SELECT);
            else if (show && (scroll != was_scroll || dv.d != was_dv.d || dv.idx != was_dv.idx)) sfx_play(SFX_MOVE);
            was_show = show; was_tab = tab; was_scroll = scroll; was_leaving = leaving; was_pause = pause_on_overlay; was_follow = mv.follow; was_dv = dv; was_exit = exit_at != 0; was_vpanel = vpanel; was_vrow = vrow; was_vchanges = vchanges;
        }
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (rw) {   /* rewind: the game stands still on a past frame; held directions move through the minute kept */
            SDL_Keycode rk = pad_key(PA_REWIND), fk = pad_key(PA_FAST);
            if (rw_cmd == 3 && !rw_open && smz_rewind_count(rw) > 1) {
                rw_open = 1; rw_shown = -1; rw_dir = 0;
                smz_rewind_open(rw);
                if (audio) SDL_ClearQueuedAudio(audio);
                sfx_rewind(SMZ_RW_SFX_OPEN);
            } else if ((rw_cmd == 1 || rw_cmd == 2) && rw_open) {
                if (rw_cmd == 2) smz_rewind_step(rw, smz_rewind_count(rw));
                emu_state_load(smz_rewind_state(rw), rw_size);
                if (rw_cmd == 1) { smz_rewind_cut(rw); trk.state_loaded = 3; }
                rw_open = 0;
                sfx_rewind(rw_cmd == 1 ? SMZ_RW_SFX_PLAY : SMZ_RW_SFX_CANCEL);
            }
            rw_cmd = 0;
            if (rw_open) {
                int d = pad_dirs(pad);
                int back = keys[SDL_SCANCODE_LEFT] || (rk != SDLK_UNKNOWN && keys[SDL_GetScancodeFromKey(rk)]) || (d & 4) || pad_held(pad, pad_code(PA_REWIND)) || pad_held(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
                int fwd = keys[SDL_SCANCODE_RIGHT] || (fk != SDLK_UNKNOWN && keys[SDL_GetScancodeFromKey(fk)]) || (d & 8) || pad_held(pad, pad_code(PA_FAST)) || pad_held(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
                int dir = back == fwd ? 0 : back ? -1 : 1;
                if (dir != rw_dir) { rw_dir = dir; rw_since = SDL_GetTicks(); }
                if (dir) {   /* real speed first, then twice and four times as fast */
                    Uint32 held = SDL_GetTicks() - rw_since;
                    int sp = held < 1200 ? 1 : held < 3000 ? 2 : 4, at = smz_rewind_cursor(rw);
                    if (smz_rewind_step(rw, dir * sp) != at && SDL_GetTicks() >= rw_tick) {
                        rw_tick = SDL_GetTicks() + (sp == 1 ? 160 : sp == 2 ? 110 : 75);
                        sfx_rewind(dir < 0 ? SMZ_RW_SFX_TICK_BACK : SMZ_RW_SFX_TICK_FWD);
                    }
                }
                if (smz_rewind_cursor(rw) != rw_shown) {   /* the picture of that moment: its state and one silent frame */
                    rw_shown = smz_rewind_cursor(rw);
                    emu_state_load(smz_rewind_state(rw), rw_size);
                    emu_set_pad(0);
                    emu_run_frame();
                    size_t n;
                    emu_take_audio(&n);
                }
            }
        }
        {   /* the D-pad and the left stick choose the dungeon and the floor while the dungeon pictures are up */
            static int last_dirs;
            int dirs = pad_dirs(pad), fresh = dirs & ~last_dirs;
            last_dirs = dirs;
            if (show && tab == TAB_MAP && dungeon_showing(&dv, lp)) {
                if (fresh & 4) dungeon_step(&dv, lp, -1);
                if (fresh & 8) dungeon_step(&dv, lp, 1);
                if (fresh & 1) dungeon_floor(&dv, lp, 1);
                if (fresh & 2) dungeon_floor(&dv, lp, -1);
            }
        }
        if (show && tab == TAB_MAP && !dungeon_showing(&dv, lp)) {
            float sp = 14, dx = 0, dy = 0, dz = 1;
            if (keys[SDL_SCANCODE_LEFT]) dx -= sp; if (keys[SDL_SCANCODE_RIGHT]) dx += sp;
            if (keys[SDL_SCANCODE_UP]) dy -= sp; if (keys[SDL_SCANCODE_DOWN]) dy += sp;
            if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS] || keys[SDL_SCANCODE_PAGEUP]) dz = 1.03f;
            if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS] || keys[SDL_SCANCODE_PAGEDOWN]) dz = 1 / 1.03f;
            if (pad) {
                int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
                if (abs(lx) > 8000) dx += sp * lx / 32767.0f; if (abs(ly) > 8000) dy += sp * ly / 32767.0f;
                if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) dz = 1.03f;
                if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000) dz = 1 / 1.03f;
                int ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);   /* right stick up zooms in, down zooms out, like the triggers */
                if (abs(ry) > 8000) dz = powf(1.03f, -ry / 32767.0f);
            }
            map_view_move(&mv, dx, dy, dz);
        }
        if (win2) hk_tick(pad);
        if (rg.on) { race_in_game(); go_in = ui.race.room.start_at ? ui.race.room.start_at - (online_now_ms() + ui.race.room.offset) : 0; }
        int waiting = rg.on && go_in > 0;   /* no input until the start; the game's intro plays meanwhile */
        int input = show || leaving || exit_at || done_popup || fail_popup || vpanel || rw_open || waiting || rg.card ? 0 : pad_bits(keys);
        SDL_Keycode ffk = pad_key(PA_FAST);
        fast = !challenge && !rw_open && ((ffk != SDLK_UNKNOWN && keys[SDL_GetScancodeFromKey(ffk)] && !show) || (win2 ? hk_held(pad, HK_FAST) : pad_held(pad, pad_code(PA_FAST))));
        int ff = win2 ? settings_ff_speed() : 4, frames = fast ? ff : 1;
        int hold = (show && pause_on_overlay) || leaving || exit_at || done_popup || fail_popup || vpanel || rw_open || rg.card;
        if (!hold) {
            emu_set_pad(input);
            /* fast forward keeps the music at its pitch: one frame of sound out of every four, each one blended into the
             * start of the next frame's sound so the joins do not click */
            enum { XF = 64 };
            static int16_t grain[2 * 4096], tail[2 * XF];
            static int have_tail;
            size_t gn = 0;
            for (int f = 0; f < frames; f++) {
                emu_run_frame();
                size_t n;
                const int16_t *a = emu_take_audio(&n);
                if (!audio || !n) continue;
                if (!fast) { SDL_QueueAudio(audio, a, (Uint32)(n * 4)); have_tail = 0; continue; }
                if (f == 0) { gn = n < 4096 ? n : 4096; memcpy(grain, a, gn * 4); }
                else if (f == 1 && n >= XF) {
                    for (int i = 0; i < XF && have_tail && (size_t)i < gn; i++) {
                        float w = (float)i / XF;
                        grain[2 * i] = (int16_t)(tail[2 * i] * (1 - w) + grain[2 * i] * w);
                        grain[2 * i + 1] = (int16_t)(tail[2 * i + 1] * (1 - w) + grain[2 * i + 1] * w);
                    }
                    memcpy(tail, a, sizeof tail);
                    have_tail = 1;
                }
            }
            if (fast && gn) SDL_QueueAudio(audio, grain, (Uint32)(gn * 4));
            if (rw && !finished && emu_state_save(rw_buf, rw_size) == 0) smz_rewind_push(rw, rw_buf);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        if (!rw_open && (now - last_tick) * 1000 / freq >= 100) {   /* rewinding: the clock's time is counted on the first read after; often enough that the final time is taken close to the real end */
            int dt = (int)((now - last_tick) * 1000 / freq);
            last_tick = now;
            size_t wl;
            const uint8_t *wram = emu_wram(&wl);
            sram = emu_sram(&sl);
            if (wram && sram) {
                if (dead_bosses) smz_prepared_apply(dead_bosses, (uint8_t *)wram, wl, sram, sl);   /* the emulator's own RAM */
                smz_tracker_tick(&trk, wram, wl, sram, sl, dt, fast ? ff : 1);
                smz_counters_tick(&g_goalc, trk.started ? (int)trk.game : 0, wram, wl, sram, sl, (int)(trk.zelda_cleared + trk.metroid_cleared));
                if (trk.save_changed) store_write(store_path("runs/slot%d.srm", slot + 1), sram, sl);
                if (daily && trk.started && smz_daily_note_began(&dr, online_now_ms())) online_daily_save(&dr);   /* the clock starts with the game, as in the ladder */
                if (rg.on && trk.started && ((trk.boss_mask & ~rg.bosses) || (trk.crystal_mask & 0x7F & ~rg.crystals))) {   /* news for the rivals */
                    static const char *const BOSS[4] = {"beat Kraid", "beat Phantoon", "beat Draygon", "beat Ridley"};
                    int nb = trk.boss_mask & ~rg.bosses & 15, nc = trk.crystal_mask & 0x7F & ~rg.crystals;
                    for (int i = 0; i < 4; i++) if (nb >> i & 1) race_event(BOSS[i]);
                    for (int i = 0; i < 7; i++) if (nc >> i & 1) { char m[24]; snprintf(m, sizeof m, "got Crystal %d", i + 1); race_event(m); }
                    rg.bosses |= nb; rg.crystals |= nc;
                    int got = __builtin_popcount((unsigned)rg.bosses) + __builtin_popcount((unsigned)rg.crystals), pc = rg.need ? 100 * got / rg.need : 0;
                    race_set_progress(pc > 99 ? 99 : pc);   /* 100 is the finish, from the server */
                }
                if (trk.game == 3 && !finished) {
                    finished = 1;
                    states = 0;   /* no state can take a finished run back */
                    if (!finished_before) { done_popup = !rg.on; final_ms = trk.base_ms + trk.pending_ms; }   /* a race has its own card */
                    if (rg.on && !rg.finish_sent && ui.race.room.state == RACE_RUNNING) {   /* the time is the server's: from the start to this request */
                        rg.finish_sent = 1;
                        size_t cap = SMZ_PROOF_MAX * 24;
                        char *text = malloc(cap), *q = malloc(cap * 2 + 8), *f = malloc(cap * 2 + 16);
                        if (text && q && f) {
                            smz_proof_format(&proof, text, cap);
                            smz_race_quote(text, q, cap * 2 + 8);
                            snprintf(f, cap * 2 + 16, "\"proof\":%s", q);
                            race_send("finish", f, 1);
                        }
                        free(text); free(q); free(f);
                        save_proof(slot + 1, &proof);
                    }
                    else if (win2) finish_start();   /* a finished run played again: its details come up on the tracker's screen */
                    if (daily && !finished_before && !daily_queued) {   /* the challenge's time is the real clock from the game's start */
                        daily_queued = 1;
                        final_ms = online_now_ms() - smz_daily_clock_from(&dr);
                        save_proof(DAILY_SLOT, &proof);
                        online_daily_queue_finish(final_ms);
                        detach(flush_thread, NULL);
                    }
                }
                if (trk.started && smz_proof_note(&proof, trk.cleared, SMZ_TRK_MAX_LOCS, trk.base_ms + trk.pending_ms)) proof_dirty = 1;
                if (trk.started && trk.running && SDL_GetTicks() >= next_token && online_slot_kind(slot + 1)) { next_token = SDL_GetTicks() + 60000; detach(token_thread, (void *)(intptr_t)(slot + 1)); }
                if (finished && !finished_before && !queued && is_default && online_slot_kind(slot + 1)) {   /* a run is sent once, in the session it ends */
                    queued = 1;
                    save_proof(slot + 1, &proof);
                    online_queue_finish(slot + 1, trk.base_ms + trk.pending_ms, atoi(run.seed));
                    detach(flush_thread, NULL);
                }
                if (trk.started && !hold) smz_disc_update(&disc, wram, wl, (int)trk.game);
                if (have_logic && (trk.started || getenv("SMZ3_LOGIC_ALWAYS"))) {
                    int items[SMZ_TRK_MAX_ITEMS];
                    int ni = smz_tracker_logic_items(&trk, wram, wl, sram, sl, keysanity, items, SMZ_TRK_MAX_ITEMS), key = 17;
                    uint32_t h = 2166136261u;
                    for (int q = 0; q < ni; q++) h = (h ^ (uint32_t)items[q]) * 16777619u;
                    (void)key;
                    if (h != logic_key && !hints_busy()) { logic_key = h; smz_logic_reachable(&logic, items, ni, in_logic, 512); }
                }
            }
        }
        if (daily) {
            int64_t wall = online_now_ms();
            if (!finished && !fail_seen && wall >= dr.closes_at) { fail_popup = fail_seen = 1; show = 0; }
            if (g_hint_state == 2) {
                if (g_hint_run.n_hints > dr.n_hints && settings_challenge_show(2)) {
                    snprintf(hint_note, sizeof hint_note, "Hint: %s", g_hint_run.hint[g_hint_run.n_hints - 1]);
                    hint_until = SDL_GetTicks() + 15000;
                    sfx_play(SFX_OPEN);
                }
                int64_t began = dr.began_at;   /* set while the hints were on their way */
                dr = g_hint_run;
                if (began) dr.began_at = began;
                online_daily_save(&dr);
                g_hint_state = 0;
            }
            if (g_hint_state == 0 && dr.next_hint_at && wall >= dr.next_hint_at && SDL_GetTicks() >= next_hints && !finished) {
                next_hints = SDL_GetTicks() + 15000;
                g_hint_run = dr;
                g_hint_state = 1;
                detach(hints_thread, NULL);
            }
        }
        if ((now - last_save) * 1000 / freq >= 5000) {
            last_save = now;
            snapshot_run(&run, &trk, finished);
            smz_counters_text(&g_goalc, run.goals, sizeof run.goals);
            store_run_save(slot + 1, &run);
            if (SDL_GetTicks() >= next_goals) { next_goals = SDL_GetTicks() + 300000; goals_report(slot + 1, &run); }
            if (proof_dirty) { save_proof(slot + 1, &proof); proof_dirty = 0; }
            if (disc.dirty) {
                char text[8192];
                if (smz_disc_save(&disc, text, sizeof text) > 0) store_write(store_path("runs/slot%d.discovery", slot + 1), text, strlen(text));
                disc.dirty = 0;
            }
        }

        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_SetRenderDrawColor(ren, 0x08, 0x0A, 0x10, 255);
        SDL_RenderClear(ren);
        int pw, ph, pitch;
        const uint32_t *pic = emu_picture(&pw, &ph, &pitch);
        int va = settings_aspect(), vf = settings_filter();
        float asp = va == 1 ? 4.0f / 3 : va == 2 ? 8.0f / 7 : aspect;
        float gh = (float)wh, gw = gh * asp;
        if (va == 3) { gw = (float)ww; gh = (float)wh; }
        else if (va == 4 && pic && pw > 0) {
            int sc = ww / pw < wh / ph ? ww / pw : wh / ph;
            if (sc < 1) sc = 1;
            gw = (float)(pw * sc); gh = (float)(ph * sc);
        } else if (gw > ww) { gw = (float)ww; gh = gw / asp; }
        float gx = (ww - gw) / 2, gy = (wh - gh) / 2;
        if (pic && pw > 0) {
            if (!tex || tw != pw || th != ph || tex_mode != (vf == 1)) {
                if (tex) SDL_DestroyTexture(tex);
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, vf == 1 ? "linear" : "nearest");
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pw, ph);
                SDL_SetTextureScaleMode(tex, vf == 1 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
                tw = pw; th = ph; tex_mode = vf == 1;
            }
            SDL_UpdateTexture(tex, NULL, pic, pitch * 4);
            SDL_FRect dst = {gx, gy, gw, gh};
            SDL_RenderCopyF(ren, tex, NULL, &dst);
            if (vf >= 2 && gh / ph >= 2.0f) {
                float cell = gh / ph;
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, vf == 2 ? 110 : 80);
                for (int i = 0; i < ph; i++) { SDL_FRect l = {gx, gy + i * cell + cell * 0.6f, gw, cell * 0.4f}; SDL_RenderFillRectF(ren, &l); }
                if (vf == 3) {
                    float cw = gw / pw;
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 60);
                    for (int i = 0; i < pw; i++) { SDL_FRect l = {gx + i * cw + cw * 0.7f, gy, cw * 0.3f, gh}; SDL_RenderFillRectF(ren, &l); }
                }
            }
        }
        {
            size_t mwl;
            const uint8_t *mw = emu_wram(&mwl);
            MiniMap mm = {&disc, discovered_only, have_logic ? in_logic : NULL, mw, mwl, sram, sl, &minimv, &minidv, have_live ? &live : NULL};
            int on = trk.started || getenv("SMZ3_MINIMAP"), mside = settings_minimap_side();
            panel_left(&trk, &run, 0, gx, (float)wh, on && mside == 1 ? &mm : NULL, !daily ? -1 : finished ? final_ms : online_now_ms() - smz_daily_clock_from(&dr), daily ? &dr : NULL, finished);
            panel_right(&trk, gx + gw, ww - (gx + gw), (float)wh, on && mside == 0 ? &mm : NULL);
        }
        if (daily && !show && gx < 90) daily_hud(&dr, finished, gx, gw, wh);
        if (rw_open) rewind_bar(rw, rw_dir, gx, gw, wh);   /* no side panels: the only room left is a corner of the game */
        {   /* a new hint, and the short notes (states, slots) while playing */
            const char *msg = SDL_GetTicks() < hint_until ? hint_note : SDL_GetTicks() < g_note_until ? g_note : NULL;
            if (msg && !show && !rw_open) {
                float u = wh / 720.0f, px = 22 * u, tw2 = fminf(gfx_text_width(px, msg), gw - 40 * u);
                gfx_rect(gx + (gw - tw2) / 2 - 14 * u, wh - 64 * u, tw2 + 28 * u, 40 * u, (SDL_Color){0x08, 0x0B, 0x11, 215});
                gfx_text(gx + gw / 2, wh - 57 * u, px, msg == hint_note ? C(0xF2, 0xA3, 0x3A) : C(0xE8, 0xEC, 0xF2), 1, msg);
            }
        }
        if (show && !ren2) {
            size_t owl;
            const uint8_t *ow = emu_wram(&owl);
            overlay(&trk, &run, tab, scroll, pause_on_overlay, ww, wh, (float)gx, (float)gw, &mv, &disc, discovered_only, have_logic ? in_logic : NULL, ow, owl, sram, sl, &dv, have_live ? &live : NULL, daily ? &dr : NULL);
        }
        if (done_popup == 1) { finish_start(); done_popup = 2; }
        if (ren2) {   /* dual screen: the tracker fills the bottom display; the helpers draw on the global renderer, so it is swapped */
            SDL_Renderer *top = ren;
            ren = ren2;
            gfx_use(ren2);
            int bw, bh;
            SDL_GetRendererOutputSize(ren2, &bw, &bh);
            SDL_SetRenderDrawColor(ren2, 0x08, 0x0A, 0x10, 255);
            SDL_RenderClear(ren2);
            size_t owl;
            const uint8_t *ow = emu_wram(&owl);
            FinishCard fc = {daily ? "CHALLENGE COMPLETE" : "RUN COMPLETE", run.name, "", C(0x87, 0x93, 0xA6), finished_before ? run.play_ms : final_ms,
                             finished_before ? (unsigned)run.zelda_cleared : trk.zelda_cleared, trk.zelda_total,
                             finished_before ? (unsigned)run.metroid_cleared : trk.metroid_cleared, trk.metroid_total};
            DualFrame df = {&trk, &disc, discovered_only, have_logic ? in_logic : NULL, ow, sram, owl, sl, have_live ? &live : NULL, daily ? &dr : NULL,
                            !daily ? -1 : finished ? final_ms : online_now_ms() - smz_daily_clock_from(&dr), finished ? &fc : NULL, done_popup != 0, states, QSLOTS, &qslot};
            dual_draw(ren2, &df, bw, bh);
            present2();
            ren = top;
            gfx_use(top);
        }
        if (rg.on && !rw_open) race_draw(go_in, gx, gw, ww, wh, !ren2);
        if (leaving) {
            int abandon = rg.on && ui.race.room.state == RACE_RUNNING;
            gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){0, 0, 0, 170});
            gfx_text(ww / 2.0f, wh / 2.0f - 30, 34, C(0xE8, 0xEC, 0xF2), 1, abandon ? "Abandon the race?" : "Leave the run?");
            static const HelpItem LEAVE[] = {{"A", "Enter", "Save and go to the run list"}, {"B", "Esc", "Keep playing"}};
            static const HelpItem ABANDON[] = {{"A", "Enter", "Leave the race"}, {"B", "Esc", "Keep racing"}};
            help_prompt(ww / 2.0f, wh / 2.0f + 14, 40, abandon ? ABANDON : LEAVE, 2);
        }
        if (vpanel) {   /* the video options over the game, so each change shows at once */
            int nrows = settings_video_rows();
            float u = wh / 720.0f, pw2 = 560 * u, rh = 44 * u, ph2 = 52 * u + nrows * rh + 16 * u, px = (ww - pw2) / 2, py = wh - ph2 - 64 * u;
            gfx_rect(px, py, pw2, ph2, (SDL_Color){0x10, 0x16, 0x1F, 235});
            gfx_rect(px, py, pw2, 2 * u, (SDL_Color){0xF2, 0xA3, 0x3A, 255});
            gfx_text(px + 20 * u, py + 12 * u, 22 * u, C(0xF2, 0xA3, 0x3A), 0, "Video");
            for (int i = 0; i < nrows; i++) {
                float ry = py + 52 * u + i * rh;
                char v[48], vv[56];
                const char *lab = settings_video_row(i, v, sizeof v);
                if (i == vrow) gfx_rect(px + 10 * u, ry, pw2 - 20 * u, rh - 6 * u, (SDL_Color){0x3A, 0x2C, 0x14, 255});
                gfx_text(px + 24 * u, ry + 7 * u, 22 * u, i == vrow ? C(0xF2, 0xA3, 0x3A) : C(0xE8, 0xEC, 0xF2), 0, lab);
                snprintf(vv, sizeof vv, "<  %s  >", v);
                gfx_text(px + pw2 - 24 * u, ry + 7 * u, 22 * u, C(0xE8, 0xEC, 0xF2), 2, vv);
            }
            static const HelpItem VB[] = {{"UD", "^", "Option"}, {"LR", "<", "Change"}, {"B", "Esc", "Close"}};
            help_bar(0, wh - 44 * u, (float)ww, 44 * u, VB, 3);
        }
        if (done_popup) {   /* the run-complete card: the final time with hundredths */
            const char *lad = !online_slot_kind(slot + 1) ? "Offline run" : is_default ? "Sent to the online ladder" : "Changed rules: not on the ladder";
            char res[160];
            if (daily) {   /* the server's answer once it comes back */
                static Uint32 next_look;
                static char last[160];
                if (SDL_GetTicks() >= next_look) { next_look = SDL_GetTicks() + 500; if (!online_daily_result(last, sizeof last)) snprintf(last, sizeof last, "Sending the time..."); }
                snprintf(res, sizeof res, "%s", smz_daily_clock_from(&dr) + final_ms > dr.closes_at && !strncmp(last, "Send", 4) ? "After the window closed: on the late list, no prize" : last);
                lad = res;
            }
            FinishCard fc = {daily ? "CHALLENGE COMPLETE" : "RUN COMPLETE", run.name, lad,
                             daily || (online_slot_kind(slot + 1) && is_default) ? C(0x4F, 0xC3, 0xF7) : C(0x87, 0x93, 0xA6),
                             final_ms, trk.zelda_cleared, trk.zelda_total, trk.metroid_cleared, trk.metroid_total};
            finish_draw(&fc, ww, wh);
        }
        if (finished && !done_popup && gx > 0 && !ren2) {   /* the credits: the run's details instead of the tracker */
            FinishCard fc = {daily ? "CHALLENGE COMPLETE" : "RUN COMPLETE", run.name, "", C(0x87, 0x93, 0xA6), finished_before ? run.play_ms : final_ms,
                             finished_before ? (unsigned)run.zelda_cleared : trk.zelda_cleared, trk.zelda_total,
                             finished_before ? (unsigned)run.metroid_cleared : trk.metroid_cleared, trk.metroid_total};
            finish_panel(&fc, 0, gx, (float)wh);
        }
        if (finished && !done_popup && !exit_at && !show) {   /* the credits: the way out stays on screen */
            float u = wh / 720.0f;
            const char *nm = pad_code_name(pad_code(PA_LEAVE));
            const char *icon = !strcmp(nm, "Back") ? "SELECT" : !strcmp(nm, "Start") ? "START" : !strcmp(nm, "LB") ? "L" : !strcmp(nm, "RB") ? "R" : nm;
            HelpItem back[] = {{icon, "Esc", "Back to menu"}};
            gfx_rect(ww / 2.0f - 142 * u, wh - 62 * u, 284 * u, 46 * u, (SDL_Color){0x10, 0x16, 0x1F, 210});
            gfx_rect(ww / 2.0f - 142 * u, wh - 62 * u, 284 * u, 3 * u, (SDL_Color){0xF2, 0xA3, 0x3A, 255});
            help_prompt(ww / 2.0f, wh - 54 * u, 30 * u, back, 1);
        }
        if (fail_popup) {
            float u = wh / 720.0f;
            gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){0, 0, 0, 180});
            gfx_text(ww / 2.0f, wh / 2.0f - 90 * u, 40 * u, C(0xE0, 0x5A, 0x4F), 1, "TIME IS UP");
            gfx_text(ww / 2.0f, wh / 2.0f - 30 * u, 22 * u, C(0xE8, 0xEC, 0xF2), 1, "The window closed before the goal: this attempt is marked failed.");
            gfx_text(ww / 2.0f, wh / 2.0f, 22 * u, C(0x87, 0x93, 0xA6), 1, "You can keep playing it. A finish now goes on the late list, without prize.");
            static const HelpItem FAIL[] = {{"A", "Enter", "Keep playing"}, {"B", "Esc", "Leave the run"}};
            help_prompt(ww / 2.0f, wh / 2.0f + 50 * u, 40 * u, FAIL, 2);
        }
        if (exit_at && SDL_GetTicks() >= exit_at) {   /* an old TV switching off: the picture closes to a bright line, which fades */
            float t = (SDL_GetTicks() - exit_at) / 450.0f, p = t < 1 ? t * t * (3 - 2 * t) : 1, half = wh / 2.0f;
            float open = half * (1 - p);
            gfx_rect(0, 0, (float)ww, half - open, (SDL_Color){0x0A, 0x0E, 0x15, 255});
            gfx_rect(0, half + open, (float)ww, half - open + 1, (SDL_Color){0x0A, 0x0E, 0x15, 255});
            gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){0x0A, 0x0E, 0x15, (Uint8)(150 * p)});
            float lw = ww * (t < 0.7f ? 1 : 1 - (t - 0.7f) / 0.3f * 0.9f), glow = 1 - (t > 0.7f ? (t - 0.7f) / 0.3f : 0);
            gfx_rect((ww - lw) / 2, half - 6, lw, 12, (SDL_Color){0xF2, 0xA3, 0x3A, (Uint8)(70 * glow)});
            gfx_rect((ww - lw) / 2, half - 1.5f, lw, 3, (SDL_Color){0xFF, 0xE7, 0xA8, (Uint8)(255 * glow)});
            if (t >= 1) { running = 0; fx_screen_changed(); }
        }
        present();

        if (audio && !rw_open) {   /* the sound sets the pace, fast forward too (four frames for each frame of sound) */
            while (running && SDL_GetQueuedAudioSize(audio) > (Uint32)(have.freq / 60 * 4 * 3)) SDL_Delay(1);
        } else if (!fast) {
            next += freq * 1000 / 60988;
            Uint64 t2 = SDL_GetPerformanceCounter();
            if (next > t2) SDL_Delay((Uint32)((next - t2) * 1000 / freq)); else next = t2;
        }
    }

    if (win2) dual_close();
    if (rg.on) {   /* out before the finish: the rivals see it at once */
        const SmzRacePlayer *me = smz_race_me(&ui.race.room);
        if (ui.race.room.state == RACE_RUNNING && (!me || me->time_ms < 0)) race_send("leave", "", 1);
        rg.on = 0;
    }
    if (rw_open) { smz_rewind_step(rw, smz_rewind_count(rw)); emu_state_load(smz_rewind_state(rw), rw_size); }   /* closed mid-rewind: the present is kept */
    smz_rewind_free(rw);
    free(rw_buf);
    size_t wl;
    emu_wram(&wl);
    sram = emu_sram(&sl);
    if (sram) store_write(store_path("runs/slot%d.srm", slot + 1), sram, sl);
    if (states) {
        size_t n = emu_state_size();
        uint8_t *buf = malloc(n);
        if (buf && emu_state_save(buf, n) == 0) store_write(store_path("runs/slot%d.state", slot + 1), buf, n);
        free(buf);
    }
    snapshot_run(&run, &trk, finished);
    smz_counters_text(&g_goalc, run.goals, sizeof run.goals);
    store_run_save(slot + 1, &run);
    goals_report(slot + 1, &run);
    if (audio) SDL_CloseAudioDevice(audio);
    if (tex) SDL_DestroyTexture(tex);
    emu_close();
}

/* ---------- the window ---------- */

static UiButton map_key(SDL_Keycode k, int *ok)
{
    *ok = 1;
    switch (k) {
    case SDLK_UP: return BTN_UP; case SDLK_DOWN: return BTN_DOWN; case SDLK_LEFT: return BTN_LEFT; case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_RETURN: case SDLK_x: case SDLK_SPACE: return BTN_A;
    case SDLK_ESCAPE: case SDLK_z: case SDLK_BACKSPACE: return BTN_B;
    case SDLK_q: case SDLK_PAGEUP: return BTN_L;
    case SDLK_w: case SDLK_PAGEDOWN: return BTN_R;
    case SDLK_TAB: return BTN_SELECT;
    default: *ok = 0; return BTN_A;
    }
}

/* A picture of the menu from the icons in the binary ("rewards/kraid_boss_token.png", "flags/ES.png"), cut to its drawn part, as a preview (u8 w, u8 h, RGBA). */
static uint8_t *icon_preview(const char *want, size_t *len)
{
    for (const PcIcon *p = PC_ITEM_ICONS; p->name; p++) {
        if (strcmp(p->name, want)) continue;
        int w, h, n, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
        unsigned char *im = stbi_load_from_memory(p->data, (int)p->len, &w, &h, &n, 4);
        if (!im) return NULL;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (im[(y * w + x) * 4 + 3]) { if (x < x0) x0 = x; if (y < y0) y0 = y; if (x > x1) x1 = x; if (y > y1) y1 = y; }
        if (x1 < 0 || x1 - x0 >= 256 || y1 - y0 >= 256) { stbi_image_free(im); return NULL; }
        int ow = x1 - x0 + 1, oh = y1 - y0 + 1;
        uint8_t *out = malloc(2 + (size_t)ow * oh * 4);
        out[0] = (uint8_t)ow; out[1] = (uint8_t)oh;
        for (int y = 0; y < oh; y++) memcpy(out + 2 + (size_t)y * ow * 4, im + ((size_t)(y0 + y) * w + x0) * 4, (size_t)ow * 4);
        stbi_image_free(im);
        *len = 2 + (size_t)ow * oh * 4;
        return out;
    }
    return NULL;
}

static uint8_t *load_preview(const char *key, size_t *len)
{
    char name[64];
    if (!strncmp(key, "reward/", 7)) { snprintf(name, sizeof name, "rewards/%s_boss_token.png", key + 7); return icon_preview(name, len); }
    if (!strncmp(key, "flag/", 5)) { snprintf(name, sizeof name, "flags/%s.png", key + 5); return icon_preview(name, len); }
    if (!strncmp(key, "goal/", 5)) {   /* "goal/boss/kraid": goals/boss_kraid.png, or the sprite cut from the ROM */
        char k[40];
        snprintf(k, sizeof k, "%s", key + 5);
        for (char *c = k; *c; c++) if (*c == '/') *c = '_';
        snprintf(name, sizeof name, "goals/%s.png", k);
        uint8_t *pv = icon_preview(name, len);
        return pv ? pv : store_read(store_path("goals/%s.pv", k), len);
    }
    snprintf(name, sizeof name, "%s", key);
    return store_read(store_path("sprites/%s.pv", name), len);
}

static int g_menu_quit;
static void toggle_fullscreen(void) { if (win2) return; SDL_SetWindowFullscreen(win, SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); settings_video_save_state(); }
static void quit_menu(void) { g_menu_quit = 1; }
static int is_fullscreen(void) { return (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0; }
static void set_window_size(int w, int h)
{
    if (win2) return;   /* dual screen: both windows fill their displays */
    SDL_Rect u;
    if (SDL_GetDisplayUsableBounds(0, &u) != 0) return;
    if (!w) { w = u.w; h = u.h; }
    if (w > u.w || h > u.h) { float f = fminf((float)u.w / w, (float)u.h / h); w = (int)(w * f); h = (int)(h * f); }
    SDL_SetWindowSize(win, w, h);
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}
static const char *setup_and_recheck(const char *path) { const char *m = setup_path(path); g_base_checked = 0; return m; }

static int welcome_ready(void) { return base_ok(); }
static void welcome_existing(char names[2][128], char md5[2][33])
{
    snprintf(names[0], 128, "saved copy"); snprintf(md5[0], 33, "%s", SMZ_MD5_SM);
    snprintf(names[1], 128, "saved copy"); snprintf(md5[1], 33, "%s", SMZ_MD5_Z3);
}
static const char *welcome_setup(const char *path)
{
    const char *m = setup_and_recheck(path);
    if (g_sprites_dirty) { g_sprites_dirty = 0; refill_sprites(); }
    welcome_added();
    return m;
}
/* one place for a menu button, whichever screen has the focus */
/* 1 when the daily slot holds the attempt of this window */
/* "install", "player", "country" and "version" of a create or join request. */
static void race_who(char *out, size_t cap)
{
    char id[48], name[64];
    online_install_id(id);
    smz_race_quote(ui.player, name, sizeof name);
    snprintf(out, cap, "\"install\":\"%s\",\"player\":%s,\"country\":\"%s\",\"version\":\"%s\"", id, name, ui.country, SMZ3_VERSION);
}

/* Builds the race's game in its slot from the room's settings with the Link and Samus picked; 0 when it is ready. */
static int race_build(char *err, size_t cap)
{
    uint8_t *sm, *z3;
    if (store_load_base(&sm, &z3, err, cap)) return -1;
    static SmzUi gu;
    static GenJob j;
    char perr[64];
    int rc = -1;
    gu = ui;
    ui_preset(&gu, 1);
    ui_apply_run_settings(&gu, ui.race.room.settings);
    gu.value[UIO_LINK] = ui.value[UIO_LINK]; gu.value[UIO_SAMUS] = ui.value[UIO_SAMUS];
    gu.value[UIO_PLANDO] = 0;
    smz_host_set_plando(NULL, 0, perr, sizeof perr);
    snprintf(gu.start_label, sizeof gu.start_label, "Race %s", ui.race.room.code);
    memset(&j, 0, sizeof j);
    j.sm = sm; j.z3 = z3; j.u = &gu; j.slot = RACE_SLOT - 1;
    j.seed_if_random = 1;
    generate(&j);
    size_t sl = strlen(j.run.settings);
    if (j.rc) snprintf(err, cap, "%s", j.err);
    else if (sl + 20 >= sizeof j.run.settings || j.run.settings[0] != '{') snprintf(err, cap, "the race's settings do not fit");
    else {
        if (!strstr(j.run.settings, "\"challenge\":true")) {   /* no states, fast forward or rewind, as in the daily */
            memmove(j.run.settings + 18, j.run.settings + 1, sl);
            memcpy(j.run.settings + 1, "\"challenge\":true,", 17);
        }
        static const char *const OLD[] = {"srm", "proof", "discovery", "state"};
        for (int i = 0; i < 4; i++) remove(store_path("runs/slot%d.%s", RACE_SLOT, OLD[i]));
        rom_file(&j.run, j.run.rom_path, sizeof j.run.rom_path);
        j.run.created = j.run.last_played = (int64_t)time(NULL) * 1000;
        if (store_write(j.run.rom_path, j.rom, SMZ_HOST_ROM_SIZE) || store_run_save(RACE_SLOT, &j.run)) snprintf(err, cap, "cannot write the run files");
        else rc = 0;
    }
    gen_free(&j);
    free(sm); free(z3);
    return rc;
}

/* Hands the race server's answers to the menu. */
static void race_answers(void)
{
    static char ans[32768];
    int k;
    while ((k = race_take(ans, sizeof ans)) >= 0) {
        if (k == RACE_ANS_ROOMS) ui_race_rooms(&ui, ans);
        else if (k == RACE_ANS_ERROR) ui_race_fail(&ui, ans);
        else ui_race_update(&ui, ans, online_now_ms());
    }
    race_set_since(ui.race.room.since);
}

static int daily_kept(const char *window)
{
    SmzDailyRun r;
    return window[0] && online_daily_load(&r) == 0 && !strcmp(r.window, window) && access(store_path("runs/slot%d.run", DAILY_SLOT), F_OK) == 0;
}

/* Start or Continue on the Daily screen: makes the run of the open window in the daily slot unless it is there already.
 * 0 when the slot is ready to play, else -1 with the reason in err. */
static int daily_begin(char *err, size_t cap)
{
    if (daily_kept(ui.daily.window)) return 0;
    uint8_t *sm, *z3;
    if (store_load_base(&sm, &z3, err, cap)) return -1;
    ui_busy(&ui, "Starting the challenge...");
    fit_width(ren);
    ui_draw(&ui);
    {
        float ox, oy; int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        gfx_ui(&ui, ww, wh, &ox, &oy);
        SDL_RenderPresent(ren);
    }
    static SmzDailyRun r;
    static char settings[8192];
    int rc = -1;
    if (online_daily_start(ui.daily.window, &r, settings, sizeof settings, err, cap) == 0) {
        static SmzUi gu;
        static GenJob j;
        char perr[64];
        gu = ui;
        ui_preset(&gu, 1);
        ui_apply_run_settings(&gu, settings);
        gu.value[UIO_LINK] = ui.value[UIO_LINK]; gu.value[UIO_SAMUS] = ui.value[UIO_SAMUS];   /* the player's sprites stay */
        gu.value[UIO_PLANDO] = 0;
        smz_host_set_plando(NULL, 0, perr, sizeof perr);
        snprintf(gu.start_label, sizeof gu.start_label, "Daily %s", r.window);
        memset(&j, 0, sizeof j);
        j.sm = sm; j.z3 = z3; j.u = &gu; j.slot = DAILY_SLOT - 1;
        j.seed_if_random = 1;
        generate(&j);
        if (j.rc) snprintf(err, cap, "%s", j.err);
        else if (smz_daily_mark_settings(j.run.settings, sizeof j.run.settings, r.check, settings)) snprintf(err, cap, "the challenge's settings do not fit");
        else {
            static const char *const OLD[] = {"srm", "proof", "discovery", "state"};
            for (int i = 0; i < 4; i++) remove(store_path("runs/slot%d.%s", DAILY_SLOT, OLD[i]));
            remove(store_path("runs/daily.finish"));
            remove(store_path("runs/daily.result"));
            rom_file(&j.run, j.run.rom_path, sizeof j.run.rom_path);
            j.run.created = j.run.last_played = (int64_t)time(NULL) * 1000;
            if (store_write(j.run.rom_path, j.rom, SMZ_HOST_ROM_SIZE) || store_run_save(DAILY_SLOT, &j.run)) snprintf(err, cap, "cannot write the run files");
            else { online_daily_save(&r); rc = 0; }
        }
        gen_free(&j);
    }
    free(sm); free(z3);
    ui_idle(&ui);
    return rc;
}

static void menu_cue(UiCue c)
{
    static const SfxKind S[] = {[UI_CUE_MOVE] = SFX_MOVE, [UI_CUE_TAB] = SFX_TAB, [UI_CUE_SELECT] = SFX_SELECT, [UI_CUE_OPEN] = SFX_OPEN, [UI_CUE_BACK] = SFX_BACK, [UI_CUE_CONFIRM] = SFX_CONFIRM};
    if (c != UI_CUE_NONE) sfx_play(S[c]);
}

static SmzUi ui_before;

static void menu_button(UiButton b)
{
    if (fx_busy()) return;
    if (settings_active() || welcome_active()) {   /* their own menus: the sound follows the button */
        if (settings_active()) settings_nav(b); else welcome_nav(b);
        menu_cue(b == BTN_UP || b == BTN_DOWN ? UI_CUE_MOVE : b == BTN_B ? UI_CUE_BACK : b == BTN_L || b == BTN_R ? UI_CUE_TAB : UI_CUE_SELECT);
        return;
    }
    ui_before = ui;
    ui_input(&ui, b);
    menu_cue(ui_cue(&ui_before, &ui, b));
}

static void window_main(int start_slot)
{
    char err[200];
    SDL_Window *w = win; (void)w;
    ui_init(&ui);
    snprintf(ui.version, sizeof ui.version, "%s", SMZ3_VERSION);   /* one screen: the run list carries the release */
    pad_cfg_load();
    sc_cfg_load();
    settings_init(&(SettingsHooks){welcome_setup, note, toggle_fullscreen, quit_menu, is_fullscreen, set_window_size});
    settings_video_load();
    welcome_init(&(WelcomeHooks){welcome_setup, welcome_ready, settings_open_files, welcome_existing});
    gfx_set_preview_loader(load_preview);
    ui.flags = 1;
    ui.rings = 1;
    ui.orbs = 1;
    install_bundled_sprites();
    refill_sprites();
    refill_slots();
    online_init(&ui);
    ui_ladder_enable(&ui, 1);
    ui_daily_enable(&ui, 1);
    ui_home_enable(&ui, UI_OPT_BUTTONS | UI_OPT_VIDEO | UI_OPT_QUIT);
    ui_race_enable(&ui, 1);
    if (start_slot < 0) ui_home_open(&ui);   /* a run named on the command line skips the start menu */
    if (getenv("SMZ3_LADDER") || getenv("SMZ3_KEYS")) ui.screen = UI_SLOTS_SCREEN;   /* the test hooks below drive the run list */
    if (ren2) { for (int t = 0; t < 3; t++) g_ladder_state[t] = 1; detach(ladder_thread, NULL); }   /* the second screen shows them from the start */
    if (getenv("SMZ3_LADDER")) ui_input(&ui, BTN_RIGHT);   /* test hook: open the online tables */
    if (getenv("SMZ3_SPRITES")) { ui.screen = UI_SPRITES; ui.sprite_game = atoi(getenv("SMZ3_SPRITES")); ui.sprite_cursor = 0; }   /* test hook: open a sprite picker */
    if (getenv("SMZ3_SETTINGS")) {   /* test hook: open the settings screen (main, pad or files) */
        const char *t = getenv("SMZ3_SETTINGS");
        settings_open();
        if (!strcmp(t, "pad")) settings_nav(BTN_A);
        else if (!strcmp(t, "video")) { settings_nav(BTN_DOWN); settings_nav(BTN_A); settings_nav(BTN_DOWN); settings_nav(BTN_DOWN); settings_nav(BTN_RIGHT); settings_nav(BTN_DOWN); settings_nav(BTN_RIGHT); }
        else if (!strcmp(t, "files")) settings_open_files();
    }
    if (getenv("SMZ3_KEYS")) {   /* test hook: press menu buttons, e.g. "a,d,a" (u d l r a b L R s: Select) */
        for (const char *k = getenv("SMZ3_KEYS"); *k; k++) {
            const char *names = "udlrabLRs";
            const char *p = strchr(names, *k);
            if (p && *k) ui_input(&ui, (UiButton)(p - names));
        }
    }
    if (getenv("SMZ3_MODE_MENU")) ui.screen = UI_MODE;   /* test hook: open the run mode list */
    if (!base_ok() || getenv("SMZ3_WELCOME")) welcome_open();
    if (getenv("SMZ3_PLAY_BUTTON")) { welcome_nav(BTN_RIGHT); welcome_nav(BTN_A); }   /* test hook: press Play! */
    if (getenv("SMZ3_DROP")) { note(accept_rom(getenv("SMZ3_DROP"))); g_base_checked = 0; welcome_added(); }   /* test hook: drop a file on the welcome screen */
    detach(monthly_thread, NULL);
    detach(flush_thread, NULL);
    {
        size_t n;
        char *saved = (char *)store_read(store_path("saved.txt"), &n);
        if (saved) { saved[n] = 0; ui_saved_load(&ui, saved); free(saved); }
    }
    int running = 1, pending_play = start_slot, pending_state_only = 0, instant = start_slot >= 0;   /* a run named on the command line starts at once */
    Uint32 next_daily = 0, launch_at = 0;   /* launch_at: when the menu began going to black for the game */
    Uint32 last_tick = SDL_GetTicks();
    while (running && !g_menu_quit) {
        {
            Uint32 now = SDL_GetTicks();
            if (!welcome_active()) ui_tick(&ui, (int)(now - last_tick));   /* the logo's time runs once the ROM files are in */
            last_tick = now;
        }
        race_answers();
        if (pending_play >= 0) {
            if (!instant && !launch_at) launch_at = SDL_GetTicks();
            if (instant || SDL_GetTicks() - launch_at >= LAUNCH_MS) {
                play(pending_play, pending_state_only);
                if (pending_play == RACE_SLOT - 1) { race_forget(); ui_race_end(&ui); }   /* a race is played once */
                pending_play = -1;
                launch_at = 0;
                instant = 0;
                refill_slots();
                next_daily = 0;
                if (g_quit) break;
            }
        }
        if (SDL_GetTicks() >= next_daily) { next_daily = SDL_GetTicks() + 60000; detach(daily_thread, NULL); }
        if (g_daily_ready) {
            g_daily_ready = 0;
            ui_daily_set(&ui, &g_daily, daily_kept(g_daily.window));
            static int pressed;   /* test hook: press Start (or Continue) on the Daily screen once the server answered */
            if (getenv("SMZ3_DAILY_GO") && !pressed && g_daily.window[0]) { pressed = 1; ui.action = ACT_DAILY; }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int pcode = pad_event_code(&e);
            if (e.type == SDL_KEYDOWN) help_note_keyboard();
            else if (pcode >= 0 || e.type == SDL_CONTROLLERBUTTONDOWN) help_note_pad(pad);
            if (launch_at && e.type != SDL_QUIT) continue;
            if (settings_raw(&e, pcode)) continue;
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                int ok;
                UiButton b = map_key(e.key.keysym.sym, &ok);
                if (e.key.keysym.sym == SDLK_F11) toggle_fullscreen();
                else if (e.key.keysym.sym == SDLK_F9) settings_cycle_aspect();
                else if (e.key.keysym.sym == SDLK_F8) settings_cycle_filter();
                else if (e.key.keysym.sym == SDLK_F1 && !settings_active() && !welcome_active() && ui.screen == UI_SLOTS_SCREEN) settings_open();
                else if (ok) menu_button(b);
            } else if (pcode >= 0) {
                /* directions come from the polling below (so holding repeats); here the buttons */
                UiButton b = BTN_A;
                int ok = 1;
                switch (pcode) {
                case SDL_CONTROLLER_BUTTON_A: b = BTN_A; break;
                case SDL_CONTROLLER_BUTTON_B: b = BTN_B; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: b = BTN_L; break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: b = BTN_R; break;
                case SDL_CONTROLLER_BUTTON_BACK:
                    if (base_ok() || settings_active() || welcome_active()) { b = BTN_SELECT; break; }
                    /* fall through: without the ROMs Select opens the settings too */
                case SDL_CONTROLLER_BUTTON_START:
                    if (!settings_active() && !welcome_active() && ui.screen == UI_NAME) { b = BTN_START; break; }
                    ok = 0;
                    if (!settings_active() && !welcome_active() && ui.screen == UI_SLOTS_SCREEN) settings_open();
                    break;
                default: ok = 0;
                }
                if (ok) menu_button(b);
            } else if (win2 && e.type == SDL_MOUSEBUTTONDOWN && e.button.windowID == SDL_GetWindowID(win2)) {
                int bw, bh, sw, sh;
                SDL_GetRendererOutputSize(ren2, &bw, &bh);
                SDL_GetWindowSize(win2, &sw, &sh);
                idle_tap(e.button.x * (float)bw / sw, e.button.y * (float)bh / sh);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !settings_active()) {
                int ww, wh;
                SDL_GetRendererOutputSize(ren, &ww, &wh);
                float k = (float)ww / UI_W < (float)wh / UI_H ? (float)ww / UI_W : (float)wh / UI_H;
                int lx = (int)((e.button.x - (ww - UI_W * k) / 2) / k), ly = (int)((e.button.y - (wh - UI_H * k) / 2) / k);
                if (fx_busy()) {} else if (welcome_active()) welcome_click(lx, ly); else { ui_before = ui; ui_touch(&ui, lx, ly); menu_cue(ui_cue(&ui_before, &ui, -1)); }
            } else if (e.type == SDL_DROPFILE) {
                note(accept_rom(e.drop.file));
                g_base_checked = 0;
                welcome_added();
                SDL_free(e.drop.file);
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        {   /* the D-pad and the left stick: one step when pressed, then repeating while held */
            static int last_dirs, repeats;
            static Uint32 next_step;
            int dirs = pad_dirs(pad), fire = 0;
            Uint32 now = SDL_GetTicks();
            if (dirs != last_dirs) { fire = dirs; next_step = now + 380; repeats = 0; }
            else if (dirs && now >= next_step) { fire = dirs; next_step = now + (repeats++ < 4 ? 120 : 70); }
            last_dirs = dirs;
            static const struct { int bit; UiButton b; } DIR[4] = {{1, BTN_UP}, {2, BTN_DOWN}, {4, BTN_LEFT}, {8, BTN_RIGHT}};
            for (int i = 0; i < 4; i++)
                if (!launch_at && (fire & DIR[i].bit)) menu_button(DIR[i].b);
        }

        if (g_month_ready) { g_month_ready = 0; ui_set_monthly(&ui, g_month, g_month_seed, online_days_left()); }
        for (int t = 0; t < 3; t++)
            if (g_ladder_state[t] >= 2) { ui_ladder_load_json(&ui, t, g_ladder_state[t] == 2 ? g_ladder[t] : NULL); g_ladder_state[t] = 0; }
        if (g_goals_state >= 2) { ui_world_load_json(&ui, g_goals_state == 2 ? g_goals : NULL); g_goals_state = 0; }
        {
            static Uint32 next_world;   /* the World tab refreshes once a minute while it is shown */
            if (ui_world_showing(&ui) && !g_goals_state && SDL_GetTicks() >= next_world) { next_world = SDL_GetTicks() + 60000; g_goals_state = 1; detach(goals_thread, NULL); }
        }
        if (g_week_ready) { g_week_ready = 0; ui_set_weekly(&ui, g_week, g_week_seed, online_weekly_days_left()); }
        online_save_profile(&ui);
        if (ui_saved_dirty(&ui)) {
            char text[8192];
            if (ui_saved_text(&ui, text, sizeof text) > 0) store_write(store_path("saved.txt"), text, strlen(text));
        }
        if (ui.action != ACT_NONE) {
            UiAction act = ui.action;
            int slot = ui.action_slot;
            ui.action = ACT_NONE;
            if (act == ACT_LADDER) { for (int t = 0; t < 3; t++) g_ladder_state[t] = 1; detach(ladder_thread, NULL); if (!g_goals_state) { g_goals_state = 1; detach(goals_thread, NULL); } }
            else if (act == ACT_RACE_CREATE || act == ACT_RACE_JOIN) {
                char who[256], f[384];
                race_who(who, sizeof who);
                if (act == ACT_RACE_CREATE) snprintf(f, sizeof f, "%s,\"format\":\"%s\",\"size\":%d,\"public\":%s", who, ui_race_format_key(&ui), ui.race.size, ui.race.is_public ? "true" : "false");
                else snprintf(f, sizeof f, "%s,\"code\":\"%s\"", who, ui.race.code);
                race_forget();
                race_send(act == ACT_RACE_CREATE ? "create" : "join", f, 0);
            }
            else if (act == ACT_RACE_FIND) race_find();
            else if (act == ACT_RACE_CLOSE) race_send("close", "", 1);
            else if (act == ACT_RACE_KICK && slot >= 0 && slot < ui.race.room.n) { char f[48]; snprintf(f, sizeof f, "\"pid\":\"%s\"", ui.race.room.p[slot].pid); race_send("kick", f, 1); }
            else if (act == ACT_RACE_LEAVE) race_send("leave", "", 1);
            else if (act == ACT_RACE_READY) {   /* the game is built with the skins picked, then the server hears both */
                {
                    float ox, oy; int ww, wh;
                    fit_width(ren);
                    ui_draw(&ui);
                    SDL_GetRendererOutputSize(ren, &ww, &wh);
                    gfx_ui(&ui, ww, wh, &ox, &oy);
                    SDL_RenderPresent(ren);
                }
                if (race_build(err, sizeof err)) ui_race_fail(&ui, err);
                else {
                    char f[160], l[72], sa[72];
                    smz_race_quote(ui.sprites[0][ui.value[UIO_LINK]], l, sizeof l);
                    smz_race_quote(ui.sprites[1][ui.value[UIO_SAMUS]], sa, sizeof sa);
                    snprintf(f, sizeof f, "\"link\":%s,\"samus\":%s", l, sa);
                    race_send("pick", f, 1);
                    race_send("ready", "", 1);
                }
            }
            else if (act == ACT_RACE_START) { pending_play = RACE_SLOT - 1; pending_state_only = 0; instant = 1; }   /* the start is the server's: no fade to wait */
            else if (act == ACT_OPTIONS) {   /* Options opens the settings screen at the page asked for */
                if (slot == UI_OPT_QUIT) quit_menu();
                else { settings_open(); if (slot == UI_OPT_VIDEO) settings_nav(BTN_DOWN); settings_nav(BTN_A); }
            }
            else if (act == ACT_DAILY) {
                if (daily_begin(err, sizeof err) == 0) { pending_play = DAILY_SLOT - 1; pending_state_only = 0; }
                else { note(err); next_daily = 0; }
            }
            else if (act == ACT_DELETE) { store_run_delete(slot + 1); refill_slots(); }
            else if (act == ACT_CONTINUE) {
                SmzRun r;
                if (store_run_load(slot + 1, &r) == 0 && json_flag(r.settings, "saveStates", 0) && r.save_ms > 0 && (r.game == 3 || r.play_ms - r.save_ms > 10000)
                    && access(store_path("runs/slot%d.state", slot + 1), F_OK) == 0)
                    ui_load_choice(&ui, slot, r.save_ms, r.save_zelda, r.save_metroid, r.play_ms, r.zelda_cleared, r.metroid_cleared, r.zelda_total, r.metroid_total);
                else { pending_play = slot; pending_state_only = 0; }
            } else if (act == ACT_LOAD_SAVE) { pending_play = slot; pending_state_only = 1; }
            else if (act == ACT_LOAD_STATE) { pending_play = slot; pending_state_only = 0; }
            else if (act == ACT_GENERATE) {
                uint8_t *sm, *z3;
                if (store_load_base(&sm, &z3, err, sizeof err)) { ui_idle(&ui); note(err); welcome_open(); }
                else {
                    ui_busy(&ui, "Generating...");
                    fit_width(ren);
                    ui_draw(&ui);
                    float ox, oy; int ww, wh;
                    SDL_GetRendererOutputSize(ren, &ww, &wh);
                    gfx_ui(&ui, ww, wh, &ox, &oy);
                    SDL_RenderPresent(ren);
                    static GenJob j;
                    memset(&j, 0, sizeof j);
                    j.sm = sm; j.z3 = z3; j.slot = slot;
                    j.seed_if_random = (int32_t)(SDL_GetTicks() * 2654435761u >> 1) % 2147483646 + 1;
                    generate(&j);
                    ui_idle(&ui);
                    if (j.rc) note(j.err);
                    else {
                        int on = ui_online_now(&ui), kind = ui_run_weekly(&ui) ? 3 : ui_run_monthly(&ui) ? 2 : 1;
                        rom_file(&j.run, j.run.rom_path, sizeof j.run.rom_path);
                        j.run.created = j.run.last_played = (int64_t)time(NULL) * 1000;
                        if (store_write(j.run.rom_path, j.rom, SMZ_HOST_ROM_SIZE) == 0 && store_run_save(slot + 1, &j.run) == 0) {
                            online_mark_run(slot + 1, on, kind);
                            refill_slots();
                            pending_play = slot; pending_state_only = 0;
                        } else note("cannot write the run files");
                    }
                    gen_free(&j); free(sm); free(z3);
                }
            }
        }

        fit_width(ren);

        ui_draw(&ui);
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        float ox, oy;
        static SmzUi prev_ui;          /* the last frame's screen, kept to fade out when the next one comes */
        static SDL_Texture *gone;
        static Uint32 gone_at;
        static int prev_screen = -1;
        if ((int)ui.screen != prev_screen) {
            if (prev_screen >= 0 && !settings_active() && !welcome_active()) {
                if (gone) SDL_DestroyTexture(gone);
                gone = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ww, wh);
                if (gone) {
                    SDL_SetRenderTarget(ren, gone);
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
                    SDL_RenderClear(ren);
                    gfx_ui(&prev_ui, ww, wh, &ox, &oy);
                    SDL_SetRenderTarget(ren, NULL);
                    SDL_SetTextureBlendMode(gone, SDL_BLENDMODE_BLEND);
                    gone_at = SDL_GetTicks();
                }
            }
            prev_screen = (int)ui.screen;
        }
        float k = gfx_ui(&ui, ww, wh, &ox, &oy);
        if (ui.screen == UI_SLOTS_SCREEN && !settings_active() && !welcome_active()) {
            if (!base_ok()) gfx_text(ww / 2.0f, wh - 28 * k / 2 - 8, 11 * k, C(0xE9, 0xA2, 0x3B), 1, "Select: find your ROM files (or drop them here)");
            else {
                static const HelpItem H[] = {{"A", "Enter", "Open"}, {"SELECT", "Tab", "Ladder"}, {"START", "F1", "Settings"}, {"B", "Esc", "Back"}};
                help_bar(ox, oy + 224 * k, UI_W * k, 16 * k, H, 4);
            }
        }
        welcome_draw(k, ox, oy);
        settings_draw(k, ox, oy);
        {
            static int last_screen = -1, last_flags;
            int flags = welcome_active() | settings_active() << 1;
            if ((int)ui.screen != last_screen || flags != last_flags) { if (last_screen >= 0) fx_screen_changed(); if (last_screen >= 0 && (flags & 2) != (last_flags & 2)) sfx_play(flags & 2 ? SFX_OPEN : SFX_CLOSE); last_screen = (int)ui.screen; last_flags = flags; }
            fx_draw(ww, wh);
            Uint32 since_gone = SDL_GetTicks() - gone_at;
            if (gone && since_gone < 300) {   /* the screen that left fades out to black, well before the seam opens */
                float a = 1 - since_gone / 300.0f;
                SDL_SetTextureAlphaMod(gone, (Uint8)(255 * a * a * (3 - 2 * a)));
                SDL_RenderCopy(ren, gone, NULL, NULL);
            }
        }
        memcpy(&prev_ui, &ui, sizeof ui);
        if (SDL_GetTicks() < g_note_until && !welcome_active()) {
            gfx_rect(0, 0, (float)ww, 34 * k / 2 + 8, (SDL_Color){0, 0, 0, 200});
            gfx_text(ww / 2.0f, 6, 13 * k, C(0xE8, 0xEC, 0xF2), 1, g_note);
        }
        SDL_Color dark = {0, 0, 0, (Uint8)(launch_at ? fminf(1, (SDL_GetTicks() - launch_at) / (float)LAUNCH_FADE_MS) * 255 : 0)};
        if (dark.a) gfx_rect(0, 0, (float)ww, (float)wh, dark);
        if (ren2) {   /* dual screen, run list: the selected run and the ladders on the tracker's screen */
            gfx_use(ren2);
            int bw, bh;
            SDL_GetRendererOutputSize(ren2, &bw, &bh);
            SDL_SetRenderDrawColor(ren2, 0x08, 0x0A, 0x10, 255);
            SDL_RenderClear(ren2);
            idle_draw(&ui, bw, bh, SMZ3_VERSION);
            if (dark.a) gfx_rect(0, 0, (float)bw, (float)bh, dark);
            present2();
            gfx_use(ren);
        }
        present();
        SDL_Delay(8);
    }
}

#ifdef __linux__
/* A dual-screen handheld by its model name (device tree on ARM boards, DMI elsewhere). */
static int model_is_dual(void)
{
    static const char *const FILES[] = {"/proc/device-tree/model", "/sys/firmware/devicetree/base/model", "/sys/class/dmi/id/product_name"};
    static const char *const NAMES[] = {"RG DS", "RGDS", "RG-DS", "RG_DS"};
    for (size_t f = 0; f < sizeof FILES / sizeof *FILES; f++) {
        char buf[128] = {0};
        FILE *fp = fopen(FILES[f], "rb");
        if (!fp) continue;
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        fclose(fp);
        for (size_t i = 0; i < n; i++) if (!buf[i]) buf[i] = ' ';
        for (size_t k = 0; k < sizeof NAMES / sizeof *NAMES; k++) if (icase_has(buf, NAMES[k])) return 1;
    }
    return 0;
}

/* Dual-screen mode: the game on the top display and the tracker on the bottom one. Auto (mode 0) takes it on a known
 * dual-screen handheld or when both displays are handheld sized, so a desktop with two monitors keeps one window. */
static int dual_displays(int mode, int *top, int *bottom)
{
    if (getenv("SMZ3_SCREENS")) mode = atoi(getenv("SMZ3_SCREENS"));
    if (mode == 1 || SDL_GetNumVideoDisplays() < 2) return 0;
    SDL_Rect a, b;
    if (SDL_GetDisplayBounds(0, &a) || SDL_GetDisplayBounds(1, &b)) return 0;
    int small = (long)a.w * a.h <= 1280L * 800 && (long)b.w * b.h <= 1280L * 800;
    if (mode == 0 && !small && !model_is_dual()) return 0;
    *top = b.y < a.y ? 1 : 0;
    *bottom = 1 - *top;
    return 1;
}
#else
static int dual_displays(int mode, int *top, int *bottom) { (void)mode; (void)top; (void)bottom; return 0; }
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    if (argc == 1) FreeConsole();   /* started by double click: no console window */
#endif
    if (argc > 1) {
        if (!strcmp(argv[1], "gen")) return cmd_gen(argc - 2, argv + 2);
        if (!strcmp(argv[1], "runs")) return cmd_runs();
        if (!strcmp(argv[1], "online")) return cmd_online();
        if (!strcmp(argv[1], "new")) return cmd_new(argc - 2, argv + 2);
        if (!strcmp(argv[1], "selftest")) return cmd_selftest(argc - 2, argv + 2);
        if (!strcmp(argv[1], "flush")) return cmd_flush();
        if (!strcmp(argv[1], "setup")) return cmd_setup(argc - 2, argv + 2);
        if (!strcmp(argv[1], "config")) return cmd_config(argc - 2, argv + 2);
        if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "help")) { usage(); return 0; }
        if (strcmp(argv[1], "play") || argc < 3) { usage(); return 2; }
    }
    int start = argc > 2 ? atoi(argv[2]) - 1 : -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    pad_handheld_setup();
    int win_w0 = 1920, win_h0 = 1080;   /* 1080p, or the largest 16:9 window the screen can hold */
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && (usable.w < win_w0 || usable.h < win_h0)) {
        float f = fminf(usable.w * 0.95f / win_w0, usable.h * 0.9f / win_h0);
        win_w0 = (int)(win_w0 * f); win_h0 = (int)(win_h0 * f);
    }
    int top = 0, bottom = 1, dual = dual_displays(settings_screens_saved(), &top, &bottom);
    SDL_Rect tb;
    if (dual && SDL_GetDisplayBounds(top, &tb) == 0)
        win = SDL_CreateWindow("SMZ3", SDL_WINDOWPOS_CENTERED_DISPLAY(top), SDL_WINDOWPOS_CENTERED_DISPLAY(top), tb.w, tb.h, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    else
        win = SDL_CreateWindow("SMZ3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w0, win_h0, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren && win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    gfx_init(ren);
    SDL_Rect bb;
    if (dual && SDL_GetDisplayBounds(bottom, &bb) == 0) {
        win2 = SDL_CreateWindow("SMZ3 tracker", SDL_WINDOWPOS_CENTERED_DISPLAY(bottom), SDL_WINDOWPOS_CENTERED_DISPLAY(bottom), bb.w, bb.h, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
        ren2 = win2 ? SDL_CreateRenderer(win2, -1, SDL_RENDERER_ACCELERATED) : NULL;
        if (!ren2 && win2) ren2 = SDL_CreateRenderer(win2, -1, SDL_RENDERER_SOFTWARE);
        if (!ren2 && win2) { SDL_DestroyWindow(win2); win2 = NULL; }   /* no second renderer here: one window, as on a PC */
        if (win2) SDL_RaiseWindow(win);
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    window_main(start);
    SDL_Quit();
    return 0;
}
