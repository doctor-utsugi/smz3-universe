#include <jni.h>
#include <stdio.h>
#include "smz3_state.h"
#include "smz3_goal.h"
#include <string.h>
#include "smz3_tracker.h"
#include "smz3_daily.h"
#include "smz3_shortcuts.h"
#include "smz3_rewind.h"
#include "smz3_version.h"
#include "smz3_rom.h"
#include "z3_rom.h"

#define NATIVE(name) Java_com_swordfish_lemuroid_app_shared_game_viewmodel_Smz3Native_##name

static SmzGoal g_goal;   /* the daily challenge or Draft goal of the run being played (smz3_goal.h) */
static SmzCounters g_counters;   /* the run's counters: world goals, and a Draft's kills and rupees */
#include "smz3_race.h"
static SmzRaceItems g_race_items;   /* the major items already told to the rivals in this game's race */
static int g_live_progress;         /* the Draft goal's progress at the last summarize (smz_goal_progress_live) */

/* Takes the goal from the run's settings JSON; a run without one clears it. */
static int g_dead;   /* the run's Super Metroid bosses dead from the start (a mid-game daily start) */
JNIEXPORT void JNICALL NATIVE(setGoal)(JNIEnv *env, jclass cls, jstring settings)
{
    const char *s = settings ? (*env)->GetStringUTFChars(env, settings, NULL) : NULL;
    smz_goal_from_settings(s, &g_goal);
    g_dead = smz_daily_prepared_bosses(s);
    if (s) (*env)->ReleaseStringUTFChars(env, settings, s);
}

/* A mid-game daily start (setGoal's run): the bytes to write so its dead bosses stay dead, as {region (0 work RAM,
 * 1 save RAM), offset, value, ...}; empty when nothing changes. */
JNIEXPORT jintArray JNICALL NATIVE(preparedPokes)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    int out[48], n = 0;
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    if (g_dead) {
        jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
        jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
        uint8_t before[2][8];
        if (wl >= 0xD830) memcpy(before[0], w + 0xD828, 8);
        if (sl >= 0x2080) memcpy(before[1], s + 0x2078, 8);
        if (smz_prepared_apply(g_dead, (uint8_t *)w, (size_t)wl, (uint8_t *)s, (size_t)sl))
            for (int i = 0; i < 8; i++) {
                if (wl >= 0xD830 && (uint8_t)w[0xD828 + i] != before[0][i]) { out[n++] = 0; out[n++] = 0xD828 + i; out[n++] = (uint8_t)w[0xD828 + i]; }
                if (sl >= 0x2080 && (uint8_t)s[0x2078 + i] != before[1][i]) { out[n++] = 1; out[n++] = 0x2078 + i; out[n++] = (uint8_t)s[0x2078 + i]; }
            }
        (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);   /* the copies only: the caller writes the emulator's memory */
        (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
    }
    jintArray r = (*env)->NewIntArray(env, n);
    (*env)->SetIntArrayRegion(env, r, 0, n, out);
    return r;
}

/* Adds the shortcut hooks to a run's ROM made before them; null when it already has them or is neither an SMZ3 nor an
 * alttpr ROM. */
JNIEXPORT jbyteArray JNICALL NATIVE(addShortcutHooks)(JNIEnv *env, jclass cls, jbyteArray rom)
{
    jsize n = (*env)->GetArrayLength(env, rom);
    if (n < 0x410000 && n != 0x200000) return NULL;
    jbyte *b = (*env)->GetByteArrayElements(env, rom, NULL);
    int ok;
    if (n == 0x200000) {
        ok = z3_rom_shortcut_hooks((uint8_t *)b) == 0;
        if (ok) z3_rom_update_checksum((uint8_t *)b);
    } else {
        ok = smz_rom_shortcut_hooks((uint8_t *)b) == 0;
        if (ok) smz_rom_checksum((uint8_t *)b);
    }
    (*env)->ReleaseByteArrayElements(env, rom, b, ok ? 0 : JNI_ABORT);
    return ok ? rom : NULL;
}

/* The item shortcuts (core/ui/smz3_shortcuts.h): "name|icon" of each id, ALttP items first, then the beams and the weapons. */
JNIEXPORT jobjectArray JNICALL NATIVE(shortcutList)(JNIEnv *env, jclass cls)
{
    jobjectArray r = (*env)->NewObjectArray(env, SC_COUNT, (*env)->FindClass(env, "java/lang/String"), NULL);
    char buf[96];
    for (int i = 0; i < SC_COUNT; i++) {
        snprintf(buf, sizeof buf, "%s|%s", smz_shortcuts[i].name, smz_shortcuts[i].icon);
        (*env)->SetObjectArrayElement(env, r, i, (*env)->NewStringUTF(env, buf));
    }
    return r;
}

/* {ready, owned bits, active bits}: bit i is shortcut id i. */
JNIEXPORT jlongArray JNICALL NATIVE(shortcutState)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL), *s = (*env)->GetByteArrayElements(env, sram, NULL);
    jlong out[3] = {smz_shortcut_ready((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl), 0, 0};
    for (int i = 0; i < SC_COUNT; i++) {
        if (smz_shortcut_owned(i, (const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl)) out[1] |= (jlong)1 << i;
        if (smz_shortcut_active(i, (const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl)) out[2] |= (jlong)1 << i;
    }
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
    jlongArray r = (*env)->NewLongArray(env, 3);
    (*env)->SetLongArrayRegion(env, r, 0, 3, out);
    return r;
}

/* The fixed keys not taken yet, {room, x, y, big} each: from the Zelda work RAM while it runs, else the parked save RAM. */
JNIEXPORT jintArray JNICALL NATIVE(fixedKeysLeft)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram, jboolean zelda)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL), *s = (*env)->GetByteArrayElements(env, sram, NULL);
    const uint8_t *flags = zelda ? (const uint8_t *)w + 0xF000 : (const uint8_t *)s;
    size_t fl = zelda ? (wl > 0xF000 ? (size_t)wl - 0xF000 : 0) : (size_t)sl;
    jint out[4 * 64];
    int n = 0;
    for (int i = 0; i < smz_fixed_key_count && n < 64; i++) {
        if (smz_fixed_key_taken(i, flags, fl, zelda ? (const uint8_t *)w : NULL, zelda ? (size_t)wl : 0)) continue;
        out[4 * n] = smz_fixed_keys[i].room; out[4 * n + 1] = smz_fixed_keys[i].x; out[4 * n + 2] = smz_fixed_keys[i].y; out[4 * n + 3] = smz_fixed_keys[i].big;
        n++;
    }
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
    jintArray r = (*env)->NewIntArray(env, 4 * n);
    (*env)->SetIntArrayRegion(env, r, 0, 4 * n, out);
    return r;
}

/* {offset, value} pairs to write in the work RAM for shortcut id, empty when it cannot act now. */
JNIEXPORT jintArray JNICALL NATIVE(shortcutPokes)(JNIEnv *env, jclass cls, jint id, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL), *s = (*env)->GetByteArrayElements(env, sram, NULL);
    SmzPoke p[4];
    int n = smz_shortcut_pokes(id, (const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, p, 4);
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
    jint out[8];
    for (int i = 0; i < n; i++) { out[2 * i] = (jint)p[i].offset; out[2 * i + 1] = p[i].value; }
    jintArray r = (*env)->NewIntArray(env, 2 * n);
    (*env)->SetIntArrayRegion(env, r, 0, 2 * n, out);
    return r;
}

/* Returns {game (3 once the run is over, or its challenge goal met), started, zeldaCleared, zeldaTotal, metroidCleared, metroidTotal}. */
JNIEXPORT jintArray JNICALL NATIVE(summarize)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    smz3_summary sum;
    smz3_summarize((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, &sum);
    if (g_goal.kind != GOAL_NONE && (smz_goal_done(&g_goal, (const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl) || smz_goal_counted(&g_goal, &g_counters))) sum.finished = 1;
    if (g_goal.kind != GOAL_NONE) g_live_progress = smz_goal_progress_live(&g_goal, &g_counters, (const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, g_race_items.got, 0);
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);

    jint out[6] = {(jint)(sum.finished ? SMZ3_GAME_CREDITS : sum.game), sum.started, (jint)sum.zelda_cleared, (jint)sum.zelda_total,
                   (jint)sum.metroid_cleared, (jint)sum.metroid_total};
    jintArray result = (*env)->NewIntArray(env, 6);
    (*env)->SetIntArrayRegion(env, result, 0, 6, out);
    return result;
}

/* Returns the indices of every cleared location, empty before a file has started. */
JNIEXPORT jintArray JNICALL NATIVE(clearedLocations)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    smz3_game game = smz3_current_game((const uint8_t *)s, (size_t)sl);
    int started = smz3_started((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl);

    jint cleared[1024];
    jsize count = 0;
    if (started && game != SMZ3_GAME_NONE) {
        for (unsigned i = 0; i < smz3_location_count && count < 1024; i++) {
            if (smz3_location_cleared(&smz3_locations[i], game, (const uint8_t *)w, (size_t)wl,
                                      (const uint8_t *)s, (size_t)sl) > 0)
                cleared[count++] = (jint)i;
        }
    }
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);

    jintArray result = (*env)->NewIntArray(env, count);
    (*env)->SetIntArrayRegion(env, result, 0, count, cleared);
    return result;
}

JNIEXPORT jstring JNICALL NATIVE(locationName)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_location_count) return (*env)->NewStringUTF(env, "");
    char buf[256];
    snprintf(buf, sizeof buf, "%s: %s", smz3_locations[index].region, smz3_locations[index].name);
    return (*env)->NewStringUTF(env, buf);
}

/* Inventory levels in smz3_inventory_items order; -1 means unknown right now. */
JNIEXPORT jintArray JNICALL NATIVE(inventory)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    jint levels[256];
    int tmp[256];
    unsigned n = smz3_inventory_count < 256 ? smz3_inventory_count : 256;
    smz3_inventory((const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, tmp);
    for (unsigned i = 0; i < n; i++) levels[i] = tmp[i];
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
    jintArray result = (*env)->NewIntArray(env, (jsize)n);
    (*env)->SetIntArrayRegion(env, result, 0, (jsize)n, levels);
    return result;
}

/* "game|name|icon" for an inventory item, game being 1 Zelda or 2 Metroid. */
JNIEXPORT jstring JNICALL NATIVE(inventoryInfo)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_inventory_count) return (*env)->NewStringUTF(env, "");
    const smz3_inv_item *it = &smz3_inventory_items[index];
    char buf[256];
    snprintf(buf, sizeof buf, "%d|%s|%s", (int)it->game, it->name, it->icon);
    return (*env)->NewStringUTF(env, buf);
}

/* "game|region|name|id" for a location. */
JNIEXPORT jstring JNICALL NATIVE(locationInfo)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_location_count) return (*env)->NewStringUTF(env, "");
    const smz3_location *l = &smz3_locations[index];
    char buf[256];
    snprintf(buf, sizeof buf, "%d|%s|%s|%s", (int)l->game, l->region, l->name, l->id);
    return (*env)->NewStringUTF(env, buf);
}

/* Room number of an ALttP chest location (its flag lives in the room table) with its flag bit number in bits 16 and up, or -1. */
JNIEXPORT jint JNICALL NATIVE(zeldaLocationRoom)(JNIEnv *env, jclass cls, jint index)
{
    if (index < 0 || (unsigned)index >= smz3_location_count) return -1;
    const smz3_location *l = &smz3_locations[index];
    return l->game == SMZ3_GAME_ZELDA && !l->zelda_misc ? (jint)(l->addr | (l->flag << 16)) : -1;
}

JNIEXPORT jint JNICALL NATIVE(zeldaRoomLocation)(JNIEnv *env, jclass cls, jint room)
{
    return (jint)smz3_zelda_room_location((unsigned)room);
}

