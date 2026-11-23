#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_gfx.h"
#include "pc_help.h"
#include "pc_welcome.h"
#include "smz3_md5.h"
#include "smz3_romsetup.h"

enum { EMPTY, VALID, WRONG };
typedef struct { int state; char name[128], md5[33], msg[96]; Uint32 since; } Card;
typedef struct { float x, y, vx, vy, life, max; SDL_Color c; } Spark;

#define SPARKS 220
static WelcomeHooks hk;
static int active, focus;   /* focus 0 browse, 1 play */
static Card card[2];
static Spark spark[SPARKS];
static Uint32 opened, ready_since, closing_since, fade_start;
static int was_ready;
static char last_reject[160];

static const char *const TITLE[2] = {"SUPER METROID", "A LINK TO THE PAST"};
static const char *const NEEDS[2] = {"Super Metroid (JU)", "ALttP, Japan v1.0"};
static const char *const WANT_MD5[2] = {SMZ_MD5_SM, SMZ_MD5_Z3};

void welcome_init(const WelcomeHooks *hooks) { hk = *hooks; }
int welcome_active(void) { return active; }

static float since(Uint32 t) { return (SDL_GetTicks() - t) / 1000.0f; }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }

static void burst(float cx, float cy, int n, SDL_Color base)
{
    for (int i = 0, made = 0; i < SPARKS && made < n; i++) {
        if (spark[i].life > 0) continue;
        float a = (float)(rand() % 6283) / 1000.0f, sp = 30 + rand() % 110;
        spark[i] = (Spark){cx, cy, cosf(a) * sp, sinf(a) * sp - 20, 0, 0.7f + (rand() % 70) / 100.0f, base};
        spark[i].life = spark[i].max;
        made++;
    }
}

static void load_existing(void)
{
    char names[2][128] = {{0}}, md5[2][33] = {{0}};
    hk.existing(names, md5);
    for (int i = 0; i < 2; i++) {
        if (!names[i][0]) { card[i] = (Card){EMPTY}; continue; }
        card[i].state = VALID;
        snprintf(card[i].name, sizeof card[i].name, "%s", names[i]);
        snprintf(card[i].md5, sizeof card[i].md5, "%s", md5[i]);
        card[i].since = SDL_GetTicks() - 2000;
    }
}

void welcome_open(void)
{
    memset(card, 0, sizeof card);
    memset(spark, 0, sizeof spark);
    last_reject[0] = 0;
    active = 1;
    focus = 0;
    opened = SDL_GetTicks();
    closing_since = 0;
    if (hk.ready()) { load_existing(); focus = 1; was_ready = 1; ready_since = opened - 3000; }
    else was_ready = 0;
}

void welcome_report(const char *source, int kind, const char *md5, int kept)
{
    int which = kind == SMZ_ROM_SM || kind == SMZ_ROM_SM_OTHER ? 0 : kind == SMZ_ROM_Z3 || kind == SMZ_ROM_Z3_OTHER ? 1 : -1;
    if (which < 0) {
        snprintf(last_reject, sizeof last_reject, "%s: not Super Metroid or ALttP (md5 %.8s...)", source, md5);
        return;
    }
    if (card[which].state == VALID && !kept) return;   /* a wrong file never replaces a good one */
    card[which].state = kept ? VALID : WRONG;
    snprintf(card[which].name, sizeof card[which].name, "%s", source);
    snprintf(card[which].md5, sizeof card[which].md5, "%s", md5);
    snprintf(card[which].msg, sizeof card[which].msg, "%s", kept ? "" : "Right game, wrong version");
    card[which].since = SDL_GetTicks();
    if (kept) last_reject[0] = 0;
}

