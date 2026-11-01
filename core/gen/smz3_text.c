#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_text.h"
#include "smz3_world_gen.h"
#include "smz3_texts_data.h"
#include "smz3_hints_data.h"

#define MAXCP 4096
#define MAXLINES 256

typedef struct { uint32_t c[MAXCP]; int n; } U32;
typedef struct { int start, len; } Span;

static void to_u32(const char *s, U32 *u)
{
    const unsigned char *p = (const unsigned char *)s;
    u->n = 0;
    while (*p && u->n < MAXCP) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xE0) == 0xC0) { cp = (*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
        else { cp = (*p & 0x07) << 18 | (p[1] & 0x3F) << 12 | (p[2] & 0x3F) << 6 | (p[3] & 0x3F); p += 4; }
        u->c[u->n++] = cp;
    }
}

static void from_u32(const uint32_t *c, int n, char *out, size_t cap)
{
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t cp = c[i];
        char tmp[4]; int k;
        if (cp < 0x80) { tmp[0] = (char)cp; k = 1; }
        else if (cp < 0x800) { tmp[0] = (char)(0xC0 | cp >> 6); tmp[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
        else { tmp[0] = (char)(0xE0 | cp >> 12); tmp[1] = (char)(0x80 | (cp >> 6 & 0x3F)); tmp[2] = (char)(0x80 | (cp & 0x3F)); k = 3; }
        if (o + k + 1 >= cap) break;
        memcpy(out + o, tmp, k);
        o += k;
    }
    out[o] = 0;
}

static int letter_bytes(uint32_t c, uint8_t *b)
{
    if (c >= '0' && c <= '9') { b[0] = (uint8_t)(c - '0' + 0xA0); return 1; }
    if (c >= 'A' && c <= 'Z') { b[0] = (uint8_t)(c - 'A' + 0xAA); return 1; }
    if (c >= 'a' && c <= 'z') { b[0] = (uint8_t)(c - 'a' + 0x30); return 1; }
    for (int i = 0; i < SMZ_LETTER_COUNT; i++)
        if (SMZ_LETTERS[i].cp == c) { memcpy(b, SMZ_LETTERS[i].b, SMZ_LETTERS[i].n); return SMZ_LETTERS[i].n; }
    b[0] = 0xFF;
    return 1;
}

/* lines of a text, split at '\n' */
static int split_lines(const U32 *u, Span *sp)
{
    int n = 0, start = 0;
    for (int i = 0; i <= u->n; i++)
        if (i == u->n || u->c[i] == '\n') { sp[n].start = start; sp[n].len = i - start; n++; start = i + 1; if (n >= MAXLINES) break; }
    return n;
}

static int simple_len(int len) { return len; }

int smz_dialog_simple(const char *text, uint8_t *out, int cap)
{
    const int max_bytes = 256, wrap = 19;
    U32 u; Span sp[MAXLINES];
    to_u32(text, &u);
    int nl = split_lines(&u, sp);
    uint8_t tmp[4096]; int n = 0;
    for (int li = 0; li < nl; li++) {
        tmp[n++] = li == 0 ? 0x74 : li == 1 ? 0x75 : 0x76;
        int len = simple_len(sp[li].len) > wrap ? wrap : sp[li].len;
        for (int k = 0; k < len; k++) {
            uint8_t b[3];
            int nb = letter_bytes(u.c[sp[li].start + k], b);
            for (int j = 0; j < nb; j++) { tmp[n++] = 0x00; tmp[n++] = b[j]; }
        }
        int next = li + 1;
        if (next < nl) {
            if (next % 3 == 0) tmp[n++] = 0x7E;
            if (next >= 3) tmp[n++] = 0x73;
        }
    }
    if (n > max_bytes - 1) n = max_bytes - 1;
    tmp[n++] = 0x7F;
    if (n > cap) n = cap;
    memcpy(out, tmp, n);
    return n;
}

typedef struct { U32 lines[MAXLINES]; int n; } Lines;

static void wordwrap(const U32 *u, int width, Lines *out)
{
    Span sp[MAXLINES];
    int nl = split_lines(u, sp);
    out->n = 0;
    for (int li = 0; li < nl; li++) {
        U32 line; line.n = sp[li].len; memcpy(line.c, u->c + sp[li].start, line.n * sizeof(uint32_t));
        while (line.n > 0 && (line.c[line.n - 1] == ' ' || line.c[line.n - 1] == '\t' || line.c[line.n - 1] == '\r')) line.n--;
        if (line.n <= width) { out->lines[out->n++] = line; continue; }
        /* split at single spaces, like string.Split(' ') */
        static U32 stack[MAXLINES]; int sn = 0;
        stack[sn].n = 0; sn++;
        int ws = 0;
        for (int i = 0; i <= line.n; i++) {
            if (i < line.n && line.c[i] != ' ') continue;
            U32 word; word.n = i - ws; memcpy(word.c, line.c + ws, word.n * sizeof(uint32_t));
            ws = i + 1;
            U32 inner = stack[--sn];
            if (inner.n + word.n <= width) {
                memcpy(inner.c + inner.n, word.c, word.n * sizeof(uint32_t)); inner.n += word.n; inner.c[inner.n++] = ' ';
            } else {
                if (inner.n > 0) stack[sn++] = inner;
                inner = word;
                while (inner.n > width) {
                    U32 head; head.n = width; memcpy(head.c, inner.c, width * sizeof(uint32_t));
                    stack[sn++] = head;
                    memmove(inner.c, inner.c + width, (inner.n - width) * sizeof(uint32_t)); inner.n -= width;
                }
                inner.c[inner.n++] = ' ';
            }
            stack[sn++] = inner;
        }
        for (int i = 0; i < sn; i++) {
            U32 l = stack[i];
            while (l.n > 0 && (l.c[l.n - 1] == ' ' || l.c[l.n - 1] == '\t')) l.n--;
            out->lines[out->n++] = l;
        }
    }
}

/* If the line starts with {...}, returns the length of the command. */
static int command_len(const U32 *l)
{
    if (l->n == 0 || l->c[0] != '{') return 0;
    for (int i = 1; i < l->n; i++) if (l->c[i] == '}') return i + 1;
    return 0;
}

static const struct { const char *name; uint8_t n; uint8_t b[13]; } COMMANDS[] = {
    { "{SPEED0}", 2, { 0xFC, 0x00 } }, { "{SPEED2}", 2, { 0xFC, 0x02 } }, { "{SPEED6}", 2, { 0xFC, 0x06 } },
    { "{PAUSE1}", 3, { 0xFE, 0x78, 0x01 } }, { "{PAUSE3}", 3, { 0xFE, 0x78, 0x03 } }, { "{PAUSE5}", 3, { 0xFE, 0x78, 0x05 } },
    { "{PAUSE7}", 3, { 0xFE, 0x78, 0x07 } }, { "{PAUSE9}", 3, { 0xFE, 0x78, 0x09 } }, { "{INPUT}", 1, { 0xFA } },
    { "{CHOICE}", 2, { 0xFE, 0x68 } }, { "{ITEMSELECT}", 2, { 0xFE, 0x69 } }, { "{CHOICE2}", 2, { 0xFE, 0x71 } },
    { "{CHOICE3}", 2, { 0xFE, 0x72 } }, { "{C:GREEN}", 3, { 0xFE, 0x77, 0x07 } }, { "{C:YELLOW}", 3, { 0xFE, 0x77, 0x02 } },
    { "{HARP}", 3, { 0xFE, 0x79, 0x2D } }, { "{MENU}", 3, { 0xFE, 0x6D, 0x00 } }, { "{BOTTOM}", 3, { 0xFE, 0x6D, 0x01 } },
    { "{NOBORDER}", 3, { 0xFE, 0x6B, 0x02 } }, { "{CHANGEPIC}", 4, { 0xFE, 0x67, 0xFE, 0x67 } }, { "{CHANGEMUSIC}", 2, { 0xFE, 0x67 } },
    { "{INTRO}", 13, { 0xFE, 0x6E, 0x00, 0xFE, 0x77, 0x07, 0xFC, 0x03, 0xFE, 0x6B, 0x02, 0xFE, 0x67 } },
    { "{IBOX}", 9, { 0xFE, 0x6B, 0x02, 0xFE, 0x77, 0x07, 0xFC, 0x03, 0xF7 } },
};

int smz_dialog_compiled(const char *text, uint8_t *out, int cap)
{
    const int max_bytes = 2046, wrap = 19;
    static U32 u; static Lines lines;
    to_u32(text, &u);
    wordwrap(&u, wrap, &lines);
    int pad_out = 0, pause = 1, line_count = 0;
    for (int i = 0; i < lines.n; i++) if (!command_len(&lines.lines[i])) line_count++;
    uint8_t tmp[8192]; int n = 0;
    tmp[n++] = 0xFB;
    int li = 0;
    for (int i = 0; i < lines.n; i++) {
        const U32 *l = &lines.lines[i];
        int cl = command_len(l);
        if (cl) {
            char cmd[64];
            from_u32(l->c, cl, cmd, sizeof cmd);
            if (!strcmp(cmd, "{NOTEXT}")) {
                static const uint8_t nt[7] = { 0xFB, 0xFE, 0x6E, 0x00, 0xFE, 0x6B, 0x04 };
                memcpy(out, nt, 7);
                return 7;
            }
            if (!strcmp(cmd, "{INTRO}")) pad_out = 1;
            if (!strcmp(cmd, "{NOPAUSE}")) { pause = 0; continue; }
            int found = 0;
            for (size_t k = 0; k < sizeof COMMANDS / sizeof COMMANDS[0]; k++)
                if (!strcmp(cmd, COMMANDS[k].name)) { memcpy(tmp + n, COMMANDS[k].b, COMMANDS[k].n); n += COMMANDS[k].n; found = 1; break; }
            if (!found) return -1;
            continue;
        }
        if (li > 0) tmp[n++] = li == 1 ? 0xF8 : li == 2 ? 0xF9 : 0xF6;
        int len = l->n;
        for (int k = 0; k < len; k++) { uint8_t b[3]; int nb = letter_bytes(l->c[k], b); memcpy(tmp + n, b, nb); n += nb; }
        if (pad_out && li < 3)
            for (int k = len; k < wrap; k++) tmp[n++] = 0x4F;   /* PadRight with spaces */
        li++;
        if (pause && li % 3 == 0 && li < line_count) tmp[n++] = 0xFA;
    }
    if (n > max_bytes) n = max_bytes;
    if (n > cap) n = cap;
    memcpy(out, tmp, n);
    return n;
}

void smz_game_safe_string(const char *text, char *out, size_t cap)
{
    U32 u; Span sp[MAXLINES];
    to_u32(text, &u);
    int nl = split_lines(&u, sp), too_long = 0;
    for (int i = 0; i < nl; i++) if (sp[i].len > 19) too_long = 1;
    if (!too_long) { snprintf(out, cap, "%s", text); return; }
    /* collapse whitespace runs into single spaces */
    static U32 w; w.n = 0;
    int in_space = 0;
    for (int i = 0; i < u.n; i++) {
        int sp_ = u.c[i] == ' ' || u.c[i] == '\n' || u.c[i] == '\r' || u.c[i] == '\t';
        if (sp_) { if (!in_space) w.c[w.n++] = ' '; in_space = 1; } else { w.c[w.n++] = u.c[i]; in_space = 0; }
    }
    static U32 outc; outc.n = 0;
    U32 cur; cur.n = 0;
    int ws = 0;
    for (int i = 0; i <= w.n; i++) {
        if (i < w.n && w.c[i] != ' ') continue;
        int wl = i - ws;
        if (wl + cur.n > 18) {
            memcpy(outc.c + outc.n, cur.c, cur.n * sizeof(uint32_t)); outc.n += cur.n; outc.c[outc.n++] = '\n';
            cur.n = 0; memcpy(cur.c, w.c + ws, wl * sizeof(uint32_t)); cur.n = wl;
        } else {
            cur.c[cur.n++] = ' '; memcpy(cur.c + cur.n, w.c + ws, wl * sizeof(uint32_t)); cur.n += wl;
        }
        ws = i + 1;
    }
    memcpy(outc.c + outc.n, cur.c, cur.n * sizeof(uint32_t)); outc.n += cur.n;
    int a = 0, b = outc.n;
    while (a < b && (outc.c[a] == ' ' || outc.c[a] == '\n')) a++;
    while (b > a && (outc.c[b - 1] == ' ' || outc.c[b - 1] == '\n')) b--;
    from_u32(outc.c + a, b - a, out, cap);
}

/* ---- the string table */
typedef struct { uint8_t *bytes; int n; } Entry;
static Entry g_entries[SMZ_TABLE_COUNT + 8];

static int table_index(const char *name)
{
    for (int i = 0; i < SMZ_TABLE_COUNT; i++) if (!strcmp(SMZ_TABLE[i].name, name)) return i;
    return -1;
}

static void table_set_bytes(int i, const uint8_t *b, int n)
{
    free(g_entries[i].bytes);
    g_entries[i].bytes = malloc(n ? n : 1);
    memcpy(g_entries[i].bytes, b, n);
    g_entries[i].n = n;
}

static int table_set_text(const char *name, const char *text)
{
    int i = table_index(name);
    if (i < 0) return -1;
    uint8_t buf[4096];
    int n = smz_dialog_compiled(text, buf, sizeof buf);
    if (n < 0) return -1;
    table_set_bytes(i, buf, n);
    return 0;
}

static int table_init(void)
{
    for (int i = 0; i < SMZ_TABLE_COUNT; i++) {
        if (SMZ_TABLE[i].bytes) table_set_bytes(i, SMZ_TABLE[i].bytes, SMZ_TABLE[i].nbytes);
        else {
            uint8_t buf[4096];
            int n = smz_dialog_compiled(SMZ_TABLE[i].text, buf, sizeof buf);
            if (n < 0) return -1;
            table_set_bytes(i, buf, n);
        }
    }
    static const char *const unwanted[] = { "bottle_vendor_choice", "bottle_vendor_get", "bottle_vendor_no", "zora_meeting", "zora_tells_cost",
        "zora_get_flippers", "zora_no_cash", "zora_no_buy_item", "fairy_fountain_refill", "pond_will_upgrade", "pond_item_test", "pond_item_bottle_filled" };
    for (size_t i = 0; i < sizeof unwanted / sizeof unwanted[0]; i++) if (table_set_text(unwanted[i], "{NOTEXT}")) return -1;
    return 0;
}

/* ---- the texts of a seed */
static unsigned g_pick;

static const char *pick(SmzStrList l)
{
    if (l.n == 0) return NULL;
    g_pick = g_pick * 1103515245u + 12345u;
    return l.v[(g_pick >> 16) % (unsigned)l.n];
}

/* string.Format with up to 3 arguments */
static void format(const char *tpl, char *out, size_t cap, const char *a0, const char *a1, const char *a2)
{
    size_t o = 0;
    const char *args[3] = { a0 ? a0 : "", a1 ? a1 : "", a2 ? a2 : "" };
    for (const char *p = tpl; *p && o + 1 < cap; p++) {
        if (p[0] == '{' && p[1] >= '0' && p[1] <= '2' && p[2] == '}') {
            const char *a = args[p[1] - '0'];
            while (*a && o + 1 < cap) out[o++] = *a++;
            p += 2;
        } else out[o++] = *p;
    }
    out[o] = 0;
}

static const char *region_name(int r)
{
    const char *n = pick(SMZ_REGION_TEXT[r].names);
    return n ? n : SMZ_REGION_TEXT[r].name;
}

static void safe_region_name(int r, char *out, size_t cap) { smz_game_safe_string(region_name(r), out, cap); }

typedef struct { SmzTextEmit emit; void *user; int err; } Out;

/* SetText with a fixed slot: writes the simple dialog and sets the table entry. */
static void set_slot(Out *o, uint32_t addr, const char *key, const char *tpl, int no_border, const char *a0, const char *a1, const char *a2)
{
    char f[1024], safe[1024], entry[1100];
    if (!tpl || !*tpl) tpl = "{NOTEXT}";
    format(tpl, f, sizeof f, a0, a1, a2);
    smz_game_safe_string(f, safe, sizeof safe);
    if (key) {
        snprintf(entry, sizeof entry, "%s%s", no_border ? "{NOBORDER}\n" : "", safe);
        if (table_set_text(key, entry)) o->err = 1;
    }
    if (addr) {
        uint8_t buf[300];
        int n = smz_dialog_simple(safe, buf, sizeof buf);
        o->emit(o->user, addr, buf, n);
    }
}

static void set_table_only(Out *o, const char *key, const char *tpl, int no_border, const char *a0, const char *a1, const char *a2)
{
    set_slot(o, 0, key, tpl, no_border, a0, a1, a2);
}

static void set_choice(Out *o, const char *key, const char *tpl, const char *item)
{
    char f[1024], safe[1024], txt[1200];
    format(tpl, f, sizeof f, item, NULL, NULL);
    smz_game_safe_string(f, safe, sizeof safe);
    size_t l = strlen(safe);
    while (l > 0 && (safe[l - 1] == ' ' || safe[l - 1] == '\n')) safe[--l] = 0;
    const char *yes = pick(SMZ_LINES[LINE_ChoiceYes]), *no = pick(SMZ_LINES[LINE_ChoiceNo]);
    if (!yes) yes = "Yes";
    if (!no) no = "No";
    char one[64];
    snprintf(one, sizeof one, "%s", strlen(yes) > 12 ? "Yeah" : yes);
    snprintf(txt, sizeof txt, "%s\n  \xE2\x89\xA5 %s\n    %s\n{CHOICE}", safe, one, strlen(yes) > 12 ? "No" : no);
    if (table_set_text(key, txt)) o->err = 1;
}

static const char *item_pedestal(int type)
{
    const char *p = pick(SMZ_ITEM_TEXT[type].pedestal);
    if (!p) p = pick(SMZ_ITEM_TEXT[type].name);
    return p ? p : "something";
}

static const char *item_articled(int type)
{
    const char *p = pick(SMZ_ITEM_TEXT[type].articled);
    return p ? p : "something";
}

static int find_item(const SmzWorld *w, int type)
{
    for (int i = 0; i < SMZ_LOC_COUNT; i++) if (w->item_at[SMZ_LOCS[i].id] == type) return SMZ_LOCS[i].id;
    return -1;
}

static int loc_region(int loc)
{
    for (int i = 0; i < SMZ_LOC_COUNT; i++) if (SMZ_LOCS[i].id == loc) return SMZ_LOCS[i].region;
    return 0;
}

static const char *item_text_name(int type)
{
    const char *p = pick(SMZ_ITEM_TEXT[type].articled);
    if (!p || strchr(p, '<')) p = pick(SMZ_ITEM_TEXT[type].name);
    return p ? p : "something";
}

static const char *region_text(int r) { return region_name(r); }

/* GameHintService.GetHintTileText; every pick is a separate statement so the order does not depend on the compiler. */
static void hint_text(const SmzSeed *seed, const SmzHint *h, char *out, size_t cap)
{
    char area[300], item[200];
    const char *tpl;
    SmzStrList list;
    if (h->type == HINT_Requirement) {
        const char *dungeon = region_text(h->region);
        snprintf(area, sizeof area, "%s", dungeon);
        snprintf(item, sizeof item, "%s", item_text_name(h->medallion));
        tpl = pick(SMZ_LINES[LINE_HintDungeonMedallion]);
        format(tpl, out, cap, area, item, NULL);
        return;
    }
    if (h->type == HINT_Location) {
        const char *rn = region_text(loc_region(h->loc));
        const char *ln = pick(SMZ_LOC_NAMES[h->loc]);
        snprintf(area, sizeof area, "%s - %s", rn, ln ? ln : "");
        snprintf(item, sizeof item, "%s", item_text_name(seed->world.item_at[h->loc]));
        tpl = pick(SMZ_LINES[LINE_HintLocationHasItem]);
        format(tpl, out, cap, area, item, NULL);
        return;
    }
    const char *a;
    if (h->type == HINT_Region || h->type == HINT_Dungeon) a = region_text(h->region);
    else if (h->type == HINT_Room && SMZ_HINT_LOCATIONS[h->def].names.n) a = pick(SMZ_HINT_LOCATIONS[h->def].names);
    else a = SMZ_HINT_LOCATIONS[h->def].key;
    snprintf(area, sizeof area, "%s", a);
    int line;
    switch (h->usefulness) {
    case USE_Mandatory: line = LINE_HintLocationIsMandatory; break;
    case USE_Key: line = LINE_HintLocationHasKey; break;
    case USE_NiceToHave: line = LINE_HintLocationHasUsefulItem; break;
    case USE_Sword: line = LINE_HintLocationHasSword; break;
    case USE_Goal: line = LINE_HintLocationAltGoal; break;
    default: line = LINE_HintLocationEmpty; break;
    }
    (void)list;
    tpl = pick(SMZ_LINES[line]);
    format(tpl, out, cap, area, NULL, NULL);
}

void smz_hint_text(const SmzSeed *seed, const SmzHint *hint, unsigned wording, char *out, size_t cap)
{
    unsigned saved = g_pick;
    g_pick = wording;
    hint_text(seed, hint, out, cap);
    g_pick = saved;
}

int smz_text_patches(const SmzSeed *seed, const SmzSettings *s, const SmzHints *hints, SmzTextEmit emit, void *user)
{
    return smz_text_patches_ex(seed, s, hints, NULL, emit, user);
}

/* the plando's text for a slot when it has one, else the generated one */
#define OV(k, def) ((ov && ov->v[k] && *ov->v[k]) ? ov->v[k] : (def))

int smz_text_patches_ex(const SmzSeed *seed, const SmzSettings *s, const SmzHints *hints, const SmzTextOverrides *ov, SmzTextEmit emit, void *user)
{
    const SmzWorld *w = &seed->world;
    Out o = { emit, user, 0 };
    g_pick = (unsigned)s->seed * 2654435761u + 7;
    if (table_init()) return -1;

    char region[256], r2[256], cr[64];
    int green = -1, red_first = -1, red_last = -1;
    for (int r = 0; r < REG_COUNT; r++) {
        if (!SMZ_REGIONS[r].has_reward) continue;
        if (w->reward_of[r] == REWARD_PendantGreen && green < 0) green = r;
        if (w->reward_of[r] == REWARD_CrystalRed) { if (red_first < 0) red_first = r; red_last = r; }
    }
    safe_region_name(green, region, sizeof region);
    set_slot(&o, 0x308A00, "sahasrahla_quest_information", OV(SMZ_TXT_SahasrahlaReveal, pick(SMZ_LINES[LINE_SahasrahlaReveal])), 0, region, NULL, NULL);
    safe_region_name(red_first, region, sizeof region);
    safe_region_name(red_last, r2, sizeof r2);
    set_slot(&o, 0x308E00, "bomb_shop", OV(SMZ_TXT_BombShopReveal, pick(SMZ_LINES[LINE_BombShopReveal])), 0, region, r2, NULL);
    set_slot(&o, 0x308800, "blind_by_the_light", OV(SMZ_TXT_BlindIntro, pick(SMZ_LINES[LINE_BlindIntro])), 0, NULL, NULL, NULL);
    set_slot(&o, 0x308C00, "kakariko_tavern_fisherman", OV(SMZ_TXT_TavernMan, pick(SMZ_LINES[LINE_TavernMan])), 0, NULL, NULL, NULL);

    /* Ganon */
    int intro_line = LINE_GanonIntro, alt_line = LINE_GanonIntroGoalsNotMet, sign_line = LINE_GanonGoalSign;
    if (s->game_mode == MODE_AllDungeons) { intro_line = LINE_AllDungeonsGanonIntro; alt_line = LINE_AllDungeonsGanonIntroGoalsNotMet; sign_line = LINE_AllDungeonsGanonGoalSign; }
    else if (s->game_mode == MODE_SpazerHunt) { intro_line = LINE_SpazerHuntGanonIntro; alt_line = LINE_SpazerHuntGanonIntroGoalsNotMet; sign_line = LINE_SpazerHuntGanonGoalSign; }
    set_slot(&o, 0x308600, "ganon_fall_in", OV(SMZ_TXT_GanonIntro, pick(SMZ_LINES[intro_line])), 0, NULL, NULL, NULL);
    set_slot(&o, 0x309100, "ganon_fall_in_alt", OV(SMZ_TXT_GanonIntroGoalsNotMet, pick(SMZ_LINES[alt_line])), 0, NULL, NULL, NULL);
    int silvers = find_item(w, ITEM_SilverArrows);
    const char *silvers_tpl = OV(SMZ_TXT_GanonSilversHint, pick(SMZ_LINES[silvers < 0 ? LINE_GanonNoSilvers : LINE_GanonSilversHint]));
    const char *area = silvers < 0 ? "" : SMZ_REGION_TEXT[loc_region(silvers)].area;
    set_slot(&o, 0x308700, "ganon_phase_3", silvers_tpl, 0, "you", area, NULL);
    set_slot(&o, 0x309200, "ganon_phase_3_alt", silvers_tpl, 0, "you", area, NULL);
    static const char *const phase3[] = { "ganon_phase_3_silvers", "ganon_phase_3_no_bow", "ganon_phase_3_no_silvers", "ganon_phase_3_no_silvers_alt" };
    for (int i = 0; i < 4; i++) set_table_only(&o, phase3[i], silvers_tpl, 0, "you", area, NULL);

    /* merchants */
    /* the picks draw from one generator, so their order must not depend on how the compiler orders arguments */
    const char *bottle_line = pick(SMZ_LINES[LINE_BottleMerchant]);
    set_choice(&o, "bottle_vendor_choice", OV(SMZ_TXT_BottleMerchant, bottle_line), item_articled(w->item_at[LOC_BottleMerchant]));
    const char *zora_line = pick(SMZ_LINES[LINE_KingZora]);
    set_choice(&o, "zora_tells_cost", OV(SMZ_TXT_KingZora, zora_line), item_articled(w->item_at[LOC_KingZora]));

    /* pedestals */
    set_slot(&o, 0x308300, "mastersword_pedestal_translated", OV(SMZ_TXT_MasterSwordPedestal, item_pedestal(w->item_at[LOC_MasterSwordPedestal])), 0, NULL, NULL, NULL);
    set_slot(&o, 0x308F00, "tablet_ether_book", OV(SMZ_TXT_EtherTablet, item_pedestal(w->item_at[LOC_EtherTablet])), 0, NULL, NULL, NULL);
    set_slot(&o, 0x309000, "tablet_bombos_book", OV(SMZ_TXT_BombosTablet, item_pedestal(w->item_at[LOC_BombosTablet])), 0, NULL, NULL, NULL);
    set_slot(&o, 0x308400, "end_triforce", OV(SMZ_TXT_TriforceRoom, pick(SMZ_LINES[LINE_TriforceRoom])), 1, NULL, NULL, NULL);

    /* goal signs */
    snprintf(cr, sizeof cr, s->gt_crystals == 1 ? "1 crystal" : "%d crystals", s->gt_crystals);
    int gtkey = find_item(w, ITEM_BigKeyGT);
    set_table_only(&o, "sign_ganons_tower", OV(SMZ_TXT_GanonsTowerGoalSign, pick(SMZ_LINES[LINE_GanonsTowerGoalSign])), 0, cr, "You", gtkey < 0 ? "unknown location" : SMZ_REGION_TEXT[loc_region(gtkey)].area);
    char gc[64], gp[64], goal[256];
    snprintf(gc, sizeof gc, s->ganon_crystals == 1 ? "1 crystal" : "%d crystals", s->ganon_crystals);
    if (s->game_mode == MODE_AllDungeons) snprintf(goal, sizeof goal, "%s", pick(SMZ_LINES[sign_line]));
    else if (s->game_mode == MODE_SpazerHunt) {
        snprintf(gc, sizeof gc, "%d", s->spazers_required);
        snprintf(gp, sizeof gp, "%d", s->spazers_in_pool);
        format(pick(SMZ_LINES[sign_line]), goal, sizeof goal, gc, gp, NULL);
    } else format(pick(SMZ_LINES[sign_line]), goal, sizeof goal, gc, NULL, NULL);
    set_table_only(&o, "sign_ganon", OV(SMZ_TXT_GanonGoalSign, goal), 0, NULL, NULL, NULL);

    int tile_done[15] = { 0 };
    if (ov) {
        for (int t = 0; t < 15; t++) {
            const char *txt = ov->v[SMZ_TXT_HintTile0 + t];
            if (!txt || !*txt) continue;
            char safe[512], entry[560];
            smz_game_safe_string(txt, safe, sizeof safe);
            snprintf(entry, sizeof entry, "{NOBORDER}\n%s", safe);   /* SetHintText, like the generated hint tiles */
            if (table_set_text(SMZ_HINT_TILES[t], entry)) o.err = 1;
            tile_done[t] = 1;
        }
    }
    if (hints && hints->n) {
        for (int i = 0; i < hints->n; i++) {
            if (hints->h[i].tile >= 0 && hints->h[i].tile < 15 && tile_done[hints->h[i].tile]) continue;
            char txt[512];
            hint_text(seed, &hints->h[i], txt, sizeof txt);
            set_table_only(&o, SMZ_HINT_TILES[hints->h[i].tile], txt, 1, NULL, NULL, NULL);
        }
    }

    /* the table */
    static uint8_t table[SMZ_TEXT_TABLE_SIZE];
    int n = 0;
    for (int i = 0; i < SMZ_TABLE_COUNT; i++) {
        if (n + g_entries[i].n > SMZ_TEXT_TABLE_SIZE) return -2;
        memcpy(table + n, g_entries[i].bytes, g_entries[i].n);
        n += g_entries[i].n;
    }
    memset(table + n, 0xFF, SMZ_TEXT_TABLE_SIZE - n);
    if (getenv("SMZ_TEXT_DEBUG")) { int at = 0; for (int i = 0; i < SMZ_TABLE_COUNT; i++) { fprintf(stderr, "E %d %s %d %d\n", i, SMZ_TABLE[i].name, at, g_entries[i].n); at += g_entries[i].n; } }
    emit(user, 0x1C8000, table, SMZ_TEXT_TABLE_SIZE);
    return o.err ? -3 : 0;
}

void smz_help_texts(const SmzSeed *seed, const SmzPatchCfg *cfg, char out[SMZ_HELP_COUNT][SMZ_HELP_TEXT])
{
    static SmzHints hints;
    int32_t hs = smz_hint_seed(seed);
    smz_hints_generate(seed, cfg, hs, SMZ_HELP_COUNT, &hints);
    for (int i = 0; i < SMZ_HELP_COUNT; i++) {
        char raw[512];
        if (i < hints.n) {
            smz_hint_text(seed, &hints.h[i], (unsigned)hs * 2654435761u + (unsigned)i * 40503u + 7, raw, sizeof raw);
            snprintf(out[i], SMZ_HELP_TEXT, "%s", raw);
        } else out[i][0] = 0;
    }
}