JNIEXPORT jint JNICALL NATIVE(locationCount)(JNIEnv *env, jclass cls)
{
    return (jint)smz3_location_count;
}

JNIEXPORT jint JNICALL NATIVE(inventoryCount)(JNIEnv *env, jclass cls)
{
    return (jint)smz3_inventory_count;
}

/* ---- offline generator (core/gen) */
#include <stdlib.h>
#include <string.h>
#include "smz3_generate.h"
#include "smz3_text.h"
#include "smz3_itemnames.h"
#include "smz3_plando.h"

static char g_error[128];

typedef struct { const char *name; const uint8_t *data; size_t len; } IpsEntry;
typedef struct { IpsEntry *e; int n; } IpsSet;

static const uint8_t *ips_lookup(void *user, const char *name, size_t *len)
{
    IpsSet *set = user;
    for (int i = 0; i < set->n; i++)
        if (!strcmp(set->e[i].name, name)) { *len = set->e[i].len; return set->e[i].data; }
    return NULL;
}

/* params: seed, keysanity, placement rule, ganon crystals, GT crystals, Tourian bosses, open pyramid, open Tourian door, light dark rooms, hint tiles, Etecoons jingle, nerfed charge, quick morph */
static void fill_settings(JNIEnv *env, jintArray params, jobjectArray logicNames, jintArray logicValues, SmzSettings *s, SmzPatchCfg *cfg)
{
    jint p[13] = {0};
    jsize np = (*env)->GetArrayLength(env, params);
    (*env)->GetIntArrayRegion(env, params, 0, np < 13 ? np : 13, p);
    smz_settings_defaults(s);
    s->seed = p[0]; s->keysanity = p[1]; s->placement_rule = p[2];
    s->ganon_crystals = p[3]; s->gt_crystals = p[4]; s->tourian_bosses = p[5];
    jsize n = (*env)->GetArrayLength(env, logicNames);
    jint *vals = (*env)->GetIntArrayElements(env, logicValues, NULL);
    for (jsize i = 0; i < n; i++) {
        jstring js = (*env)->GetObjectArrayElement(env, logicNames, i);
        const char *name = (*env)->GetStringUTFChars(env, js, NULL);
        smz_setting_set(s, name, vals[i]);
        (*env)->ReleaseStringUTFChars(env, js, name);
        (*env)->DeleteLocalRef(env, js);
    }
    (*env)->ReleaseIntArrayElements(env, logicValues, vals, JNI_ABORT);
    if (cfg) {
        smz_patch_cfg_defaults(cfg, s);
        cfg->pyramid_open = p[6];
        cfg->tourian_door_open = p[7];
        cfg->light_dark_rooms = p[8];
        cfg->hint_tiles = p[9];
        cfg->etecoons_jingle = p[10];
        cfg->nerfed_charge = p[11];
        cfg->quick_morph = p[12];
        cfg->sm_default_controls = 1;
    }
}

JNIEXPORT jbyteArray JNICALL NATIVE(generate)(JNIEnv *env, jclass cls, jbyteArray sm, jbyteArray z3, jobjectArray ipsNames,
                                              jobjectArray ipsData, jintArray params, jobjectArray logicNames, jintArray logicValues,
                                              jbyteArray linkRdc, jbyteArray samusRdc, jbyteArray plandoYaml)
{
    g_error[0] = 0;
    jsize sml = (*env)->GetArrayLength(env, sm), z3l = (*env)->GetArrayLength(env, z3);
    if (sml != 0x300000 || z3l != 0x100000) { snprintf(g_error, sizeof g_error, "base ROMs have the wrong size"); return NULL; }
    SmzSettings *s = calloc(1, sizeof *s);
    SmzPatchCfg *cfg = calloc(1, sizeof *cfg);
    fill_settings(env, params, logicNames, logicValues, s, cfg);

    jsize nips = (*env)->GetArrayLength(env, ipsNames);
    IpsSet set = { calloc(nips ? nips : 1, sizeof(IpsEntry)), nips };
    jstring *names = calloc(nips ? nips : 1, sizeof(jstring));
    jbyteArray *datas = calloc(nips ? nips : 1, sizeof(jbyteArray));
    for (jsize i = 0; i < nips; i++) {
        names[i] = (*env)->GetObjectArrayElement(env, ipsNames, i);
        datas[i] = (*env)->GetObjectArrayElement(env, ipsData, i);
        set.e[i].name = (*env)->GetStringUTFChars(env, names[i], NULL);
        set.e[i].data = (const uint8_t *)(*env)->GetByteArrayElements(env, datas[i], NULL);
        set.e[i].len = (size_t)(*env)->GetArrayLength(env, datas[i]);
    }
    jbyte *lnk = linkRdc ? (*env)->GetByteArrayElements(env, linkRdc, NULL) : NULL;
    jbyte *sam = samusRdc ? (*env)->GetByteArrayElements(env, samusRdc, NULL) : NULL;
    if (lnk) { cfg->link_rdc = (const uint8_t *)lnk; cfg->link_rdc_len = (size_t)(*env)->GetArrayLength(env, linkRdc); }
    if (sam) { cfg->samus_rdc = (const uint8_t *)sam; cfg->samus_rdc_len = (size_t)(*env)->GetArrayLength(env, samusRdc); }
    jbyte *smb = (*env)->GetByteArrayElements(env, sm, NULL);
    jbyte *z3b = (*env)->GetByteArrayElements(env, z3, NULL);

    SmzSeed *seed = calloc(1, sizeof *seed);
    uint8_t *rom = malloc(SMZ_ROM_SIZE);
    int rc;
    if (plandoYaml) {
        SmzPlando *pl = calloc(1, sizeof *pl);
        jbyte *yaml = (*env)->GetByteArrayElements(env, plandoYaml, NULL);
        char err[256];
        if (smz_plando_parse(pl, (const char *)yaml, (size_t)(*env)->GetArrayLength(env, plandoYaml), err, sizeof err)) {
            snprintf(g_error, sizeof g_error, "plando: %s", err);
            rc = -9;
        } else {
            rc = smz_generate_plando(rom, seed, (const uint8_t *)smb, (const uint8_t *)z3b, pl, cfg, ips_lookup, &set, NULL, NULL);
        }
        (*env)->ReleaseByteArrayElements(env, plandoYaml, yaml, JNI_ABORT);
        free(pl);   /* the patcher only read it during the call */
    } else {
        rc = smz_generate(rom, seed, (const uint8_t *)smb, (const uint8_t *)z3b, s, cfg, ips_lookup, &set, NULL, NULL);
    }
    jbyteArray result = NULL;
    if (rc == -9) {}   /* g_error holds the plando message */
    else if (rc) snprintf(g_error, sizeof g_error, rc == -6 ? "the sprite file is not valid" : rc == -8 ? "the plando config is incomplete" : "generation failed (%d)", rc);
    else {
        result = (*env)->NewByteArray(env, SMZ_ROM_SIZE);
        (*env)->SetByteArrayRegion(env, result, 0, SMZ_ROM_SIZE, (const jbyte *)rom);
    }

    if (lnk) (*env)->ReleaseByteArrayElements(env, linkRdc, lnk, JNI_ABORT);
    if (sam) (*env)->ReleaseByteArrayElements(env, samusRdc, sam, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sm, smb, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, z3, z3b, JNI_ABORT);
    for (jsize i = 0; i < nips; i++) {
        (*env)->ReleaseStringUTFChars(env, names[i], set.e[i].name);
        (*env)->ReleaseByteArrayElements(env, datas[i], (jbyte *)set.e[i].data, JNI_ABORT);
        (*env)->DeleteLocalRef(env, names[i]);
        (*env)->DeleteLocalRef(env, datas[i]);
    }
    free(names); free(datas); free(set.e); free(rom); free(seed); free(s); free(cfg);
    return result;
}

JNIEXPORT jstring JNICALL NATIVE(lastError)(JNIEnv *env, jclass cls)
{
    return (*env)->NewStringUTF(env, g_error);
}

#include "smz3_sm.h"

/* Super Metroid alone: the run's settings JSON with the seed hash added (smz_sm_generate), read back by smLastSettings. */
static char g_sm_json[4096];
static JNIEnv *g_gen_env;   /* the thread building a seed, for Map Rando's download through Smz3Native */

JNIEXPORT jbyteArray JNICALL NATIVE(generateSm)(JNIEnv *env, jclass cls, jbyteArray sm, jstring settings, jbyteArray samusRdc)
{
    g_error[0] = 0;
    if ((*env)->GetArrayLength(env, sm) != SMZ_SM_BASE_SIZE) { snprintf(g_error, sizeof g_error, "the Super Metroid ROM has the wrong size"); return NULL; }
    const char *j = (*env)->GetStringUTFChars(env, settings, NULL);
    snprintf(g_sm_json, sizeof g_sm_json, "%s", j);
    (*env)->ReleaseStringUTFChars(env, settings, j);
    jbyte *smb = (*env)->GetByteArrayElements(env, sm, NULL);
    jbyte *sam = samusRdc ? (*env)->GetByteArrayElements(env, samusRdc, NULL) : NULL;
    uint8_t *rom = malloc(SMZ_SM_ROM_SIZE);
    g_gen_env = env;
    int rc = rom ? smz_sm_generate(g_sm_json, sizeof g_sm_json, (const uint8_t *)smb, (const uint8_t *)sam,
                                   sam ? (size_t)(*env)->GetArrayLength(env, samusRdc) : 0, rom, SMZ_SM_ROM_SIZE) : -1;
    g_gen_env = NULL;
    jbyteArray result = NULL;
    if (rc && smz_sm_error(rc)) snprintf(g_error, sizeof g_error, "%s", smz_sm_error(rc));
    else if (rc) snprintf(g_error, sizeof g_error, "generation failed (%d)", rc);
    else {
        result = (*env)->NewByteArray(env, SMZ_SM_ROM_SIZE);
        (*env)->SetByteArrayRegion(env, result, 0, SMZ_SM_ROM_SIZE, (const jbyte *)rom);
    }
    if (sam) (*env)->ReleaseByteArrayElements(env, samusRdc, sam, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sm, smb, JNI_ABORT);
    free(rom);
    return result;
}

JNIEXPORT jstring JNICALL NATIVE(smLastSettings)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, g_sm_json); }

/* Map Rando (core/maprando, built with cargo-ndk): its packed data, extracted from the APK's assets by MapRando.kt. */
#ifdef SMZ_MAPRANDO
#include "smz3_maprando.h"
#include "smz3_bps.h"

static char g_mr_dir[512];
static volatile int g_mr_phase;   /* 0 idle, 1 loading the data, 2 building the seed, 3 downloading it */

/* Building a seed peaks near 780 MB (measured): a phone with less RAM than this in all, or free, downloads it instead. */
#define MR_TOTAL_MB 2048
#define MR_FREE_MB 1100

static long meminfo_mb(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128], fmt[40];
    long kb = -1;
    snprintf(fmt, sizeof fmt, "%s: %%ld kB", key);
    while (fgets(line, sizeof line, f)) if (sscanf(line, fmt, &kb) == 1) break;
    fclose(f);
    return kb < 0 ? -1 : kb / 1024;
}

