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

#define NATIVE(name) Java_com_swordfish_lemuroid_app_shared_game_viewmodel_Smz3Native_##name

static SmzGoal g_goal;   /* the daily challenge or Draft goal of the run being played (smz3_goal.h) */
static SmzCounters g_counters;   /* the run's counters: world goals, and a Draft's kills and rupees */

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

/* Adds the shortcut hooks to a run's ROM made before them; null when it already has them or is not an SMZ3 ROM. */
JNIEXPORT jbyteArray JNICALL NATIVE(addShortcutHooks)(JNIEnv *env, jclass cls, jbyteArray rom)
{
    jsize n = (*env)->GetArrayLength(env, rom);
    if (n < 0x410000) return NULL;
    jbyte *b = (*env)->GetByteArrayElements(env, rom, NULL);
    int ok = smz_rom_shortcut_hooks((uint8_t *)b) == 0;
    if (ok) smz_rom_checksum((uint8_t *)b);
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

JNIEXPORT void JNICALL NATIVE(uiSetMonthly)(JNIEnv *env, jclass cls, jstring label, jint seed, jint days)
{
    const char *l = (*env)->GetStringUTFChars(env, label, NULL);
    ui_set_monthly(&g_ui, l, seed, days);
    (*env)->ReleaseStringUTFChars(env, label, l);
}

JNIEXPORT jint JNICALL NATIVE(uiRunMonthly)(JNIEnv *env, jclass cls) { return ui_run_monthly(&g_ui); }

JNIEXPORT void JNICALL NATIVE(uiSetWeekly)(JNIEnv *env, jclass cls, jstring label, jint seed, jint days)
{
    const char *l = (*env)->GetStringUTFChars(env, label, NULL);
    ui_set_weekly(&g_ui, l, seed, days);
    (*env)->ReleaseStringUTFChars(env, label, l);
}

JNIEXPORT void JNICALL NATIVE(uiLadderEnable)(JNIEnv *env, jclass cls, jboolean on) { ui_ladder_enable(&g_ui, on); }

/* The server's answer for one online table (0 original, 1 monthly, 2 weekly); null marks it failed. */
JNIEXPORT void JNICALL NATIVE(uiLadderJson)(JNIEnv *env, jclass cls, jint tab, jstring json)
{
    const char *j = json ? (*env)->GetStringUTFChars(env, json, NULL) : NULL;
    ui_ladder_load_json(&g_ui, tab, j);
    if (j) (*env)->ReleaseStringUTFChars(env, json, j);
}

JNIEXPORT jint JNICALL NATIVE(uiRunWeekly)(JNIEnv *env, jclass cls) { return ui_run_weekly(&g_ui); }

JNIEXPORT void JNICALL NATIVE(uiSetSlotOnline)(JNIEnv *env, jclass cls, jint i, jint kind) { ui_set_slot_online(&g_ui, i, kind); }

JNIEXPORT jint JNICALL NATIVE(uiOnline)(JNIEnv *env, jclass cls) { return ui_online_now(&g_ui); }

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
JNIEXPORT jint JNICALL NATIVE(raceGoalProgress)(JNIEnv *env, jclass cls)
{
    if (g_goal.kind != GOAL_KILLS && g_goal.kind != GOAL_RUPEES) return -1;
    return smz_goal_progress(&g_goal, &g_counters, 0);
}
