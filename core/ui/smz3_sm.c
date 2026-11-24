#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_sm.h"
#include "smz3_run.h"
#include "sm_generate.h"
#include "smz3_state.h"

size_t smz_mode_rom_size(int mode) { return mode == SMZ_MODE_SM ? SMZ_SM_ROM_SIZE : mode == SMZ_MODE_Z3 ? 0x200000 : 0x600000; }

/* The 32 words of sm_randomizer_rom's randopatches/seed_display.asm, one per hash byte (its low 5 bits). */
static const char *const WORDS[32] = {
    "GEEMER", "RIPPER", "ATOMIC", "POWAMP", "SCISER", "NAMIHE", "PUROMI", "ALCOON", "BEETOM", "OWTCH", "ZEBBO", "ZEELA",
    "HOLTZ", "VIOLA", "WAVER", "RINKA", "BOYON", "CHOOT", "KAGO", "SKREE", "COVERN", "EVIR", "TATORI", "OUM", "PUYO",
    "YARD", "ZOA", "FUNE", "GAMET", "GERUTA", "SOVA", "BULL"};

void smz_sm_hash_text(const uint8_t *rom, size_t len, char *out, size_t cap)
{
    if (!cap) return;
    out[0] = 0;
    if (len < 0x2FFF04) return;
    const uint8_t *h = rom + 0x2FFF00;
    snprintf(out, cap, "%s %s %s %s", WORDS[h[0] & 31], WORDS[h[1] & 31], WORDS[h[2] & 31], WORDS[h[3] & 31]);
}

/* The Samus block of an RDC sprite (smz3_sprite.c's field list, the same SpriteSomething layout) moved to the standalone
 * ROM: the combo's tile banks $44-$4B, $54-$59 and $5A (death) are $9C-$9F, $F5-$FE and $FF there (sprite/gfx.asm of both
 * base ROMs); every other field is Super Metroid's own LoROM address, which the combo keeps in its upper half banks. */
typedef struct { int addr[2]; int length; int count; int stride; } Field;
#define F(a, len, n, stride) {{a, -1}, len, n, stride}
#define F2(a, b, len) {{a, b}, len, 1, 0}
static const Field SAMUS[] = {
    F(0x440000, 0x8000, 1, 0), F(0x450000, 0x8000, 1, 0), F(0x460000, 0x8000, 1, 0), F(0x470000, 0x8000, 1, 0),
    F(0x480000, 0x8000, 1, 0), F(0x490000, 0x8000, 1, 0), F(0x4A0000, 0x8000, 1, 0), F(0x4B0000, 0x8000, 1, 0),
    F(0x540000, 0x8000, 1, 0), F(0x550000, 0x8000, 1, 0), F(0x560000, 0x8000, 1, 0), F(0x570000, 0x8000, 1, 0),
    F(0x580000, 0x8000, 1, 0), F(0x590000, 0x7880, 1, 0), F(0x5A0000, 0x3F60, 1, 0), F(0x5A4000, 0x3F60, 1, 0),
    F(0x1A9A00, 0x3C0, 1, 0), F(0x36DA00, 0x600, 1, 0), F(0x36D900, 0x20, 1, 0), F(0x36D980, 0x20, 1, 0),
    F(0x1B9402, 30, 1, 0), F(0x1B9522, 30, 1, 0), F(0x1B9802, 30, 1, 0),
    F(0xDDB6D, 30, -9, 0), F(0xDDCD3, 30, -9, 0), F(0xDDE39, 30, -9, 0),
    F(0xDE468, 30, 16, 0x22), F(0xDE694, 30, 16, 0x22), F(0xDE8C0, 30, 16, 0x22),
    F(0x1B9822, 30, 8, 0x20), F(0x1B9922, 30, 8, 0x20), F(0x1B9A22, 30, 8, 0x20),
    F(0x1B9B22, 30, 4, 0x20), F(0x1B9D22, 30, 4, 0x20), F(0x1B9F22, 30, 4, 0x20),
    F(0x1B9BA2, 30, 4, 0x20), F(0x1B9DA2, 30, 4, 0x20), F(0x1B9FA2, 30, 4, 0x20),
    F(0x1B9C22, 30, 4, 0x20), F(0x1B9E22, 30, 4, 0x20), F(0x1BA022, 30, 4, 0x20),
    F(0x1B9CA2, 30, 4, 0x20), F(0x1B9EA2, 30, 4, 0x20), F(0x1BA0A2, 30, 4, 0x20),
    F(0x1B96C2, 30, 6, 0x20), F(0x1BA122, 30, 9, 0x20), F(0x1BA242, 30, 10, 0x20),
    F2(0x1BA3A2, 0xCE56B, 30), F(0x1BA382, 30, 1, 0), F2(0x1BA3C0, 0x1BA3C6, 6),
    F(0x2E52C, 2, 1, 0), F(0xEE5E2, 30, 1, 0), F(0xCE68B, 30, 1, 0), F(0xDD6C2, 30, 16, 0x24),
    F(0x22A5A0, 28, 1, 0), F(0xDCA54, 2, 14, 0x6),
};
static const int LOADER_OFFSETS[9] = {0x0, 0x24, 0x4F, 0x73, 0x9E, 0xC2, 0xED, 0x111, 0x139};
static const int TILE_BANKS[15][2] = {
    {0x44, 0x9C}, {0x45, 0x9D}, {0x46, 0x9E}, {0x47, 0x9F}, {0x48, 0xF5}, {0x49, 0xF6}, {0x4A, 0xF7}, {0x4B, 0xF8},
    {0x54, 0xF9}, {0x55, 0xFA}, {0x56, 0xFB}, {0x57, 0xFC}, {0x58, 0xFD}, {0x59, 0xFE}, {0x5A, 0xFF}};