/* The server's patch for these settings (MapRando.download in Kotlin), applied to the player's ROM. */
static int mr_download(const char *settings, uint64_t seed, const uint8_t *sm, size_t sm_len, uint8_t *out, size_t cap,
                       char *info, size_t info_cap)
{
    JNIEnv *env = g_gen_env;
    snprintf(info, info_cap, "{\"error\":\"This phone has too little memory to build Map Rando seeds\"}");
    if (!env) return -1;
    jclass c = (*env)->FindClass(env, "com/swordfish/lemuroid/app/shared/game/viewmodel/Smz3Native");
    jmethodID dl = c ? (*env)->GetStaticMethodID(env, c, "mapRandoDownload", "(Ljava/lang/String;Ljava/lang/String;)[B") : NULL;
    jmethodID inf = c ? (*env)->GetStaticMethodID(env, c, "mapRandoInfo", "()Ljava/lang/String;") : NULL;
    if (!dl || !inf) { (*env)->ExceptionClear(env); return -1; }
    char sd[24];
    snprintf(sd, sizeof sd, "%llu", (unsigned long long)seed);
    jstring js = (*env)->NewStringUTF(env, settings), jseed = (*env)->NewStringUTF(env, sd);
    g_mr_phase = 3;
    jbyteArray patch = (jbyteArray)(*env)->CallStaticObjectMethod(env, c, dl, js, jseed);
    g_mr_phase = 0;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); patch = NULL; }
    jstring ji = (jstring)(*env)->CallStaticObjectMethod(env, c, inf);
    if (ji) {
        const char *t = (*env)->GetStringUTFChars(env, ji, NULL);
        snprintf(info, info_cap, "%s", t);
        (*env)->ReleaseStringUTFChars(env, ji, t);
    }
    if (!patch) return -1;
    jsize n = (*env)->GetArrayLength(env, patch);
    jbyte *b = (*env)->GetByteArrayElements(env, patch, NULL);
    if (sm_len % 0x8000 == 0x200) { sm += 0x200; sm_len -= 0x200; }   /* a copier header: the patch is made from the bare ROM */
    int r = !b ? -1 : smz_bps_target_size((const uint8_t *)b, (size_t)n) > cap ? -1 : smz_bps_apply((const uint8_t *)b, (size_t)n, sm, sm_len, out, cap);
    if (b) (*env)->ReleaseByteArrayElements(env, patch, b, JNI_ABORT);
    if (r) snprintf(info, info_cap, "{\"error\":\"%s\"}", r == -2 ? "The patch does not fit this Super Metroid ROM" : "The seed came damaged, try again");
    return r ? -1 : 0;
}

static int mr_generate(const char *settings, uint64_t seed, const uint8_t *sm, size_t sm_len, uint8_t *out, size_t cap,
                       char *info, size_t info_cap)
{
    char err[300];
    long total_mb = meminfo_mb("MemTotal"), free_mb = meminfo_mb("MemAvailable");
    if ((total_mb >= 0 && total_mb < MR_TOTAL_MB) || (free_mb >= 0 && free_mb < MR_FREE_MB))
        return mr_download(settings, seed, sm, sm_len, out, cap, info, info_cap);
    g_mr_phase = 1;
    if (smz_mr_init(g_mr_dir, err, sizeof err)) {
        for (char *c = err; *c; c++) if (*c == '"' || *c == '\\' || *c < ' ') *c = '\'';
        snprintf(info, info_cap, "{\"error\":\"%s\"}", err);
        g_mr_phase = 0;
        return -1;
    }
    g_mr_phase = 2;
    int rc = smz_mr_generate(settings, seed, 0, sm, sm_len, out, cap, info, info_cap);
    smz_mr_free();   /* about 780 MB the game does not need */
    g_mr_phase = 0;
    return rc;
}
#endif