void welcome_added(void)
{
    int now = hk.ready();
    for (int i = 0; i < 2; i++)
        if (card[i].state == VALID && since(card[i].since) < 0.2f) burst(i ? 236 : 84, 112, 26, (SDL_Color){0x4C, 0xC3, 0x8A, 255});
    if (now && !was_ready) {
        ready_since = SDL_GetTicks();
        focus = 1;
        burst(160, 204, 90, (SDL_Color){0xE9, 0xA2, 0x3B, 255});
        burst(84, 112, 40, (SDL_Color){0x4F, 0xC3, 0xF7, 255});
        burst(236, 112, 40, (SDL_Color){0x4F, 0xC3, 0xF7, 255});
    }
    was_ready = now;
}

static void close_screen(void) { closing_since = SDL_GetTicks(); }

void welcome_nav(UiButton b)
{
    if (!active || closing_since) return;
    int ready = hk.ready();
    if (b == BTN_LEFT) focus = 0;
    else if (b == BTN_RIGHT && ready) focus = 1;
    else if (b == BTN_A) { if (focus == 1 && ready) close_screen(); else hk.browse(); }
    else if (b == BTN_B && ready) close_screen();
}

void welcome_click(int x, int y)
{
    if (!active || closing_since) return;
    if (y >= 186 && y < 216) {
        if (x >= 16 && x < 86) { focus = 0; hk.browse(); }
        else if (x >= 93 && x < 304 && hk.ready()) { focus = 1; close_screen(); }
    } else if (y >= 66 && y < 178) hk.browse();
}

static SDL_Color rgba(int r, int g, int b, float a) { return (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)(255 * clamp01(a))}; }

static void rect(float k, float ox, float oy, float x, float y, float w, float h, SDL_Color c) { gfx_rect(ox + x * k, oy + y * k, w * k, h * k, c); }
static void text(float k, float ox, float oy, float x, float y, float size, SDL_Color c, int align, const char *s) { gfx_text(ox + x * k, oy + y * k, size * k, c, align, s); }

static void border(float k, float ox, float oy, float x, float y, float w, float h, float t, SDL_Color c)
{
    rect(k, ox, oy, x, y, w, t, c); rect(k, ox, oy, x, y + h - t, w, t, c);
    rect(k, ox, oy, x, y, t, h, c); rect(k, ox, oy, x + w - t, y, t, h, c);
}

static void short_md5(const char *m, char out[24]) { snprintf(out, 24, "%.8s...%s", m, m + 28); }

static SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
    return (SDL_Color){(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t), (Uint8)(a.a + (b.a - a.a) * t)};
}

/* A box with its four corners cut off, filled in 1-unit rows. */
static void chamfer(float k, float ox, float oy, float x, float y, float w, float h, float cut, SDL_Color c)
{
    for (int i = 0; i < (int)cut; i++) {
        float in = cut - i;
        rect(k, ox, oy, x + in, y + i, w - 2 * in, 1, c);
        rect(k, ox, oy, x + in, y + h - 1 - i, w - 2 * in, 1, c);
    }
    rect(k, ox, oy, x, y + cut, w, h - 2 * cut, c);
}

static void chamfer_box(float k, float ox, float oy, float x, float y, float w, float h, float cut, float t, SDL_Color edge, SDL_Color fill)
{
    chamfer(k, ox, oy, x, y, w, h, cut, edge);
    chamfer(k, ox, oy, x + t, y + t, w - 2 * t, h - 2 * t, cut > t ? cut - t : 0, fill);
}

static void hgrad(float k, float ox, float oy, float x, float y, float w, float h, float top_cut, SDL_Color a, SDL_Color b)
{
    float c = top_cut, tl = c / w;
    SDL_Color m1 = mix(a, b, tl), m2 = mix(a, b, 1 - tl);
    float top[8] = {ox + (x + c) * k, oy + y * k, ox + (x + w - c) * k, oy + y * k, ox + (x + w) * k, oy + (y + c) * k, ox + x * k, oy + (y + c) * k};
    SDL_Color tc[4] = {m1, m2, b, a};
    gfx_quad(top, tc);
    float bot[8] = {ox + x * k, oy + (y + c) * k, ox + (x + w) * k, oy + (y + c) * k, ox + (x + w) * k, oy + (y + h) * k, ox + x * k, oy + (y + h) * k};
    SDL_Color bc[4] = {a, b, b, a};
    gfx_quad(bot, bc);
}