static long lorom_of(long a)
{
    if (a >= 0x400000) {
        for (int i = 0; i < 15; i++) if (a >> 16 == TILE_BANKS[i][0]) return (long)(TILE_BANKS[i][1] & 0x7F) * 0x8000 + (a & 0x7FFF);
        return -1;
    }
    return a & 0x8000 ? (a >> 16) * 0x8000 + (a & 0x7FFF) : -1;
}

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int sprite_apply(uint8_t *rom, const uint8_t *rdc, size_t len)
{
    if (len < 23 || memcmp(rdc, "RETRODATACONTAINER", 18) || rdc[18] != 1) return -1;
    uint32_t blocks = u32(rdc + 19);
    if (blocks > 64 || 23 + (size_t)blocks * 8 > len) return -1;
    for (uint32_t b = 0; b < blocks; b++) {
        if (u32(rdc + 23 + b * 8) != 4) continue;
        size_t pos = u32(rdc + 27 + b * 8);
        for (size_t f = 0; f < sizeof SAMUS / sizeof SAMUS[0]; f++) {
            const Field *d = &SAMUS[f];
            int n = d->count < 0 ? -d->count : d->count;
            if (pos > len || (size_t)d->length * n > len - pos) return -1;
            for (int a = 0; a < 2 && d->addr[a] >= 0; a++)
                for (int i = 0; i < n; i++) {
                    long o = lorom_of(d->addr[a] + (d->count < 0 ? LOADER_OFFSETS[i] : i * d->stride));
                    if (o < 0 || o + d->length > SMZ_SM_ROM_SIZE) return -1;
                    memcpy(rom + o, rdc + pos + (size_t)i * d->length, d->length);
                }
            pos += (size_t)d->length * n;
        }
        return 0;
    }
    return -1;
}

/* The "sm" object of the settings: every "key":"value" pair goes to sm_option_set. */
static int sm_settings(const char *json, SmGenSettings *s)
{
    sm_settings_defaults(s);
    const char *p = strstr(json, "\"sm\":{"), *seed = strstr(json, "\"seed\":\"");
    if (!p || !seed) return -1;
    char v[16];
    size_t n = strcspn(seed + 8, "\"");
    if (n >= sizeof v) return -1;
    memcpy(v, seed + 8, n); v[n] = 0;
    if (sm_option_set(s, "seed", v)) return -1;
    for (p += 6; *p == '"';) {
        char key[24], val[24];
        const char *k = p + 1, *ke = strchr(k, '"');
        if (!ke || ke[1] != ':' || ke[2] != '"') return -1;
        const char *w = ke + 3, *we = strchr(w, '"');
        if (!we || (size_t)(ke - k) >= sizeof key || (size_t)(we - w) >= sizeof val) return -1;
        memcpy(key, k, ke - k); key[ke - k] = 0;
        memcpy(val, w, we - w); val[we - w] = 0;
        if (sm_option_set(s, key, val)) return -1;
        p = we + 1 + (we[1] == ',');
    }
    return 0;
}

/* Inserts text at json + at, if it fits. */
static int insert(char *json, size_t cap, size_t at, const char *text)
{
    size_t n = strlen(json), k = strlen(text);
    if (n + k + 1 > cap || at > n) return -1;
    memmove(json + at + k, json + at, n - at + 1);
    memcpy(json + at, text, k);
    return 0;
}

static SmzMapRandoFn g_mr_fn;
static int g_mr_state;
static char g_mr_error[120];

void smz_sm_set_maprando(SmzMapRandoFn fn, int state) { g_mr_fn = fn; g_mr_state = fn ? state : SMZ_MR_NONE; }
int smz_sm_maprando_state(void) { return g_mr_state; }
int smz_sm_is_maprando(const char *json) { return json && strstr(json, "\"generator\":\"maprando\"") != NULL; }
const char *smz_sm_error(int rc) { return rc == SMZ_SM_ERR_MAPRANDO ? g_mr_error : rc == -6 ? "The Samus sprite is not valid" : NULL; }