JNIEXPORT jboolean JNICALL NATIVE(mapRandoSetup)(JNIEnv *env, jclass cls, jstring dir)
{
#ifdef SMZ_MAPRANDO
    const char *d = (*env)->GetStringUTFChars(env, dir, NULL);
    snprintf(g_mr_dir, sizeof g_mr_dir, "%s", d);
    (*env)->ReleaseStringUTFChars(env, dir, d);
    smz_sm_set_maprando(mr_generate, SMZ_MR_READY);
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT void JNICALL NATIVE(mapRandoPool)(JNIEnv *env, jclass cls, jstring dir)
{
    const char *d = (*env)->GetStringUTFChars(env, dir, NULL);
    smz_sm_set_maprando_pool(d);
    (*env)->ReleaseStringUTFChars(env, dir, d);
}

/* -1 idle, 0 loading Map Rando's data, else 1 + the attempts of the seed being built. */
JNIEXPORT jint JNICALL NATIVE(mapRandoProgress)(JNIEnv *env, jclass cls)
{
#ifdef SMZ_MAPRANDO
    return g_mr_phase == 3 ? -2 : g_mr_phase == 1 ? 0 : g_mr_phase == 2 ? 1 + (jint)smz_mr_attempts() : -1;
#else
    return -1;
#endif
}

/* Which randomizer's ROM the reads follow (smz3_set_mode): set when a run starts, in each process that reads memory. */
JNIEXPORT void JNICALL NATIVE(setMode)(JNIEnv *env, jclass cls, jint mode) { smz3_set_mode(mode); }
JNIEXPORT jint JNICALL NATIVE(markerDy)(JNIEnv *env, jclass cls, jint index) { return smz3_marker_dy(index); }

JNIEXPORT jint JNICALL NATIVE(itemByName)(JNIEnv *env, jclass cls, jstring name)
{
    const char *n = (*env)->GetStringUTFChars(env, name, NULL);
    int id = smz_item_by_name(n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return id;
}

static SmzSeed g_seed;
static SmzSettings g_seed_settings;
static int g_seed_valid;
static SmzPlando g_seed_plando;
static jsize g_seed_plando_len = -1;

/* (Re)builds the cached seed of a run: filled from its settings, or taken from its plando YAML. */
static void ensure_seed(JNIEnv *env, const SmzSettings *s, jbyteArray plandoYaml)
{
    jsize plen = plandoYaml ? (*env)->GetArrayLength(env, plandoYaml) : 0;
    SmzPlando pl;
    int have_pl = 0;
    if (plandoYaml) {
        jbyte *yaml = (*env)->GetByteArrayElements(env, plandoYaml, NULL);
        char err[256];
        have_pl = smz_plando_parse(&pl, (const char *)yaml, (size_t)plen, err, sizeof err) == 0;
        (*env)->ReleaseByteArrayElements(env, plandoYaml, yaml, JNI_ABORT);
        if (!have_pl) { g_seed_valid = 0; return; }
    }
    if (g_seed_valid && !memcmp(s, &g_seed_settings, sizeof *s) && plen == g_seed_plando_len && (!have_pl || !memcmp(&pl, &g_seed_plando, sizeof pl))) return;
    if (have_pl) {
        char err[256];
        g_seed_valid = smz_plando_seed(&g_seed, &pl, err, sizeof err) == 0;
        g_seed_plando = pl;
    } else {
        SmzSettings eff;
        g_seed_valid = smz_fill_valid(&g_seed, s, &eff) == 0;
    }
    g_seed_settings = *s;
    g_seed_plando_len = plen;
}

/* Indices (into the tracker's location table) of the locations in logic for this run's seed and these item types. */
JNIEXPORT jintArray JNICALL NATIVE(reachable)(JNIEnv *env, jclass cls, jintArray params, jobjectArray logicNames, jintArray logicValues,
                                              jintArray items, jbyteArray plandoYaml)
{
    SmzSettings s;
    fill_settings(env, params, logicNames, logicValues, &s, NULL);
    ensure_seed(env, &s, plandoYaml);
    if (!g_seed_valid) return (*env)->NewIntArray(env, 0);
    jsize n = (*env)->GetArrayLength(env, items);
    jint *it = (*env)->GetIntArrayElements(env, items, NULL);
    int ids[512];
    int all[1100], na = smz_assumed_items(&g_seed, all, 600);
    for (jsize i = 0; i < n && na < 1100; i++) all[na++] = it[i];
    int k = smz_reachable(&g_seed, all, na, ids, 512);
    (*env)->ReleaseIntArrayElements(env, items, it, JNI_ABORT);
    jint out[512];
    int count = 0;
    for (int i = 0; i < k; i++)
        for (unsigned j = 0; j < smz3_location_count; j++) {
            const char *id = smz3_locations[j].id;
            for (int q = 0; q < SMZ_LOC_NAME_COUNT; q++)
                if (SMZ_LOC_NAMES[q].id == ids[i]) {
                    if (!strcmp(SMZ_LOC_NAMES[q].name, id)) out[count++] = (jint)j;
                    break;
                }
        }
    jintArray result = (*env)->NewIntArray(env, count);
    (*env)->SetIntArrayRegion(env, result, 0, count, out);
    return result;
}

/* The hints a player may ask for in the limited help mode (the same ones the hint tiles show), for this run's seed. */
JNIEXPORT jobjectArray JNICALL NATIVE(helpTexts)(JNIEnv *env, jclass cls, jintArray params, jobjectArray logicNames, jintArray logicValues, jbyteArray plandoYaml)
{
    SmzSettings s;
    SmzPatchCfg cfg;
    fill_settings(env, params, logicNames, logicValues, &s, &cfg);
    ensure_seed(env, &s, plandoYaml);
    jclass str = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result = (*env)->NewObjectArray(env, SMZ_HELP_COUNT, str, NULL);
    if (!g_seed_valid) return result;
    static char texts[SMZ_HELP_COUNT][SMZ_HELP_TEXT];
    smz_help_texts(&g_seed, &cfg, texts);
    for (int i = 0; i < SMZ_HELP_COUNT; i++) {
        jstring js = (*env)->NewStringUTF(env, texts[i]);
        (*env)->SetObjectArrayElement(env, result, i, js);
        (*env)->DeleteLocalRef(env, js);
    }
    return result;
}

/* ---- shared menu (core/ui): the Kotlin side only forwards input and draws the commands */
#include "smz3_ui.h"
#include "smz3_run.h"
#include "smz3_ui_gen.h"

static SmzUi g_ui;

JNIEXPORT void JNICALL NATIVE(uiInit)(JNIEnv *env, jclass cls) { ui_init(&g_ui); g_ui.flags = 1; g_ui.rings = 1; g_ui.orbs = 1; }

JNIEXPORT void JNICALL NATIVE(uiSetSlot)(JNIEnv *env, jclass cls, jint i, jboolean used, jstring name, jstring summary)
{
    const char *n = (*env)->GetStringUTFChars(env, name, NULL), *s = (*env)->GetStringUTFChars(env, summary, NULL);
    ui_set_slot(&g_ui, i, used, n, s);
    (*env)->ReleaseStringUTFChars(env, name, n);
    (*env)->ReleaseStringUTFChars(env, summary, s);
}

JNIEXPORT void JNICALL NATIVE(uiSetSlotStats)(JNIEnv *env, jclass cls, jint i, jint zc, jint zt, jint mc, jint mt, jlong ms, jint game, jint bosses, jint crystals)
{
    ui_set_slot_stats(&g_ui, i, zc, zt, mc, mt, ms, game, bosses, crystals);
}

JNIEXPORT void JNICALL NATIVE(uiSetSlotRules)(JNIEnv *env, jclass cls, jint i, jstring settings)
{
    const char *s = (*env)->GetStringUTFChars(env, settings, NULL);
    ui_set_slot_rules(&g_ui, i, s);
    (*env)->ReleaseStringUTFChars(env, settings, s);
}

JNIEXPORT void JNICALL NATIVE(uiSetSprites)(JNIEnv *env, jclass cls, jint game, jobjectArray names)
{
    jsize n = (*env)->GetArrayLength(env, names);
    const char **list = calloc(n ? n : 1, sizeof *list);
    jstring *js = calloc(n ? n : 1, sizeof *js);
    for (jsize i = 0; i < n; i++) {
        js[i] = (*env)->GetObjectArrayElement(env, names, i);
        list[i] = (*env)->GetStringUTFChars(env, js[i], NULL);
    }
    ui_set_sprites(&g_ui, game, list, n);
    for (jsize i = 0; i < n; i++) { (*env)->ReleaseStringUTFChars(env, js[i], list[i]); (*env)->DeleteLocalRef(env, js[i]); }
    free(list); free(js);
}

static SmzUi g_ui_before;   /* the menu before an input, to tell which sound it makes */
JNIEXPORT jint JNICALL NATIVE(uiInput)(JNIEnv *env, jclass cls, jint button) { g_ui_before = g_ui; ui_input(&g_ui, (UiButton)button); return ui_cue(&g_ui_before, &g_ui, button); }
JNIEXPORT jint JNICALL NATIVE(uiTouch)(JNIEnv *env, jclass cls, jint x, jint y) { g_ui_before = g_ui; ui_touch(&g_ui, x, y); return ui_cue(&g_ui_before, &g_ui, -1); }
JNIEXPORT void JNICALL NATIVE(uiIdle)(JNIEnv *env, jclass cls) { ui_idle(&g_ui); }
JNIEXPORT void JNICALL NATIVE(uiHomeEnable)(JNIEnv *env, jclass cls, jint mask) { ui_home_enable(&g_ui, mask); }
JNIEXPORT void JNICALL NATIVE(uiMenuMusic)(JNIEnv *env, jclass cls, jboolean on) { ui_menu_music(&g_ui, on); }
JNIEXPORT void JNICALL NATIVE(uiSeedCode)(JNIEnv *env, jclass cls, jboolean on) { ui_seed_code(&g_ui, on); }
JNIEXPORT void JNICALL NATIVE(uiHomeOpen)(JNIEnv *env, jclass cls) { ui_home_open(&g_ui); }
JNIEXPORT void JNICALL NATIVE(uiTick)(JNIEnv *env, jclass cls, jint ms) { ui_tick(&g_ui, ms); }

JNIEXPORT void JNICALL NATIVE(uiBusy)(JNIEnv *env, jclass cls, jstring msg)
{
    const char *m = (*env)->GetStringUTFChars(env, msg, NULL);
    ui_busy(&g_ui, m);
    (*env)->ReleaseStringUTFChars(env, msg, m);
}

/* One string per command: "kind,x,y,w,h,color,align,text". */
JNIEXPORT jobjectArray JNICALL NATIVE(uiDraw)(JNIEnv *env, jclass cls)
{
    ui_draw(&g_ui);
    jclass str = (*env)->FindClass(env, "java/lang/String");
    jobjectArray out = (*env)->NewObjectArray(env, g_ui.cmd_count, str, NULL);
    for (int i = 0; i < g_ui.cmd_count; i++) {
        const UiCmd *c = &g_ui.cmd[i];
        char line[128];
        snprintf(line, sizeof line, "%d,%d,%d,%d,%d,%d,%d,%s", c->kind, c->x, c->y, c->w, c->h, c->color, c->align, c->text);
        jstring js = (*env)->NewStringUTF(env, line);
        (*env)->SetObjectArrayElement(env, out, i, js);
        (*env)->DeleteLocalRef(env, js);
    }
    return out;
}

/* [action, slot] of a pending host action, or [0, 0]; clears it. */
JNIEXPORT jintArray JNICALL NATIVE(uiAction)(JNIEnv *env, jclass cls)
{
    jint v[2] = { g_ui.action, g_ui.action_slot };
    g_ui.action = ACT_NONE;
    jintArray out = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}

/* The plando YAML of the file chosen in the menu (null clears it); returns the error text, empty on success. */
JNIEXPORT jstring JNICALL NATIVE(uiSetPlando)(JNIEnv *env, jclass cls, jbyteArray yaml)
{
    char err[256] = "";
    if (!yaml) ui_set_plando(NULL, 0, err, sizeof err);
    else {
        jbyte *b = (*env)->GetByteArrayElements(env, yaml, NULL);
        if (ui_set_plando((const char *)b, (size_t)(*env)->GetArrayLength(env, yaml), err, sizeof err)) {}
        (*env)->ReleaseByteArrayElements(env, yaml, b, JNI_ABORT);
    }
    return (*env)->NewStringUTF(env, err);
}

JNIEXPORT jstring JNICALL NATIVE(uiPlandoName)(JNIEnv *env, jclass cls)
{
    char name[32];
    ui_plando_name(&g_ui, name);
    return (*env)->NewStringUTF(env, name);
}

JNIEXPORT jstring JNICALL NATIVE(uiSettingsJson)(JNIEnv *env, jclass cls, jint seedIfRandom)
{
    char buf[4096];
    if (ui_settings_json(&g_ui, seedIfRandom, buf, sizeof buf) < 0) buf[0] = 0;
    return (*env)->NewStringUTF(env, buf);
}

#include "smz3_host.h"
#include "smz3_z3.h"
/* A Link to the Past alone: the 2 MB ROM of the menu's run from the 1 MB JP ROM (smz_host_generate, which moves on from a
 * random seed whose fill fails), its settings with the seed hash read back by z3LastSettings. */
static char g_z3_json[4096];

JNIEXPORT jbyteArray JNICALL NATIVE(generateZ3)(JNIEnv *env, jclass cls, jbyteArray z3, jbyteArray linkRdc, jint seedIfRandom)
{
    g_error[0] = 0;
    if ((*env)->GetArrayLength(env, z3) != SMZ_Z3_BASE_SIZE) { snprintf(g_error, sizeof g_error, "the ALttP ROM has the wrong size"); return NULL; }
    jbyte *zb = (*env)->GetByteArrayElements(env, z3, NULL);
    jbyte *lk = linkRdc ? (*env)->GetByteArrayElements(env, linkRdc, NULL) : NULL;
    uint8_t *rom = malloc(SMZ_HOST_ROM_SIZE);
    char seed[16];
    int rc = rom ? smz_host_generate(&g_ui, seedIfRandom, NULL, (const uint8_t *)zb, (const uint8_t *)lk,
                                     lk ? (size_t)(*env)->GetArrayLength(env, linkRdc) : 0, NULL, 0, NULL, NULL, rom,
                                     g_z3_json, sizeof g_z3_json, seed, sizeof seed, NULL) : -1;
    jbyteArray result = NULL;
    if (rc && smz_z3_error(rc)) snprintf(g_error, sizeof g_error, "%s", smz_z3_error(rc));
    else if (rc) snprintf(g_error, sizeof g_error, "generation failed (%d)", rc);
    else {
        result = (*env)->NewByteArray(env, SMZ_Z3_ROM_SIZE);
        (*env)->SetByteArrayRegion(env, result, 0, SMZ_Z3_ROM_SIZE, (const jbyte *)rom);
    }
    if (lk) (*env)->ReleaseByteArrayElements(env, linkRdc, lk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, z3, zb, JNI_ABORT);
    free(rom);
    return result;
}

/* A Link to the Past alone from a settings JSON (a daily challenge's or a race's), the seed hash added as generateZ3 does. */
JNIEXPORT jbyteArray JNICALL NATIVE(generateZ3Json)(JNIEnv *env, jclass cls, jbyteArray z3, jstring settings, jbyteArray linkRdc)
{
    g_error[0] = 0;
    if ((*env)->GetArrayLength(env, z3) != SMZ_Z3_BASE_SIZE) { snprintf(g_error, sizeof g_error, "the ALttP ROM has the wrong size"); return NULL; }
    const char *j = (*env)->GetStringUTFChars(env, settings, NULL);
    snprintf(g_z3_json, sizeof g_z3_json, "%s", j);
    (*env)->ReleaseStringUTFChars(env, settings, j);
    jbyte *zb = (*env)->GetByteArrayElements(env, z3, NULL);
    jbyte *lk = linkRdc ? (*env)->GetByteArrayElements(env, linkRdc, NULL) : NULL;
    uint8_t *rom = malloc(SMZ_HOST_ROM_SIZE);
    int rc = rom ? smz_z3_generate(g_z3_json, sizeof g_z3_json, (const uint8_t *)zb, (const uint8_t *)lk,
                                   lk ? (size_t)(*env)->GetArrayLength(env, linkRdc) : 0, rom, SMZ_HOST_ROM_SIZE) : -1;
    jbyteArray result = NULL;
    if (rc && smz_z3_error(rc)) snprintf(g_error, sizeof g_error, "%s", smz_z3_error(rc));
    else if (rc) snprintf(g_error, sizeof g_error, "generation failed (%d)", rc);
    else {
        result = (*env)->NewByteArray(env, SMZ_Z3_ROM_SIZE);
        (*env)->SetByteArrayRegion(env, result, 0, SMZ_Z3_ROM_SIZE, (const jbyte *)rom);
    }
    if (lk) (*env)->ReleaseByteArrayElements(env, linkRdc, lk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, z3, zb, JNI_ABORT);
    free(rom);
    return result;
}

JNIEXPORT jstring JNICALL NATIVE(z3LastSettings)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, g_z3_json); }

/* Saved customs: the host stores the text in a file; null (no file yet) installs the built-in ones. */
JNIEXPORT void JNICALL NATIVE(uiSavedLoad)(JNIEnv *env, jclass cls, jstring text)
{
    const char *t = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    ui_saved_load(&g_ui, t);
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
}

/* The saved customs as the file text when they changed since the last call, else null. */
JNIEXPORT jstring JNICALL NATIVE(uiSavedChanged)(JNIEnv *env, jclass cls)
{
    static char buf[UI_MAX_SAVED * (UI_SPEC_LEN + 40)];
    if (!ui_saved_dirty(&g_ui) || ui_saved_text(&g_ui, buf, sizeof buf) < 0) return NULL;
    return (*env)->NewStringUTF(env, buf);
}

/* Player profile ("name<TAB>CC"): shows the Player row in the start screen; same file handling as the saved customs. */
JNIEXPORT void JNICALL NATIVE(uiPlayerLoad)(JNIEnv *env, jclass cls, jstring text)
{
    const char *t = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    ui_player_enable(&g_ui, 1);
    ui_player_load(&g_ui, t);
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
}

JNIEXPORT void JNICALL NATIVE(uiLoadChoice)(JNIEnv *env, jclass cls, jint slot, jlong saveMs, jint saveZ, jint saveM, jlong stateMs, jint stateZ, jint stateM, jint zt, jint mt)
{
    ui_load_choice(&g_ui, slot, saveMs, saveZ, saveM, stateMs, stateZ, stateM, zt, mt);
}

/* One game's monthly seed (mode: SMZ_MODE_*); the menu shows the entered game's. */
JNIEXPORT void JNICALL NATIVE(uiSetMonthly)(JNIEnv *env, jclass cls, jint mode, jstring label, jint seed, jint days)
{
    const char *l = (*env)->GetStringUTFChars(env, label, NULL);
    ui_set_monthly_game(&g_ui, mode, l, seed, days);
    (*env)->ReleaseStringUTFChars(env, label, l);
}

JNIEXPORT jint JNICALL NATIVE(uiRunMonthly)(JNIEnv *env, jclass cls) { return ui_run_monthly(&g_ui); }

JNIEXPORT void JNICALL NATIVE(uiSetWeekly)(JNIEnv *env, jclass cls, jint mode, jstring label, jint seed, jint days)
{
    const char *l = (*env)->GetStringUTFChars(env, label, NULL);
    ui_set_weekly_game(&g_ui, mode, l, seed, days);
    (*env)->ReleaseStringUTFChars(env, label, l);
}

JNIEXPORT void JNICALL NATIVE(uiMemoryLoad)(JNIEnv *env, jclass cls, jstring text)
{
    const char *t = (*env)->GetStringUTFChars(env, text, NULL);
    ui_memory_load(&g_ui, t);
    (*env)->ReleaseStringUTFChars(env, text, t);
}

JNIEXPORT jstring JNICALL NATIVE(uiMemoryText)(JNIEnv *env, jclass cls)
{
    char t[32];
    ui_memory_text(&g_ui, t, sizeof t);
    return (*env)->NewStringUTF(env, t);
}