static void disc(float k, float ox, float oy, float cx, float cy, float r, SDL_Color c)
{
    for (int i = -(int)r; i <= (int)r; i++) {
        float half = sqrtf(r * r - (float)i * i);
        rect(k, ox, oy, cx - half, cy + i, half * 2, 1, c);
    }
}

static void backdrop(float k, float ox, float oy, float t, float fade)
{
    rect(k, ox, oy, 0, 0, 320, 240, rgba(0x0A, 0x0E, 0x15, 1));
    {
        float top[8] = {ox, oy, ox + 320 * k, oy, ox + 320 * k, oy + 90 * k, ox, oy + 90 * k};
        SDL_Color tc[4] = {rgba(0x2A, 0x21, 0x12, 0.9f * fade), rgba(0x2A, 0x21, 0x12, 0.9f * fade), rgba(0x2A, 0x21, 0x12, 0), rgba(0x2A, 0x21, 0x12, 0)};
        gfx_quad(top, tc);
        float bot[8] = {ox, oy + 170 * k, ox + 320 * k, oy + 170 * k, ox + 320 * k, oy + 240 * k, ox, oy + 240 * k};
        SDL_Color bc[4] = {rgba(0x0F, 0x2A, 0x2C, 0), rgba(0x0F, 0x2A, 0x2C, 0), rgba(0x0F, 0x2A, 0x2C, 0.8f * fade), rgba(0x0F, 0x2A, 0x2C, 0.8f * fade)};
        gfx_quad(bot, bc);
    }
    for (int i = 0; i <= 20; i++) {
        float d = fabsf(i * 16 - 160) / 160.0f, a = 0.05f * (1 - d * d) * fade;
        if (a > 0) rect(k, ox, oy, i * 16, 0, 1, 240, rgba(0x4F, 0xC3, 0xF7, a));
    }
    for (int j = 0; j <= 15; j++) {
        float d = fabsf(j * 16 - 108) / 130.0f, a = 0.05f * (1 - d * d) * fade;
        if (a > 0) rect(k, ox, oy, 0, j * 16, 320, 1, rgba(0x4F, 0xC3, 0xF7, a));
    }
    for (int i = 0; i < 14; i++) {   /* a few slow motes */
        float sx = fmodf(i * 53.7f, 320), sy = fmodf(240 - fmodf(i * 37.1f + t * (3 + i % 4) * 2, 240), 240);
        rect(k, ox, oy, sx, sy, 1.2f, 1.2f, rgba(0xB8, 0xC6, 0xE0, (0.2f + 0.2f * sinf(t * 1.5f + i)) * fade));
    }
}