/* A string field of a flat JSON object, "" when missing. */
static void json_field(const char *json, const char *key, char *out, size_t cap)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    out[0] = 0;
    if (p) snprintf(out, cap, "%.*s", (int)strcspn(p + strlen(pat), "\""), p + strlen(pat));
}

/* The run's Map Rando settings as the crate takes them: its "sm" object without "generator". */
static int mr_settings(const char *json, char *out, size_t cap)
{
    const char *p = strstr(json, "\"sm\":{"), *end = p ? strchr(p, '}') : NULL;
    if (!end) return -1;
    p += 5;
    static const char GEN[] = "\"generator\":\"maprando\"";
    const char *g = strstr(p, GEN);
    if (!g || g > end) return -1;
    const char *after = g + sizeof GEN - 1;
    if (*after == ',') after++;
    int n = snprintf(out, cap, "%.*s%.*s", (int)(g - p), p, (int)(end + 1 - after), after);
    if (n < 0 || (size_t)n >= cap) return -1;
    if (n > 2 && out[n - 2] == ',') { out[n - 2] = '}'; out[n - 1] = 0; }
    return 0;
}

static int mr_generate(const char *json, const uint8_t *sm, uint8_t *rom_out, size_t cap, char *words, size_t wcap)
{
    char settings[256], info[512], seed[24];
    if (!g_mr_fn || g_mr_state != SMZ_MR_READY) { snprintf(g_mr_error, sizeof g_mr_error, "Map Rando is not available here"); return SMZ_SM_ERR_MAPRANDO; }
    if (mr_settings(json, settings, sizeof settings)) return -5;
    json_field(json, "seed", seed, sizeof seed);
    info[0] = 0;
    if (g_mr_fn(settings, strtoull(seed, NULL, 10), sm, SMZ_SM_BASE_SIZE, rom_out, cap, info, sizeof info)) {
        json_field(info, "error", g_mr_error, sizeof g_mr_error);
        if (!g_mr_error[0]) snprintf(g_mr_error, sizeof g_mr_error, "Map Rando failed");
        return SMZ_SM_ERR_MAPRANDO;
    }
    json_field(info, "hash", words, wcap);
    return 0;
}

int smz_sm_generate(char *json, size_t json_cap, const uint8_t *sm, const uint8_t *samus_rdc, size_t samus_len,
                    uint8_t *rom_out, size_t cap)
{
    SmGenSettings s;
    uint8_t hash[4];
    char words[64], add[96];
    if (smz_settings_mode(json) != SMZ_MODE_SM) return -5;
    if (smz_sm_is_maprando(json)) {
        int rc = mr_generate(json, sm, rom_out, cap, words, sizeof words);
        if (rc) return rc;
    } else {
        if (sm_settings(json, &s)) return -5;
        int rc = sm_generate(&s, sm, rom_out, cap, NULL, NULL, hash);
        if (rc) return rc;
        if (samus_rdc && sprite_apply(rom_out, samus_rdc, samus_len)) return -6;
        smz_sm_hash_text(rom_out, SMZ_SM_ROM_SIZE, words, sizeof words);
    }
    const char *diff = strstr(json, "\"diff\":[");
    if (diff && !strstr(json, "\"hash\":\"")) {
        snprintf(add, sizeof add, "\"Hash: %s\"%s", words, diff[8] == ']' ? "" : ",");
        insert(json, json_cap, (size_t)(diff - json) + 8, add);
        snprintf(add, sizeof add, "\"hash\":\"%s\",", words);
        insert(json, json_cap, (size_t)(diff - json), add);
    }
    return 0;
}

int smz_sm_loc_of(int tracker_index)
{
    if (tracker_index < 0 || tracker_index >= (int)smz3_location_count) return -1;
    const smz3_location *l = &smz3_locations[tracker_index];
    if (l->game == SMZ3_GAME_ZELDA || !l->flag) return -1;
    int bit = 0;
    while (!(l->flag >> bit & 1)) bit++;
    return sm_loc_index((int)l->addr * 8 + bit);
}

int smz_sm_tracker_index(int sm_index)
{
    for (int j = 0; j < (int)smz3_location_count; j++) if (smz_sm_loc_of(j) == sm_index && sm_index >= 0) return j;
    return -1;
}

int smz_sm_proof_logic(const char *json, const SmzProof *p, SmzProofVerdict *v)
{
    static SmSeed seed;
    SmGenSettings s;
    if (smz_sm_is_maprando(json)) return 0;   /* Map Rando's logic is the crate's: only the time and rate rules apply */
    if (sm_settings(json, &s) || sm_fill(&seed, &s)) return -1;
    SmProg prog;
    memset(&prog, 0, sizeof prog);
    for (int i = 0; i < p->n; i++) {
        int idx = smz_sm_loc_of(p->loc[i]);
        if (idx < 0) { v->out_of_logic++; continue; }
        if (!sm_loc_available(idx, s.logic, &prog)) v->out_of_logic++;
        if (seed.item_at[idx].type < SM_ITEM_SLOTS) prog.n[seed.item_at[idx].type]++;
    }
    return 0;
}