JNIEXPORT void JNICALL NATIVE(uiSetStaging)(JNIEnv *env, jclass cls, jboolean on) { g_ui.staging = on; }

JNIEXPORT void JNICALL NATIVE(uiSetPlaying)(JNIEnv *env, jclass cls, jint mode, jint weekly, jint n) { ui_set_playing(&g_ui, mode, weekly, n); }

JNIEXPORT void JNICALL NATIVE(uiLadderEnable)(JNIEnv *env, jclass cls, jboolean on) { ui_ladder_enable(&g_ui, on); }

/* The server's answer for one online table (0 original, 1 monthly, 2 weekly); null marks it failed. */
JNIEXPORT void JNICALL NATIVE(uiLadderJson)(JNIEnv *env, jclass cls, jint tab, jstring json)
{
    const char *j = json ? (*env)->GetStringUTFChars(env, json, NULL) : NULL;
    ui_ladder_load_json(&g_ui, tab, j);
    if (j) (*env)->ReleaseStringUTFChars(env, json, j);
}

/* The /api/version answer: a newer release puts the Update row in Options, which comes back as ACT_UPDATE. */
JNIEXPORT void JNICALL NATIVE(uiUpdateJson)(JNIEnv *env, jclass cls, jstring json)
{
    const char *j = (*env)->GetStringUTFChars(env, json, NULL);
    ui_update_load_json(&g_ui, j, SMZ3_VERSION, 1);
    (*env)->ReleaseStringUTFChars(env, json, j);
}

/* The update was handed over or failed: back to Options, with msg as its notice ("" for none). */
JNIEXPORT void JNICALL NATIVE(uiUpdateEnd)(JNIEnv *env, jclass cls, jstring msg)
{
    const char *m = (*env)->GetStringUTFChars(env, msg, NULL);
    snprintf(g_ui.notice, sizeof g_ui.notice, "%.31s", m);
    (*env)->ReleaseStringUTFChars(env, msg, m);
    g_ui.screen = UI_OPTIONS;
}

JNIEXPORT jint JNICALL NATIVE(uiRunWeekly)(JNIEnv *env, jclass cls) { return ui_run_weekly(&g_ui); }

JNIEXPORT void JNICALL NATIVE(uiSetSlotOnline)(JNIEnv *env, jclass cls, jint i, jint kind) { ui_set_slot_online(&g_ui, i, kind); }

JNIEXPORT jint JNICALL NATIVE(uiOnline)(JNIEnv *env, jclass cls) { return ui_online_now(&g_ui); }

/* The race preset of the run being started ("" Original), kept with it for /api/start; and the table the ladder's first tab shows. */
JNIEXPORT jstring JNICALL NATIVE(uiRunPreset)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, ui_run_preset_key(&g_ui)); }
JNIEXPORT jstring JNICALL NATIVE(uiLadderPreset)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, ui_ladder_preset_key(&g_ui)); }

JNIEXPORT jstring JNICALL NATIVE(uiPlayerChanged)(JNIEnv *env, jclass cls)
{
    char buf[64];
    if (!ui_player_dirty(&g_ui) || ui_player_text(&g_ui, buf, sizeof buf) < 0) return NULL;
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL NATIVE(uiRunLabel)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, ui_run_label(&g_ui)); }

JNIEXPORT jint JNICALL NATIVE(uiOption)(JNIEnv *env, jclass cls, jint opt) { return opt >= 0 && opt < UI_OPT_COUNT ? g_ui.value[opt] : 0; }
JNIEXPORT void JNICALL NATIVE(uiSetSlotCount)(JNIEnv *env, jclass cls, jint n) { ui_set_slot_count(&g_ui, n); }
/* The slot the menu is on: the highlighted one in the list, the opened one in its menus. */
JNIEXPORT jint JNICALL NATIVE(uiSlot)(JNIEnv *env, jclass cls) { return g_ui.screen == UI_SLOTS_SCREEN ? (g_ui.daily_sel ? -1 : g_ui.slot_cursor) : g_ui.screen == UI_SLOT_MENU || g_ui.screen == UI_CONFIRM ? g_ui.active_slot : -1; }
JNIEXPORT jint JNICALL NATIVE(uiScreen)(JNIEnv *env, jclass cls) { return g_ui.screen; }
JNIEXPORT jint JNICALL NATIVE(uiGameMode)(JNIEnv *env, jclass cls) { return g_ui.game_mode; }
/* Back in the game a run was played from, without the carousel (the menu was made again). */
JNIEXPORT void JNICALL NATIVE(uiGameEnter)(JNIEnv *env, jclass cls, jint mode) { ui_game_enter(&g_ui, mode); }
JNIEXPORT void JNICALL NATIVE(uiGameState)(JNIEnv *env, jclass cls, jint mode, jint state) { ui_game_state(&g_ui, mode, state); }
JNIEXPORT void JNICALL NATIVE(uiRomsScreen)(JNIEnv *env, jclass cls, jstring hint)
{
    const char *h = (*env)->GetStringUTFChars(env, hint, NULL);
    ui_roms_screen(&g_ui, 1, h);
    (*env)->ReleaseStringUTFChars(env, hint, h);
}
JNIEXPORT void JNICALL NATIVE(uiRomsNote)(JNIEnv *env, jclass cls, jstring note)
{
    const char *n = (*env)->GetStringUTFChars(env, note, NULL);
    ui_roms_note(&g_ui, n);
    (*env)->ReleaseStringUTFChars(env, note, n);
}

JNIEXPORT jstring JNICALL NATIVE(romName)(JNIEnv *env, jclass cls, jstring json, jstring seed)
{
    const char *j = (*env)->GetStringUTFChars(env, json, NULL), *sd = (*env)->GetStringUTFChars(env, seed, NULL);
    char buf[96];
    if (smz_run_rom_name(j, sd, buf, sizeof buf) < 0) buf[0] = 0;
    (*env)->ReleaseStringUTFChars(env, json, j);
    (*env)->ReleaseStringUTFChars(env, seed, sd);
    return (*env)->NewStringUTF(env, buf);
}

/* ---- the daily challenge (core/ui/smz3_daily.h) */

JNIEXPORT void JNICALL NATIVE(uiDailyEnable)(JNIEnv *env, jclass cls, jboolean on) { ui_daily_enable(&g_ui, on); }

/* The /api/daily answer (null: offline) with the device clock at the answer, and whether the daily slot holds this window's run. */
JNIEXPORT void JNICALL NATIVE(uiDailySet)(JNIEnv *env, jclass cls, jstring json, jlong nowMs, jboolean hasRun)
{
    SmzDailyInfo d;
    memset(&d, 0, sizeof d);
    d.loaded = -1;
    const char *j = json ? (*env)->GetStringUTFChars(env, json, NULL) : NULL;
    if (j) smz_daily_parse_info(j, nowMs, &d);
    if (j) (*env)->ReleaseStringUTFChars(env, json, j);
    ui_daily_set(&g_ui, &d, hasRun);
}

/* When the kept attempt's game started (SmzDailyRun.began_at), so the menu's clock counts as the game does. */
JNIEXPORT void JNICALL NATIVE(uiDailyBegan)(JNIEnv *env, jclass cls, jlong beganAt) { ui_daily_began(&g_ui, beganAt); }

JNIEXPORT jstring JNICALL NATIVE(uiDailyWindow)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, g_ui.daily.window); }

/* The settings JSON of the challenge run in the app's format: the menu, set from the server's settings (the player's sprites
 * kept), then marked with the goal. "" when it does not fit. */
JNIEXPORT jstring JNICALL NATIVE(dailySettingsJson)(JNIEnv *env, jclass cls, jstring serverSettings, jstring check)
{
    static SmzUi u;
    char buf[4096];
    const char *s = (*env)->GetStringUTFChars(env, serverSettings, NULL), *c = (*env)->GetStringUTFChars(env, check, NULL);
    u = g_ui;
    ui_preset(&u, 1);
    ui_apply_run_settings(&u, s);
    u.value[UIO_LINK] = g_ui.value[UIO_LINK]; u.value[UIO_SAMUS] = g_ui.value[UIO_SAMUS];
    u.value[UIO_PLANDO] = 0;
    if (ui_settings_json(&u, 1, buf, sizeof buf) < 0 || smz_daily_mark_settings(buf, sizeof buf, c, s)) buf[0] = 0;
    (*env)->ReleaseStringUTFChars(env, serverSettings, s);
    (*env)->ReleaseStringUTFChars(env, check, c);
    return (*env)->NewStringUTF(env, buf);
}

/* ---- world goal counters of the run being played (see smz3_counters.h) ---- */

JNIEXPORT void JNICALL NATIVE(countersLoad)(JNIEnv *env, jclass cls, jstring text)
{
    const char *t = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    smz_counters_load(&g_counters, t);
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
}

JNIEXPORT void JNICALL NATIVE(countersTick)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram, jint game, jint items)
{
    jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
    jbyte *w = (*env)->GetByteArrayElements(env, wram, NULL);
    jbyte *s = (*env)->GetByteArrayElements(env, sram, NULL);
    smz_counters_tick(&g_counters, game, (const uint8_t *)w, (size_t)wl, (const uint8_t *)s, (size_t)sl, items);
    (*env)->ReleaseByteArrayElements(env, wram, w, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sram, s, JNI_ABORT);
}

JNIEXPORT jstring JNICALL NATIVE(countersText)(JNIEnv *env, jclass cls)
{
    char t[640];
    if (smz_counters_text(&g_counters, t, sizeof t) < 0) t[0] = 0;
    return (*env)->NewStringUTF(env, t);
}

JNIEXPORT void JNICALL NATIVE(uiSetWidth)(JNIEnv *env, jclass cls, jint w) { ui_set_width(w); }
JNIEXPORT jint JNICALL NATIVE(uiWidth)(JNIEnv *env, jclass cls) { return UI_W; }
JNIEXPORT void JNICALL NATIVE(uiSetHeight)(JNIEnv *env, jclass cls, jint h) { ui_set_height(h); }
JNIEXPORT jint JNICALL NATIVE(uiHeight)(JNIEnv *env, jclass cls) { return UI_H; }

JNIEXPORT void JNICALL NATIVE(uiWorldJson)(JNIEnv *env, jclass cls, jstring json)
{
    const char *j = json ? (*env)->GetStringUTFChars(env, json, NULL) : NULL;
    ui_world_load_json(&g_ui, j);
    if (j) (*env)->ReleaseStringUTFChars(env, json, j);
}

JNIEXPORT jboolean JNICALL NATIVE(uiWorldShowing)(JNIEnv *env, jclass cls) { return ui_world_showing(&g_ui) ? JNI_TRUE : JNI_FALSE; }

#include "smz3_goalicons.h"

static void goal_icon_file(const char *name, const uint8_t *pv, size_t n, void *ctx)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.pv", (const char *)ctx, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(pv, 1, n, f);
    fclose(f);
}

/* Cuts the world goal icons out of the base ROMs into dir (goals/<name>.pv); returns how many. */
JNIEXPORT jint JNICALL NATIVE(goalIcons)(JNIEnv *env, jclass cls, jbyteArray sm, jbyteArray z3, jstring dir)
{
    jsize sl = (*env)->GetArrayLength(env, sm), zl = (*env)->GetArrayLength(env, z3);
    jbyte *s = (*env)->GetByteArrayElements(env, sm, NULL), *z = (*env)->GetByteArrayElements(env, z3, NULL);
    const char *d = (*env)->GetStringUTFChars(env, dir, NULL);
    int n = smz_goal_icons((const uint8_t *)s, (size_t)sl, (const uint8_t *)z, (size_t)zl, goal_icon_file, (void *)d);
    (*env)->ReleaseStringUTFChars(env, dir, d);
    (*env)->ReleaseByteArrayElements(env, z3, z, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sm, s, JNI_ABORT);
    return n;
}