void welcome_draw(float k, float ox, float oy)
{
    if (!active) return;
    float t = since(opened), fade_in = ease(t / 0.5f), out = closing_since ? clamp01(since(closing_since) / 0.45f) : 0;
    if (out >= 1) { active = 0; fx_screen_changed(); return; }
    int ready = hk.ready();
    backdrop(k, ox, oy, t, fade_in);
    {   /* Samus on the left, Zelda on the right, split by a slanted seam */
        float lit = 0.5f + 0.5f * sinf(t * 2.2f), s0 = 187, s1 = 133;
        float L[8] = {ox, oy, ox + s0 * k, oy, ox + s1 * k, oy + 240 * k, ox, oy + 240 * k};
        SDL_Color lc[4] = {rgba(0x4A, 0x1C, 0x10, 0.28f * fade_in), rgba(0x4A, 0x1C, 0x10, 0.28f * fade_in), rgba(0x4A, 0x1C, 0x10, 0.8f * fade_in), rgba(0x4A, 0x1C, 0x10, 0.8f * fade_in)};
        gfx_quad(L, lc);
        float R_[8] = {ox + s0 * k, oy, ox + 320 * k, oy, ox + 320 * k, oy + 240 * k, ox + s1 * k, oy + 240 * k};
        SDL_Color rc[4] = {rgba(0x0F, 0x3A, 0x24, 0.28f * fade_in), rgba(0x0F, 0x3A, 0x24, 0.28f * fade_in), rgba(0x0F, 0x3A, 0x24, 0.8f * fade_in), rgba(0x0F, 0x3A, 0x24, 0.8f * fade_in)};
        gfx_quad(R_, rc);
        float gl[8] = {ox + (s0 - 5) * k, oy, ox + (s0 + 5) * k, oy, ox + (s1 + 5) * k, oy + 240 * k, ox + (s1 - 5) * k, oy + 240 * k};
        SDL_Color gc = rgba(0xF2, 0xA3, 0x3A, 0.14f * fade_in * lit);
        SDL_Color gcs[4] = {gc, gc, gc, gc};
        gfx_quad(gl, gcs);
        float sm[8] = {ox + (s0 - 0.5f) * k, oy, ox + (s0 + 0.5f) * k, oy, ox + (s1 + 0.5f) * k, oy + 240 * k, ox + (s1 - 0.5f) * k, oy + 240 * k};
        SDL_Color sc = rgba(0xFF, 0xE7, 0xA8, 0.55f * fade_in + 0.3f * lit);
        SDL_Color scs[4] = {sc, sc, sc, sc};
        gfx_quad(sm, scs);
    }
    float glow = ready ? 0.5f + 0.5f * sinf(since(ready_since) * 3.2f) : 0;
    float slide = (1 - fade_in) * 14;
    float lx = 92, ly = 8 - slide;
    float breathe = 0.5f + 0.5f * sinf(t * 1.6f);
    for (int q = 4; q >= 1; q--) disc(k, ox, oy, lx + 27, ly + 27, 27 + q * 3, rgba(0x3F, 0xD0, 0x8B, (0.03f + 0.03f * breathe) * fade_in / q));
    gfx_logo(ox + lx * k, oy + ly * k, 54 * k, fade_in);
    text(k, ox, oy, lx + 64 + 1.5f, ly + 6 + 1.5f, 24, rgba(0x8A, 0x5A, 0x14, fade_in), 0, "SMZ3");
    text(k, ox, oy, lx + 64, ly + 6, 24, rgba(0xF2, 0xA3, 0x3A, fade_in), 0, "SMZ3");
    text(k, ox, oy, lx + 65, ly + 42, 8, ready ? rgba(0x3F, 0xD0, 0x8B, fade_in) : rgba(0x87, 0x93, 0xA6, fade_in), 0, ready ? "Both ROMs ready" : "Add your two ROMs");

    static const SDL_Color HEAD_A[2] = {{0xF2, 0xA3, 0x3A, 255}, {0x3F, 0xD0, 0x8B, 255}};
    static const SDL_Color HEAD_B[2] = {{0xD4, 0x56, 0x2A, 255}, {0xC9, 0xB2, 0x3A, 255}};
    static const SDL_Color HEAD_TX[2] = {{0x1A, 0x0F, 0x05, 255}, {0x07, 0x18, 0x0F, 255}};
    for (int i = 0; i < 2; i++) {
        float x = i ? 166.5f : 16, y = 68, w = 137.5f, h = 108, cut = 4;
        float appear = ease((t - 0.15f - 0.12f * i) / 0.45f), dy = (1 - appear) * 24;
        float st = since(card[i].since), pop = card[i].state ? ease(st / 0.35f) : 0;
        int s_ = card[i].state;
        SDL_Color ok = rgba(0x3F, 0xD0, 0x8B, 1), bad = rgba(0xE0, 0x5A, 0x4F, 1), accent = s_ == VALID ? ok : bad;
        float yy = y + dy;
        if (s_ == VALID || s_ == WRONG) {   /* outer glow */
            float g = (0.10f + 0.05f * sinf(t * 2.2f + i) + (1 - ease(st / 0.9f)) * 0.3f) * appear;
            for (int q = 5; q >= 1; q--) chamfer(k, ox, oy, x - q * 1.4f, yy - q * 1.4f, w + q * 2.8f, h + q * 2.8f, cut + q, rgba(accent.r, accent.g, accent.b, g / q));
        }
        SDL_Color fill = s_ == VALID ? rgba(0x16, 0x21, 0x1F, appear) : s_ == WRONG ? rgba(0x25, 0x16, 0x1A, appear) : rgba(0x11, 0x18, 0x23, appear);
        SDL_Color line = s_ ? rgba(accent.r, accent.g, accent.b, appear) : rgba(0x25, 0x32, 0x4A, appear);
        chamfer_box(k, ox, oy, x, yy, w, h, cut, 1.2f, line, fill);
        if (s_ == EMPTY) {   /* dashed, breathing outline */
            float a = (0.35f + 0.65f * (0.5f + 0.5f * sinf(t * 2.6f + i * 1.7f))) * appear;
            for (float d = 6; d < w - 6; d += 9) {
                rect(k, ox, oy, x + d, yy + 1, 5, 1.2f, rgba(0x4F, 0xC3, 0xF7, a));
                rect(k, ox, oy, x + d, yy + h - 2.2f, 5, 1.2f, rgba(0x4F, 0xC3, 0xF7, a));
            }
            for (float d = 6; d < h - 6; d += 9) {
                rect(k, ox, oy, x + 1, yy + d, 1.2f, 5, rgba(0x4F, 0xC3, 0xF7, a));
                rect(k, ox, oy, x + w - 2.2f, yy + d, 1.2f, 5, rgba(0x4F, 0xC3, 0xF7, a));
            }
        }
        SDL_Color ha = s_ == WRONG ? rgba(0xE0, 0x5A, 0x4F, 1) : s_ ? HEAD_A[i] : rgba(0x1F, 0x2C, 0x3F, 1);
        SDL_Color hb = s_ == WRONG ? rgba(0x8F, 0x2F, 0x2A, 1) : s_ ? HEAD_B[i] : rgba(0x17, 0x20, 0x2E, 1);
        ha.a = hb.a = (Uint8)(255 * appear);
        hgrad(k, ox, oy, x + 1, yy + 1, w - 2, 14, cut - 1, ha, hb);
        SDL_Color htx = s_ == WRONG ? rgba(0x1A, 0x05, 0x05, appear) : s_ ? rgba(HEAD_TX[i].r, HEAD_TX[i].g, HEAD_TX[i].b, appear) : rgba(0xC7, 0xD0, 0xDE, appear);
        text(k, ox, oy, x + 8, yy + 4, 8, htx, 0, TITLE[i]);
        char pg[8];
        snprintf(pg, sizeof pg, "%d / 2", i + 1);
        text(k, ox, oy, x + w - 8, yy + 5, 6, htx, 2, pg);
        char line_[64], m[24];
        text(k, ox, oy, x + 8, yy + 22, 8, rgba(0xE8, 0xEC, 0xF2, appear), 0, NEEDS[i]);
        short_md5(WANT_MD5[i], m);
        snprintf(line_, sizeof line_, "md5 %s", m);
        text(k, ox, oy, x + 8, yy + 33, 7, rgba(0x6F, 0x7D, 0x93, appear), 0, line_);
        float cx = x + w / 2, cy = yy + 64;
        if (s_ == EMPTY) {
            float pulse = 0.6f + 0.4f * sinf(t * 2.6f + i);
            SDL_Color bl = rgba(0x4F, 0xC3, 0xF7, appear * pulse);
            rect(k, ox, oy, cx - 0.7f, cy - 12, 1.4f, 14, bl);   /* a down arrow into a tray */
            for (int p = 0; p < 6; p++) rect(k, ox, oy, cx - 6 + p, cy - 3 + p, 1.4f, 1.4f, bl), rect(k, ox, oy, cx + 6 - p, cy - 3 + p, 1.4f, 1.4f, bl);
            rect(k, ox, oy, cx - 10, cy + 6, 20, 1.4f, bl);
            text(k, ox, oy, cx, cy + 12, 9, bl, 1, "Drop it here");
        } else {
            float s = 0.4f + 0.6f * pop;
            disc(k, ox, oy, cx, cy, 12 * s, rgba(accent.r, accent.g, accent.b, pop * 0.22f));
            disc(k, ox, oy, cx, cy, 11 * s, rgba(accent.r, accent.g, accent.b, pop * 0.9f));
            disc(k, ox, oy, cx, cy, 9.6f * s, rgba(0x14, 0x23, 0x22, pop));
            if (s_ == VALID) {
                SDL_Color c = rgba(0x3F, 0xD0, 0x8B, pop);
                for (int p = 0; p < 5; p++) rect(k, ox, oy, cx - 6 * s + p * 1.2f * s, cy + p * 1.2f * s - 0.5f, 2.6f * s, 2.6f * s, c);
                for (int p = 0; p < 9; p++) rect(k, ox, oy, cx - 0.5f * s + p * 1.3f * s, cy + 5 * s - p * 1.3f * s, 2.6f * s, 2.6f * s, c);
            } else text(k, ox, oy, cx, cy - 7 * s, 15 * s, rgba(0xE0, 0x5A, 0x4F, pop), 1, "!");
            rect(k, ox, oy, x + 8, yy + h - 28, w - 16, 1, rgba(accent.r, accent.g, accent.b, pop * 0.4f));
            char nm[40];
            snprintf(nm, sizeof nm, "%.26s", card[i].name);
            text(k, ox, oy, x + 8, yy + h - 25, 8, rgba(0xE8, 0xEC, 0xF2, pop), 0, nm);
            text(k, ox, oy, x + 8, yy + h - 14, 7, rgba(accent.r, accent.g, accent.b, pop), 0, s_ == VALID ? "Checksum matches, saved" : card[i].msg);
        }
    }

    if (last_reject[0]) text(k, ox, oy, 160, 179, 8, rgba(0xE0, 0x5A, 0x4F, fade_in), 1, last_reject);

    float by = 188, bh = 26;
    if (ready) {
        float lift = 1 + 0.02f * glow, w = 211 * lift, x = 198.5f - w / 2, pulse = 0.22f + 0.22f * glow;
        for (int g = 5; g >= 1; g--) rect(k, ox, oy, x - g * 2.2f, by - g * 2.2f, w + g * 4.4f, bh + g * 4.4f, rgba(0xF2, 0xA3, 0x3A, pulse / g * 0.55f));
        rect(k, ox, oy, x, by, w, bh / 2, mix(rgba(0xF7, 0xB9, 0x55, 1), rgba(0xE9, 0xA2, 0x3B, 1), 1));
        rect(k, ox, oy, x, by + bh / 2, w, bh / 2, rgba(0xC9, 0x80, 0x2A, 1));
        rect(k, ox, oy, x, by, w, 1, rgba(0xFF, 0xD9, 0x8A, 1));
        float sheen = fmodf(since(ready_since) * 0.7f, 1.8f) - 0.3f;   /* a light sweeps across */
        if (sheen > 0 && sheen < 1) rect(k, ox, oy, x + sheen * w - 7, by, 14, bh, rgba(0xFF, 0xF3, 0xD0, 0.5f));
        if (focus == 1) border(k, ox, oy, x - 1.5f, by - 1.5f, w + 3, bh + 3, 1.4f, rgba(0xFF, 0xF3, 0xD0, 0.8f + 0.2f * glow));
        text(k, ox, oy, 198.5f, by + 4, 15, rgba(0x2A, 0x1A, 0x08, 1), 1, "> PLAY!");
        for (int i = 0; i < 5; i++) {   /* sparks rising from the panel */
            float ph = fmodf(since(ready_since) * 0.45f + i * 0.21f, 1);
            rect(k, ox, oy, 115 + i * 44 + sinf(i * 3.1f) * 6, by - 2 - ph * 34, 1.8f, 1.8f,
                 rgba(i % 3 == 0 ? 0xFF : i % 3 == 1 ? 0x4F : 0x3F, i % 3 == 0 ? 0xD9 : i % 3 == 1 ? 0xC3 : 0xD0, i % 3 == 0 ? 0x8A : i % 3 == 1 ? 0xF7 : 0x8B, (1 - ph) * ph * 4 > 1 ? 1 : (1 - ph) * ph * 4));
        }
    } else {
        chamfer_box(k, ox, oy, 93, by, 211, bh, 0, 1, rgba(0x25, 0x32, 0x4A, fade_in), rgba(0x10, 0x16, 0x1F, fade_in));
        text(k, ox, oy, 198.5f, by + 8, 9, rgba(0x5A, 0x66, 0x78, fade_in), 1, "ADD BOTH ROMS");
    }
    chamfer_box(k, ox, oy, 16, by, 70, bh, 4, focus == 0 ? 2 : 1, rgba(0x4F, 0xC3, 0xF7, fade_in * (focus == 0 ? 1 : 0.55f)), rgba(0x1D, 0x2A, 0x3F, fade_in));
    text(k, ox, oy, 51, by + 7, 10, rgba(0xE8, 0xEC, 0xF2, fade_in), 1, "Browse");
    {
        static const HelpItem NOT_READY[] = {{"A", "Enter", "Browse"}}, READY[] = {{"A", "Enter", "Choose"}, {"LR", "<", "Switch"}};
        if (ready) help_bar(ox, oy + 224 * k, 320 * k, 16 * k, READY, 2); else help_bar(ox, oy + 224 * k, 320 * k, 16 * k, NOT_READY, 1);
    }

    /* sparkles */
    static Uint32 last;
    Uint32 now = SDL_GetTicks();
    float dt = last ? (now - last) / 1000.0f : 0;
    last = now;
    for (int i = 0; i < SPARKS; i++) {
        Spark *s = &spark[i];
        if (s->life <= 0) continue;
        s->life -= dt; s->x += s->vx * dt; s->y += s->vy * dt; s->vy += 90 * dt;
        float a = clamp01(s->life / s->max);
        rect(k, ox, oy, s->x, s->y, 2.2f, 2.2f, rgba(s->c.r, s->c.g, s->c.b, a));
    }
    if (out > 0) rect(k, ox, oy, 0, 0, 320, 240, rgba(0x0A, 0x0E, 0x15, out));
}