#include "smz3_romderived.h"

/* Writes the standing Link and Samus drawn from the base ROMs to dir/link/Original.pv and dir/samus/Original.pv; returns 1 on success. */
JNIEXPORT jint JNICALL NATIVE(originalPreviews)(JNIEnv *env, jclass cls, jbyteArray sm, jbyteArray z3, jstring dir)
{
    jsize sl = (*env)->GetArrayLength(env, sm), zl = (*env)->GetArrayLength(env, z3);
    jbyte *s = (*env)->GetByteArrayElements(env, sm, NULL), *z = (*env)->GetByteArrayElements(env, z3, NULL);
    const char *d = (*env)->GetStringUTFChars(env, dir, NULL);
    static uint8_t l[SMZ_PV_LINK_LEN], m[SMZ_PV_SAMUS_LEN];
    int ok = smz_original_previews((const uint8_t *)s, (size_t)sl, (const uint8_t *)z, (size_t)zl, l, m);
    if (ok) { goal_icon_file("link/Original", l, sizeof l, (void *)d); goal_icon_file("samus/Original", m, sizeof m, (void *)d); }
    (*env)->ReleaseStringUTFChars(env, dir, d);
    (*env)->ReleaseByteArrayElements(env, z3, z, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sm, s, JNI_ABORT);
    return ok;
}

/* The rewind screen's sounds (SMZ_RW_SFX_*) as mono 16-bit PCM. */
JNIEXPORT jshortArray JNICALL NATIVE(rewindSound)(JNIEnv *env, jclass cls, jint kind, jint rate)
{
    int cap = rate / 2;
    int16_t *pcm = malloc((size_t)cap * sizeof *pcm);
    int n = pcm ? smz_rewind_sound(kind, rate, pcm, cap) : 0;
    jshortArray out = (*env)->NewShortArray(env, n);
    if (out && n) (*env)->SetShortArrayRegion(env, out, 0, n, pcm);
    free(pcm);
    return out;
}

/* The release tag the app was built from. */
JNIEXPORT jstring JNICALL NATIVE(version)(JNIEnv *env, jclass cls) { return (*env)->NewStringUTF(env, SMZ3_VERSION); }

/* ---- online races (core/ui ui_race_*): the app sends the requests, the menu reads the answers ---- */
JNIEXPORT void JNICALL NATIVE(uiRaceEnable)(JNIEnv *env, jclass cls, jboolean on) { ui_race_enable(&g_ui, on); }

JNIEXPORT void JNICALL NATIVE(uiRaceUpdate)(JNIEnv *env, jclass cls, jstring json, jlong localNowMs)
{
    const char *j = (*env)->GetStringUTFChars(env, json, NULL);
    ui_race_update(&g_ui, j, localNowMs);
    (*env)->ReleaseStringUTFChars(env, json, j);
}

JNIEXPORT void JNICALL NATIVE(uiRaceRooms)(JNIEnv *env, jclass cls, jstring json)
{
    const char *j = (*env)->GetStringUTFChars(env, json, NULL);
    ui_race_rooms(&g_ui, j);
    (*env)->ReleaseStringUTFChars(env, json, j);
}

JNIEXPORT void JNICALL NATIVE(uiRaceFail)(JNIEnv *env, jclass cls, jstring msg)
{
    const char *m = (*env)->GetStringUTFChars(env, msg, NULL);
    ui_race_fail(&g_ui, m);
    (*env)->ReleaseStringUTFChars(env, msg, m);
}

JNIEXPORT void JNICALL NATIVE(uiRaceEnd)(JNIEnv *env, jclass cls) { ui_race_end(&g_ui); }

/* The paused race this device keeps ("" none): Championship lists it first. */
JNIEXPORT void JNICALL NATIVE(uiRaceSetPaused)(JNIEnv *env, jclass cls, jstring code, jlong resumeAt)
{
    const char *c = (*env)->GetStringUTFChars(env, code, NULL);
    ui_race_set_paused(&g_ui, c, resumeAt);
    (*env)->ReleaseStringUTFChars(env, code, c);
}

/* What the actions need: {format key, size, public (0/1), code typed, room code, last log line read, Link, Samus}. */
JNIEXPORT jobjectArray JNICALL NATIVE(uiRaceInfo)(JNIEnv *env, jclass cls)
{
    char size[8], pub[4], since[24];
    snprintf(size, sizeof size, "%d", g_ui.race.size);
    snprintf(pub, sizeof pub, "%d", g_ui.race.is_public ? 1 : 0);
    snprintf(since, sizeof since, "%lld", (long long)g_ui.race.room.since);
    const char *v[8] = {ui_race_format_key(&g_ui), size, pub, g_ui.race.code, g_ui.race.room.code, since,
                        g_ui.sprites[0][g_ui.value[UIO_LINK]], g_ui.sprites[1][g_ui.value[UIO_SAMUS]]};
    jobjectArray out = (*env)->NewObjectArray(env, 8, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < 8; i++) (*env)->SetObjectArrayElement(env, out, i, (*env)->NewStringUTF(env, v[i]));
    return out;
}

/* The pid of the room's player i (ACT_RACE_KICK), "" when there is none. */
JNIEXPORT jstring JNICALL NATIVE(uiRacePid)(JNIEnv *env, jclass cls, jint i)
{
    return (*env)->NewStringUTF(env, i >= 0 && i < g_ui.race.room.n ? g_ui.race.room.p[i].pid : "");
}

/* The race game's settings in the app's format: the room's, with the Link and Samus picked and marked as a challenge (no
 * states, fast forward or rewind), with a Draft's goal and start. "" when they do not fit. */
JNIEXPORT jstring JNICALL NATIVE(raceSettingsJson)(JNIEnv *env, jclass cls)
{
    static SmzUi u;
    char buf[4096];
    u = g_ui;
    ui_preset(&u, 1);
    ui_apply_run_settings(&u, g_ui.race.room.settings);
    u.value[UIO_LINK] = g_ui.value[UIO_LINK]; u.value[UIO_SAMUS] = g_ui.value[UIO_SAMUS];
    u.value[UIO_PLANDO] = 0;
    snprintf(u.start_label, sizeof u.start_label, "Race %s", g_ui.race.room.code);
    if (ui_settings_json(&u, 1, buf, sizeof buf) < 0 || smz_race_mark_settings(buf, sizeof buf, g_ui.race.room.settings)) buf[0] = 0;
    return (*env)->NewStringUTF(env, buf);
}

/* Writes the start of a Draft's settings into its ROM (and fixes the checksum); false when the ROM refused it. */
JNIEXPORT jboolean JNICALL NATIVE(raceApplyStart)(JNIEnv *env, jclass cls, jbyteArray rom, jstring settings)
{
    if ((*env)->GetArrayLength(env, rom) < 0x410000) return JNI_FALSE;
    const char *s = (*env)->GetStringUTFChars(env, settings, NULL);
    jbyte *b = (*env)->GetByteArrayElements(env, rom, NULL);
    int ok = smz_race_apply_start((uint8_t *)b, s) == 0;
    (*env)->ReleaseByteArrayElements(env, rom, b, ok ? 0 : JNI_ABORT);
    (*env)->ReleaseStringUTFChars(env, settings, s);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/* The goal of a run's settings for people ("Defeat 25 enemies"), "" when it has none. */
JNIEXPORT jstring JNICALL NATIVE(raceGoalText)(JNIEnv *env, jclass cls, jstring settings)
{
    SmzGoal g;
    char t[64] = "";
    const char *s = (*env)->GetStringUTFChars(env, settings, NULL);
    if (!smz_goal_from_settings(s, &g) && g.kind != GOAL_NONE) smz_goal_text(&g, t, sizeof t);
    (*env)->ReleaseStringUTFChars(env, settings, s);
    return (*env)->NewStringUTF(env, t);
}

/* How far the run's counted goal (kills, rupees) is, 0 to 99 (100 is the finish, from the server); -1 for the other kinds. */

JNIEXPORT void JNICALL NATIVE(raceItemsReset)(JNIEnv *env, jclass cls) { memset(&g_race_items, 0, sizeof g_race_items); }

/* The rival news for items since the last call (smz_race_item_news), from the tracker's inventory levels. */
JNIEXPORT jobjectArray JNICALL NATIVE(raceItemNews)(JNIEnv *env, jclass cls, jintArray inventory)
{
    int inv[RACE_ITEMS_MAX], n = (*env)->GetArrayLength(env, inventory);
    if (n > RACE_ITEMS_MAX) n = RACE_ITEMS_MAX;
    jint tmp[RACE_ITEMS_MAX];
    (*env)->GetIntArrayRegion(env, inventory, 0, n, tmp);
    for (int i = 0; i < n; i++) inv[i] = tmp[i];
    char news[8][40];
    const char *v[8];
    int k = smz_race_item_news(&g_race_items, inv, n, news, 8);
    for (int i = 0; i < k; i++) v[i] = news[i];
    jobjectArray out = (*env)->NewObjectArray(env, k, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < k; i++) (*env)->SetObjectArrayElement(env, out, i, (*env)->NewStringUTF(env, v[i]));
    return out;
}

JNIEXPORT jint JNICALL NATIVE(raceProgress)(JNIEnv *env, jclass cls) { return g_live_progress; }

JNIEXPORT jint JNICALL NATIVE(raceGoalProgress)(JNIEnv *env, jclass cls)
{
    if (g_goal.kind != GOAL_KILLS && g_goal.kind != GOAL_RUPEES) return -1;
    return smz_goal_progress(&g_goal, &g_counters, 0);
}

/* A goal text that defeats one boss ("Defeat Arrghus"): {name, goal icon ("" when none), place}; null when it names no boss. */
JNIEXPORT jobjectArray JNICALL NATIVE(goalBoss)(JNIEnv *env, jclass cls, jstring text)
{
    const char *name, *sprite, *place;
    const char *t = (*env)->GetStringUTFChars(env, text, NULL);
    int found = smz_goal_boss(t, &name, &sprite, &place);
    (*env)->ReleaseStringUTFChars(env, text, t);
    if (!found) return NULL;
    const char *parts[3] = {name, sprite, place};
    jobjectArray r = (*env)->NewObjectArray(env, 3, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < 3; i++) (*env)->SetObjectArrayElement(env, r, i, (*env)->NewStringUTF(env, parts[i]));
    return r;
}

/* The daily chat's quick lines (SMZ_DAILY_PHRASE), emotes among them. */
JNIEXPORT jobjectArray JNICALL NATIVE(dailyPhrases)(JNIEnv *env, jclass cls)
{
    jobjectArray r = (*env)->NewObjectArray(env, SMZ_DAILY_PHRASES, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < SMZ_DAILY_PHRASES; i++) (*env)->SetObjectArrayElement(env, r, i, (*env)->NewStringUTF(env, SMZ_DAILY_PHRASE[i]));
    return r;
}

/* The game's own pause map in a Map Rando run (smz3_smmap.h): the tracker thread hands in each read, the map view takes the
 * picture (the whole area, 512 x 256) and changes the area shown. */
#include <pthread.h>
#include <stdlib.h>
#include "smz3_smmap.h"
static pthread_mutex_t g_mm_lock = PTHREAD_MUTEX_INITIALIZER;
static SmzSmMap *g_mm;
static uint8_t *g_mm_rom, *g_mm_wram, *g_mm_sram;
static size_t g_mm_rom_len;
static int g_mm_stamp;

JNIEXPORT jboolean JNICALL NATIVE(gameMapRom)(JNIEnv *env, jclass cls, jbyteArray rom)
{
    pthread_mutex_lock(&g_mm_lock);
    free(g_mm_rom);
    g_mm_rom = NULL;
    jsize n = rom ? (*env)->GetArrayLength(env, rom) : 0;
    if (n > 0 && (g_mm_rom = malloc((size_t)n))) {
        (*env)->GetByteArrayRegion(env, rom, 0, n, (jbyte *)g_mm_rom);
        g_mm_rom_len = (size_t)n;
        if (!smz_smmap_rom_ok(g_mm_rom, g_mm_rom_len)) { free(g_mm_rom); g_mm_rom = NULL; }
    }
    if (g_mm_rom && !g_mm) g_mm = malloc(sizeof *g_mm);
    if (g_mm_rom && !g_mm_wram) g_mm_wram = calloc(1, 0x20000);
    if (g_mm_rom && !g_mm_sram) g_mm_sram = calloc(1, 0x8000);
    if (g_mm) smz_smmap_init(g_mm);
    int ok = g_mm_rom && g_mm && g_mm_wram && g_mm_sram;
    g_mm_stamp++;
    pthread_mutex_unlock(&g_mm_lock);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/* A new read: the picture is rebuilt when the game's map changed. */
JNIEXPORT void JNICALL NATIVE(gameMapUpdate)(JNIEnv *env, jclass cls, jbyteArray wram, jbyteArray sram)
{
    pthread_mutex_lock(&g_mm_lock);
    if (g_mm_rom && g_mm && g_mm_wram && g_mm_sram) {
        jsize wl = (*env)->GetArrayLength(env, wram), sl = (*env)->GetArrayLength(env, sram);
        (*env)->GetByteArrayRegion(env, wram, 0, wl < 0x20000 ? wl : 0x20000, (jbyte *)g_mm_wram);
        (*env)->GetByteArrayRegion(env, sram, 0, sl < 0x8000 ? sl : 0x8000, (jbyte *)g_mm_sram);
        if (smz_smmap_update(g_mm, g_mm_rom, g_mm_rom_len, g_mm_wram, 0x20000, g_mm_sram, 0x8000)) g_mm_stamp++;
    }
    pthread_mutex_unlock(&g_mm_lock);
}

/* Changes with every new picture; -1 while there is none. */
JNIEXPORT jint JNICALL NATIVE(gameMapStamp)(JNIEnv *env, jclass cls)
{
    pthread_mutex_lock(&g_mm_lock);
    int s = g_mm_rom && g_mm && g_mm->shown >= 0 ? (g_mm_stamp & 0x7FFFFFFF) : -1;
    pthread_mutex_unlock(&g_mm_lock);
    return s;
}

/* The picture into out (512 x 256 ARGB); then {area shown, Samus's area, her tile x and y, the drawn box x0 y0 x1 y1}. */
JNIEXPORT jintArray JNICALL NATIVE(gameMapPicture)(JNIEnv *env, jclass cls, jintArray out)
{
    static uint32_t px[SMZ_SMMAP_W * SMZ_SMMAP_H];
    jint info[8] = {-1, -1, -1, -1, 0, 0, 0, 0};
    pthread_mutex_lock(&g_mm_lock);
    if (g_mm_rom && g_mm && g_mm->shown >= 0) {
        smz_smmap_draw(g_mm, px, SMZ_SMMAP_W, SMZ_SMMAP_H, SMZ_SMMAP_W, SMZ_SMMAP_W / 2.0f, SMZ_SMMAP_H / 2.0f, 1);
        jint v[8] = {g_mm->shown, g_mm->samus_area, g_mm->samus_x, g_mm->samus_y, g_mm->x0, g_mm->y0, g_mm->x1, g_mm->y1};
        memcpy(info, v, sizeof info);
    }
    pthread_mutex_unlock(&g_mm_lock);
    if (info[0] >= 0 && (*env)->GetArrayLength(env, out) >= SMZ_SMMAP_W * SMZ_SMMAP_H)
        (*env)->SetIntArrayRegion(env, out, 0, SMZ_SMMAP_W * SMZ_SMMAP_H, (const jint *)px);
    jintArray r = (*env)->NewIntArray(env, 8);
    (*env)->SetIntArrayRegion(env, r, 0, 8, info);
    return r;
}

/* {cx, cy, zoom}: the view that shows the area (or Samus) in w x h, as every host starts it. */
JNIEXPORT jfloatArray JNICALL NATIVE(gameMapHome)(JNIEnv *env, jclass cls, jint w, jint h)
{
    float v[3] = {SMZ_SMMAP_W / 2.0f, SMZ_SMMAP_H / 2.0f, 1};
    pthread_mutex_lock(&g_mm_lock);
    if (g_mm) smz_smmap_home(g_mm, w, h, &v[0], &v[1], &v[2]);
    pthread_mutex_unlock(&g_mm_lock);
    jfloatArray r = (*env)->NewFloatArray(env, 3);
    (*env)->SetFloatArrayRegion(env, r, 0, 3, v);
    return r;
}

/* The next (1) or previous (-1) area the player has been in, 0 Samus's again; the picture follows at once. */
JNIEXPORT void JNICALL NATIVE(gameMapStep)(JNIEnv *env, jclass cls, jint dir)
{
    pthread_mutex_lock(&g_mm_lock);
    if (g_mm_rom && g_mm && g_mm_wram && g_mm_sram) {
        smz_smmap_step(g_mm, g_mm_sram, 0x8000, dir);
        if (smz_smmap_update(g_mm, g_mm_rom, g_mm_rom_len, g_mm_wram, 0x20000, g_mm_sram, 0x8000)) g_mm_stamp++;
    }
    pthread_mutex_unlock(&g_mm_lock);
}

JNIEXPORT jstring JNICALL NATIVE(gameMapArea)(JNIEnv *env, jclass cls)
{
    pthread_mutex_lock(&g_mm_lock);
    const char *n = g_mm ? smz_smmap_area_name(g_mm->shown) : "";
    pthread_mutex_unlock(&g_mm_lock);
    return (*env)->NewStringUTF(env, n);
}

/* An ALttP seed hash's five file select icons (z3hash_NN.png), empty when the text is not one. */
JNIEXPORT jintArray JNICALL NATIVE(z3HashIcons)(JNIEnv *env, jclass cls, jstring text)
{
    int ic[5];
    const char *t = (*env)->GetStringUTFChars(env, text, NULL);
    int n = smz_z3_hash_icons(t, ic);
    (*env)->ReleaseStringUTFChars(env, text, t);
    jintArray r = (*env)->NewIntArray(env, n);
    if (n) (*env)->SetIntArrayRegion(env, r, 0, n, ic);
    return r;
}

/* ---- RomM sync (core/ui/smz3_sync.h): the logic stays here, the app's Smz3SyncHost moves the bytes on the calling thread ---- */
#include <stdarg.h>
#include "smz3_sync.h"

typedef struct { JNIEnv *env; jobject host; } JHost;

static jbyteArray jbytes(JNIEnv *env, const void *p, size_t n)
{
    jbyteArray a = (*env)->NewByteArray(env, (jsize)n);
    if (a && n) (*env)->SetByteArrayRegion(env, a, 0, (jsize)n, (const jbyte *)p);
    return a;
}

/* A Java byte[] as a malloc'd buffer with a NUL after it; NULL for a null array. */
static uint8_t *cbytes(JNIEnv *env, jbyteArray a, size_t *len)
{
    if (!a) return NULL;
    jsize n = (*env)->GetArrayLength(env, a);
    uint8_t *p = malloc((size_t)n + 1);
    if (!p) return NULL;
    (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *)p);
    p[n] = 0;
    *len = (size_t)n;
    return p;
}

static jobject jcall(JHost *h, const char *name, const char *sig, ...)
{
    JNIEnv *env = h->env;
    jclass c = (*env)->GetObjectClass(env, h->host);
    jmethodID m = (*env)->GetMethodID(env, c, name, sig);
    (*env)->DeleteLocalRef(env, c);
    va_list ap;
    va_start(ap, sig);
    jobject r = m ? (*env)->CallObjectMethodV(env, h->host, m, ap) : NULL;
    va_end(ap);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); r = NULL; }
    return r;
}

static int jhttp(void *ctx, const char *method, const char *url, const char *headers, const uint8_t *body, size_t len, uint8_t **out, size_t *out_len)
{
    JHost *h = ctx;
    JNIEnv *env = h->env;
    jstring jm = (*env)->NewStringUTF(env, method);
    jbyteArray ju = jbytes(env, url, strlen(url)), jh = headers ? jbytes(env, headers, strlen(headers)) : NULL, jb = body ? jbytes(env, body, len) : NULL;
    jbyteArray r = jcall(h, "http", "(Ljava/lang/String;[B[B[B)[B", jm, ju, jh, jb);
    jclass c = (*env)->GetObjectClass(env, h->host);
    int status = (*env)->GetIntField(env, h->host, (*env)->GetFieldID(env, c, "status", "I"));
    (*env)->DeleteLocalRef(env, c);
    size_t n = 0;
    uint8_t *o = r ? cbytes(env, r, &n) : NULL;
    if (!o) { o = malloc(1); if (o) o[0] = 0; n = 0; }
    *out = o;
    *out_len = n;
    (*env)->DeleteLocalRef(env, jm); (*env)->DeleteLocalRef(env, ju);
    if (jh) (*env)->DeleteLocalRef(env, jh);
    if (jb) (*env)->DeleteLocalRef(env, jb);
    if (r) (*env)->DeleteLocalRef(env, r);
    return r ? status : -1;
}

static uint8_t *jread(void *ctx, int kind, size_t *len)
{
    JHost *h = ctx;
    jbyteArray r = jcall(h, "read", "(I)[B", (jint)kind);
    uint8_t *p = cbytes(h->env, r, len);
    if (r) (*h->env)->DeleteLocalRef(h->env, r);
    return p;
}

static int jwrite(void *ctx, int kind, const uint8_t *data, size_t len)
{
    JHost *h = ctx;
    JNIEnv *env = h->env;
    jbyteArray a = data ? jbytes(env, data, len) : NULL;
    jclass c = (*env)->GetObjectClass(env, h->host);
    jmethodID m = (*env)->GetMethodID(env, c, "write", "(I[B)Z");
    (*env)->DeleteLocalRef(env, c);
    jboolean ok = (*env)->CallBooleanMethod(env, h->host, m, (jint)kind, a);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); ok = JNI_FALSE; }
    if (a) (*env)->DeleteLocalRef(env, a);
    return ok ? 0 : -1;
}

static void jstr(JNIEnv *env, jobjectArray a, int i, char *out, size_t cap)
{
    jstring s = (*env)->GetObjectArrayElement(env, a, i);
    out[0] = 0;
    if (!s) return;
    const char *t = (*env)->GetStringUTFChars(env, s, NULL);
    snprintf(out, cap, "%s", t);
    (*env)->ReleaseStringUTFChars(env, s, t);
    (*env)->DeleteLocalRef(env, s);
}

/* cfg: {romm base, token, device id, SMZ3 server, install id, device name}. */
static void sync_setup(JNIEnv *env, jobject host, jobjectArray cfg, JHost *h, SmzRomm *r, SmzSync *s)
{
    h->env = env;
    h->host = host;
    memset(r, 0, sizeof *r);
    r->http = jhttp;
    r->ctx = h;
    jstr(env, cfg, 0, r->base, sizeof r->base);
    jstr(env, cfg, 1, r->token, sizeof r->token);
    jstr(env, cfg, 2, r->device, sizeof r->device);
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->romm = r;
    jstr(env, cfg, 3, s->server, sizeof s->server);
    jstr(env, cfg, 4, s->install, sizeof s->install);
    jstr(env, cfg, 5, s->device, sizeof s->device);
}