void fx_screen_changed(void) { fade_start = SDL_GetTicks(); }

#define FX_HOLD 0.6f   /* the new screen stays black this long before the seam opens it */
#define FX_OPEN 0.5f

int fx_busy(void) { return fade_start && since(fade_start) < FX_HOLD + FX_OPEN * 0.6f; }

void fx_draw(int win_w, int win_h)
{
    float t = (since(fade_start) - FX_HOLD) / FX_OPEN;
    if (t >= 1) return;
    int holding = t <= 0;   /* still covered: no seam yet */
    if (t < 0) t = 0;
    float p = ease(t), w = (float)win_w, h = (float)win_h, band = h / 4, slope = 0.22f;
    for (float y = 0; y < h; y += 2) {
        int b = (int)(y / band);   /* the four bands lock in one after another */
        float edge = p * (w + h * slope + 0.18f * w * 3) - 0.18f * w * b - y * slope;
        float cover = edge < 0 ? 0 : edge;
        if (cover < w) gfx_rect(cover, y, w - cover, 2, rgba(0x0A, 0x0E, 0x15, 1));
        if (!holding && edge > -8 && edge < w) {
            gfx_rect(edge - 5, y, 14, 2, rgba(0xF2, 0xA3, 0x3A, 0.22f * (1 - t)));
            gfx_rect(edge, y, 3, 2, rgba(0xFF, 0xE7, 0xA8, 1 - t * 0.6f));
        }
    }
}