static jobjectArray jstrings(JNIEnv *env, const char *const *v, int n)
{
    jobjectArray r = (*env)->NewObjectArray(env, n, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < n; i++) {
        jstring s = (*env)->NewStringUTF(env, v[i] ? v[i] : "");
        (*env)->SetObjectArrayElement(env, r, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    return r;
}

/* {device code, user code, url, interval, expires in}, or {"", why}. */
JNIEXPORT jobjectArray JNICALL NATIVE(syncLoginStart)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg, jstring platform)
{
    JHost h; SmzRomm r; SmzSync s; SmzRommLogin l;
    sync_setup(env, host, cfg, &h, &r, &s);
    const char *p = (*env)->GetStringUTFChars(env, platform, NULL);
    int e = smz_romm_login_start(&r, s.install, s.device, p, SMZ3_VERSION, &l);
    (*env)->ReleaseStringUTFChars(env, platform, p);
    if (e) { const char *v[2] = {"", r.err}; return jstrings(env, v, 2); }
    char iv[16], ex[16];
    snprintf(iv, sizeof iv, "%d", l.interval);
    snprintf(ex, sizeof ex, "%d", l.expires_in);
    const char *v[5] = {l.device_code, l.user_code, l.url, iv, ex};
    return jstrings(env, v, 5);
}

/* {"0" token device user | "1" (not yet) | "-1" why}. */
JNIEXPORT jobjectArray JNICALL NATIVE(syncLoginPoll)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg, jstring deviceCode)
{
    JHost h; SmzRomm r; SmzRommLogin l;
    sync_setup(env, host, cfg, &h, &r, NULL);
    memset(&l, 0, sizeof l);
    const char *d = (*env)->GetStringUTFChars(env, deviceCode, NULL);
    snprintf(l.device_code, sizeof l.device_code, "%s", d);
    (*env)->ReleaseStringUTFChars(env, deviceCode, d);
    r.token[0] = 0;
    int p = smz_romm_login_poll(&r, &l);
    char user[24] = "";
    if (p == 0) smz_romm_whoami(&r, user, sizeof user);
    const char *v[4] = {p == 0 ? "0" : p == 1 ? "1" : "-1", p == 0 ? r.token : r.err, r.device, user};
    return jstrings(env, v, 4);
}

/* {0 the token works | 1 RomM refuses it | -1 not reached, user}. */
JNIEXPORT jobjectArray JNICALL NATIVE(syncWhoami)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg)
{
    JHost h; SmzRomm r;
    sync_setup(env, host, cfg, &h, &r, NULL);
    char user[24] = "", rc[8];
    snprintf(rc, sizeof rc, "%d", smz_romm_whoami(&r, user, sizeof user));
    const char *v[2] = {rc, user};
    return jstrings(env, v, 2);
}

/* The account's runs: per run {run id, ROM id, note id, ROM name, run text} as UTF-8 bytes; null when RomM failed. */
JNIEXPORT jobjectArray JNICALL NATIVE(syncNotes)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg)
{
    JHost h; SmzRomm r;
    sync_setup(env, host, cfg, &h, &r, NULL);
    SmzRommNote *notes;
    int count;
    if (smz_romm_notes(&r, &notes, &count)) return NULL;
    jobjectArray out = (*env)->NewObjectArray(env, count * 5, (*env)->FindClass(env, "[B"), NULL);
    for (int i = 0; i < count; i++) {
        char rom[96] = "", ids[2][16], *text = NULL;
        smz_sync_note_info(notes[i].content, NULL, 0, rom, sizeof rom, NULL, &text);
        snprintf(ids[0], sizeof ids[0], "%d", notes[i].rom_id);
        snprintf(ids[1], sizeof ids[1], "%d", notes[i].note_id);
        const char *v[5] = {notes[i].run_id, ids[0], ids[1], rom[0] ? rom : notes[i].rom_name, text ? text : ""};
        for (int k = 0; k < 5; k++) {
            jbyteArray b = jbytes(env, v[k], strlen(v[k]));
            (*env)->SetObjectArrayElement(env, out, i * 5 + k, b);
            (*env)->DeleteLocalRef(env, b);
        }
        free(text);
    }
    smz_romm_notes_free(notes, count);
    return out;
}

/* Pull (push false) or push one run. run: {run id, ROM name, name, proof or ""}. Returns {result, changed, unranked,
 * cloud device, cloud ms, why, play ms}. */
JNIEXPORT jobjectArray JNICALL NATIVE(syncRun)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg, jboolean push, jboolean force,
                                              jobjectArray run, jboolean online, jlong playMs, jint romId, jlong nowMs)
{
    JHost h; SmzRomm r; SmzSync s;
    sync_setup(env, host, cfg, &h, &r, &s);
    s.now_ms = nowMs;
    char id[48], rom[96], name[48];
    jstr(env, run, 0, id, sizeof id);
    jstr(env, run, 1, rom, sizeof rom);
    jstr(env, run, 2, name, sizeof name);
    jstring jp = (*env)->GetObjectArrayElement(env, run, 3);
    const char *proof = jp ? (*env)->GetStringUTFChars(env, jp, NULL) : NULL;
    SmzSyncRun sr = {id, rom, name, online, playMs, proof && proof[0] ? proof : NULL, romId};
    SmzSyncFiles fs = {jread, jwrite, &h};
    SmzSyncResult res;
    int rc = push ? smz_sync_push(&s, &sr, &fs, force, &res) : smz_sync_pull(&s, &sr, &fs, force, &res);
    if (proof) (*env)->ReleaseStringUTFChars(env, jp, proof);
    char v0[8], v1[4], v2[4], v4[24], v6[24];
    snprintf(v0, sizeof v0, "%d", rc);
    snprintf(v1, sizeof v1, "%d", res.changed);
    snprintf(v2, sizeof v2, "%d", res.unranked);
    snprintf(v4, sizeof v4, "%lld", (long long)res.cloud_at);
    snprintf(v6, sizeof v6, "%lld", (long long)res.play_ms);
    const char *v[7] = {v0, v1, v2, res.cloud_dev, v4, res.err, v6};
    return jstrings(env, v, 7);
}

/* SMZ3's emulator wrote a run file (SMZ_SYNC_* kind): its hash goes into the run's sync record. */
JNIEXPORT void JNICALL NATIVE(syncOwn)(JNIEnv *env, jclass cls, jobject host, jint kind, jbyteArray data)
{
    JHost h = {env, host};
    SmzSyncFiles fs = {jread, jwrite, &h};
    size_t n = 0;
    uint8_t *d = cbytes(env, data, &n);
    if (d) smz_sync_own(&fs, kind, d, n);
    free(d);
}

/* A deleted run: its saves, states and note go from RomM too (the host reads the run's old sync record). */
JNIEXPORT void JNICALL NATIVE(syncDelete)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg)
{
    JHost h; SmzRomm r;
    sync_setup(env, host, cfg, &h, &r, NULL);
    SmzSyncFiles fs = {jread, jwrite, &h};
    SmzSyncRecord rec;
    smz_sync_record_load(&fs, &rec);
    smz_sync_delete(&r, &rec);
}

/* Writes a fresh sync record for a run brought from the account: revision 0, so opening it brings everything down. */
JNIEXPORT void JNICALL NATIVE(syncNewRecord)(JNIEnv *env, jclass cls, jobject host, jint romId, jint noteId)
{
    JHost h = {env, host};
    SmzSyncFiles fs = {jread, jwrite, &h};
    SmzSyncRecord rec = {0};
    rec.rom_id = romId;
    rec.note_id = noteId;
    smz_sync_record_save(&fs, &rec);
}

/* A run already here that RomM now lists under other ids. */
JNIEXPORT void JNICALL NATIVE(syncKeepIds)(JNIEnv *env, jclass cls, jobject host, jint romId, jint noteId)
{
    JHost h = {env, host};
    SmzSyncFiles fs = {jread, jwrite, &h};
    smz_sync_record_ids(&fs, romId, noteId);
}

/* The player link: what 0 asks for a code ({"0", code} or {"-1", why}), 1 links this install with code. */
JNIEXPORT jobjectArray JNICALL NATIVE(syncPlayer)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg, jint what, jstring code)
{
    JHost h; SmzRomm r; SmzSync s;
    sync_setup(env, host, cfg, &h, &r, &s);
    char c[8] = "";
    int e;
    if (what == 0) e = smz_sync_player_code(&s, c);
    else {
        const char *t = (*env)->GetStringUTFChars(env, code, NULL);
        e = smz_sync_player_link(&s, t);
        (*env)->ReleaseStringUTFChars(env, code, t);
    }
    const char *v[2] = {e ? "-1" : "0", e ? r.err : c};
    return jstrings(env, v, 2);
}

/* An online run is played on one device at a time: 0 held here, 1 another device holds it, -1 not reached. */
JNIEXPORT jint JNICALL NATIVE(syncHold)(JNIEnv *env, jclass cls, jobject host, jobjectArray cfg, jstring runId, jboolean force)
{
    JHost h; SmzRomm r; SmzSync s;
    sync_setup(env, host, cfg, &h, &r, &s);
    const char *t = (*env)->GetStringUTFChars(env, runId, NULL);
    int e = smz_sync_hold(&s, t, force);
    (*env)->ReleaseStringUTFChars(env, runId, t);
    return e;
}

JNIEXPORT jstring JNICALL NATIVE(syncNewId)(JNIEnv *env, jclass cls, jchar letter, jlong entropy)
{
    char id[40];
    smz_sync_new_id((char)letter, (uint64_t)entropy, id);
    return (*env)->NewStringUTF(env, id);
}

JNIEXPORT void JNICALL NATIVE(uiCloudEnable)(JNIEnv *env, jclass cls, jboolean on) { ui_cloud_enable(&g_ui, on); }

JNIEXPORT void JNICALL NATIVE(uiCloudState)(JNIEnv *env, jclass cls, jint state, jstring user, jstring server, jstring code, jstring url)
{
    const char *u = (*env)->GetStringUTFChars(env, user, NULL), *s = (*env)->GetStringUTFChars(env, server, NULL);
    const char *c = (*env)->GetStringUTFChars(env, code, NULL), *l = (*env)->GetStringUTFChars(env, url, NULL);
    ui_cloud_state(&g_ui, state, u, s, c, l);
    (*env)->ReleaseStringUTFChars(env, user, u); (*env)->ReleaseStringUTFChars(env, server, s);
    (*env)->ReleaseStringUTFChars(env, code, c); (*env)->ReleaseStringUTFChars(env, url, l);
}

JNIEXPORT void JNICALL NATIVE(uiCloudMsg)(JNIEnv *env, jclass cls, jstring msg)
{
    const char *m = (*env)->GetStringUTFChars(env, msg, NULL);
    ui_cloud_msg(&g_ui, m);
    (*env)->ReleaseStringUTFChars(env, msg, m);
}

JNIEXPORT void JNICALL NATIVE(uiCloudConflict)(JNIEnv *env, jclass cls, jint slot, jstring device, jstring when)
{
    const char *d = (*env)->GetStringUTFChars(env, device, NULL), *w = (*env)->GetStringUTFChars(env, when, NULL);
    ui_cloud_conflict(&g_ui, slot, d, w);
    (*env)->ReleaseStringUTFChars(env, device, d);
    (*env)->ReleaseStringUTFChars(env, when, w);
}

/* {the run of the conflict, the link code typed}. */
JNIEXPORT jobjectArray JNICALL NATIVE(uiCloudInfo)(JNIEnv *env, jclass cls)
{
    char slot[8];
    snprintf(slot, sizeof slot, "%d", g_ui.cloud.slot);
    const char *v[3] = {slot, g_ui.cloud.link, g_ui.cloud.addr};
    return jstrings(env, v, 3);
}
