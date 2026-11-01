#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_font.h"
#include "pc_gfx.h"
#include "pc_help.h"
#include "smz3_preview.h"

static SDL_Renderer *R;
/* Each window has its own renderer and SDL textures belong to one renderer: every cache below has one copy per window. */
static SDL_Renderer *slot_ren[2];
static int S;

static const SDL_Color PALETTE[] = {
    {0x10, 0x14, 0x1C, 255}, {0x1D, 0x24, 0x30, 255}, {0x2E, 0x4A, 0x6B, 255}, {0xE8, 0xEC, 0xF2, 255}, {0x87, 0x93, 0xA6, 255},
    {0xE9, 0xA2, 0x3B, 255}, {0xE0, 0x5A, 0x4F, 255}, {0x4C, 0xC3, 0x8A, 255}, {0x4F, 0xC3, 0xF7, 255}, {0x14, 0x50, 0x4A, 255},
    {0x1B, 0x2F, 0x6B, 255}, {0x4B, 0x2A, 0x7A, 255}, {0x8A, 0x64, 0x14, 255},
    /* the rupeergy: white, light, mid, shadow, rim, then the second blink frame's light, mid, shadow, rim */
    {0xFF, 0xF1, 0xF8, 255}, {0xFF, 0x9A, 0xD0, 255}, {0xE0, 0x55, 0x9B, 255}, {0x8E, 0x2E, 0x68, 255}, {0x5A, 0x2A, 0x86, 255},
    {0xFF, 0xC2, 0xE4, 255}, {0xF0, 0x7A, 0xB6, 255}, {0xA8, 0x40, 0x7E, 255}, {0x7A, 0x3A, 0xAE, 255},
};

SDL_Color gfx_ui_color(int i) { return PALETTE[i < 0 || i >= UI_COLORS ? 0 : i]; }

typedef struct { SDL_Texture *tex; int cw, ch, cols, size; const short *adv; } Atlas;
static Atlas atlas_s[2][2];
#define atlas atlas_s[S]

static void make_atlas(Atlas *a, const unsigned char *alpha, const short *adv, int w, int h, int cw, int ch, int cols, int size)
{
    uint32_t *px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) px[i] = 0x00FFFFFFu | ((uint32_t)alpha[i] << 24);
    a->tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(a->tex, NULL, px, w * 4);
    SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(a->tex, SDL_ScaleModeBest);
    a->cw = cw; a->ch = ch; a->cols = cols; a->size = size; a->adv = adv;
    free(px);
}

static void make_atlases(void)
{
    make_atlas(&atlas[0], PC_FONT_ALPHA, PC_FONT_ADV, PC_FONT_W, PC_FONT_H, PC_FONT_CW, PC_FONT_CH, PC_FONT_COLS, PC_FONT_SIZE);
    make_atlas(&atlas[1], PC_FONT2_ALPHA, PC_FONT2_ADV, PC_FONT2_W, PC_FONT2_H, PC_FONT2_CW, PC_FONT2_CH, PC_FONT2_COLS, PC_FONT2_SIZE);
}

void gfx_init(SDL_Renderer *r)
{
    R = slot_ren[0] = r;
    S = 0;
    make_atlases();
}

void gfx_use(SDL_Renderer *r)
{
    if (r == slot_ren[0]) S = 0;
    else {
        S = 1;
        if (slot_ren[1] != r) { slot_ren[1] = r; R = r; make_atlases(); }
    }
    R = r;
}

int gfx_slot(void) { return S; }

/* Glyphs have their own advances; large text uses the large atlas so it is never stretched much. */
static float glyph_adv(int ch, float px) { return (ch < 32 || ch > 126 ? atlas[0].adv['?' - 32] : atlas[0].adv[ch - 32]) * 0.001f * px; }

float gfx_text_width(float px, const char *s)
{
    float w = 0;
    for (; *s; s++) w += glyph_adv((unsigned char)*s, px);
    return w;
}

void gfx_text(float x, float y, float px, SDL_Color c, int align, const char *s)
{
    const Atlas *a = &atlas[px > 64 ? 1 : 0];
    float f = px / a->size, m = (a->size / 10) * f;
    if (align) { float w = gfx_text_width(px, s); x -= align == 1 ? w / 2 : w; }
    y -= 0.17f * px;
    SDL_SetTextureColorMod(a->tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(a->tex, c.a);
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if (ch < 33 || ch > 126) { if (ch == 32) { x += glyph_adv(32, px); continue; } ch = '?'; }
        int i = ch - 32;
        SDL_Rect src = {(i % a->cols) * a->cw, (i / a->cols) * a->ch, a->cw, a->ch};
        SDL_FRect dst = {x - m, y - m, a->cw * f, a->ch * f};
        SDL_RenderCopyF(R, a->tex, &src, &dst);
        x += glyph_adv(ch, px);
    }
}

void gfx_quad(const float *xy, const SDL_Color *c)
{
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) v[i] = (SDL_Vertex){{xy[i * 2], xy[i * 2 + 1]}, c[i], {0, 0}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(R, NULL, v, 4, idx, 6);
}

void gfx_rect(float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(R, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_FRect r = {x, y, w, h};
    SDL_RenderFillRectF(R, &r);
}

static uint8_t *(*preview_loader)(const char *key, size_t *len);
void gfx_set_preview_loader(uint8_t *(*load)(const char *key, size_t *len)) { preview_loader = load; }

typedef struct { char key[48]; SDL_Texture *tex; int w, h; } Preview;
static Preview previews_s[2][64];
static int preview_next_s[2];
#define previews previews_s[S]
#define preview_next preview_next_s[S]

/* The picture of a preview key, loaded once (w == 0 when the key has none). */
static const Preview *get_preview(const char *key)
{
    for (int i = 0; i < 64; i++) if (previews[i].key[0] && !strcmp(previews[i].key, key)) return previews[i].w ? &previews[i] : NULL;
    Preview *p = &previews[preview_next];
    preview_next = (preview_next + 1) % 64;
    if (p->tex) SDL_DestroyTexture(p->tex);
    memset(p, 0, sizeof *p);
    snprintf(p->key, sizeof p->key, "%s", key);
    size_t len;
    uint8_t *buf = preview_loader ? preview_loader(key, &len) : NULL;
    int w, h;
    const uint8_t *rgba;
    if (buf && smz_preview_parse(buf, len, &w, &h, &rgba)) {
        p->tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (p->tex) {
            SDL_UpdateTexture(p->tex, NULL, rgba, w * 4);
            SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(p->tex, SDL_ScaleModeNearest);
            p->w = w; p->h = h;
        }
    }
    free(buf);
    return p->w ? p : NULL;
}

extern const int PC_LOGO_SIZE;
extern const unsigned char PC_LOGO_RGBA[];

void gfx_logo(float x, float y, float size, float alpha)
{
    static SDL_Texture *logos[2];
    SDL_Texture *logo = logos[S];
    if (!logo) {
        logos[S] = logo = SDL_CreateTexture(R, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, PC_LOGO_SIZE, PC_LOGO_SIZE);
        SDL_UpdateTexture(logo, NULL, PC_LOGO_RGBA, PC_LOGO_SIZE * 4);
        SDL_SetTextureBlendMode(logo, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(logo, SDL_ScaleModeLinear);
    }
    SDL_SetTextureAlphaMod(logo, (Uint8)(255 * (alpha < 0 ? 0 : alpha > 1 ? 1 : alpha)));
    SDL_FRect d = {x, y, size, size};
    SDL_RenderCopyF(R, logo, NULL, &d);
}

static SDL_Color col4(int r, int g, int b, float a) { return (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)(255 * (a < 0 ? 0 : a > 1 ? 1 : a))}; }

/* A box with its corners cut off, filled in rows of one pixel. */
void gfx_chamfer(float x, float y, float w, float h, float cut, SDL_Color c)
{
    int n = (int)cut;
    for (int i = 0; i < n; i++) {
        float in = (float)(n - i);
        gfx_rect(x + in, y + i, w - 2 * in, 1, c);
        gfx_rect(x + in, y + h - 1 - i, w - 2 * in, 1, c);
    }
    gfx_rect(x, y + n, w, h - 2 * n, c);
}

static void backdrop(int win_w, int win_h, float ox, float oy, float k)
{
    if (win_w) gfx_rect(0, 0, (float)win_w, (float)win_h, col4(0x0A, 0x0E, 0x15, 1));
    else gfx_rect(ox, oy, UI_W * k, UI_H * k, col4(0x0A, 0x0E, 0x15, 1));
    float W = UI_W * k, H = UI_H * k;
    {
        float top[8] = {ox, oy, ox + W, oy, ox + W, oy + 90 * k, ox, oy + 90 * k};
        SDL_Color tc[4] = {col4(0x2A, 0x21, 0x12, 0.9f), col4(0x2A, 0x21, 0x12, 0.9f), col4(0x2A, 0x21, 0x12, 0), col4(0x2A, 0x21, 0x12, 0)};
        gfx_quad(top, tc);
        float bot[8] = {ox, oy + H - 70 * k, ox + W, oy + H - 70 * k, ox + W, oy + H, ox, oy + H};
        SDL_Color bc[4] = {col4(0x0F, 0x2A, 0x2C, 0), col4(0x0F, 0x2A, 0x2C, 0), col4(0x0F, 0x2A, 0x2C, 0.8f), col4(0x0F, 0x2A, 0x2C, 0.8f)};
        gfx_quad(bot, bc);
    }
    for (int i = 0; i <= 20; i++) {
        float d = fabsf(i * 16 - 160) / 160.0f, a = 0.05f * (1 - d * d);
        gfx_rect(ox + i * 16 * k, oy, 1, H, col4(0x4F, 0xC3, 0xF7, a));
    }
    for (int j = 0; j <= 15; j++) {
        float d = fabsf(j * 16 - 108) / 130.0f, a = 0.05f * (1 - d * d);
        if (a > 0) gfx_rect(ox, oy + j * 16 * k, W, 1, col4(0x4F, 0xC3, 0xF7, a));
    }
}

void gfx_rect_idx(float k, float ox, float oy, float lx, float ly, float lw, float lh, int color)
{
    float pulse = 0.5f + 0.5f * sinf(SDL_GetTicks() / 1000.0f * 3.4f);
    float l = roundf(ox + lx * k), t = roundf(oy + ly * k), w = roundf(ox + (lx + lw) * k) - l, h = roundf(oy + (ly + lh) * k) - t;
    if (color == 0 && lw >= UI_W - 2 && lh >= UI_H - 2) return;   /* the full-screen clear: the backdrop stays */
    if (color == 1 && lw >= 280 && ly <= 2) {   /* the title strip */
        float sq[8] = {l, t, l + w, t, l + w, t + h, l, t + h};
        SDL_Color sc[4] = {col4(0x14, 0x1B, 0x27, 1), col4(0x14, 0x1B, 0x27, 1), col4(0x10, 0x16, 0x1F, 1), col4(0x10, 0x16, 0x1F, 1)};
        gfx_quad(sq, sc);
        gfx_rect(l, t + h - 1, w, 1, col4(0x25, 0x32, 0x4A, 1));
    } else if ((color == 1 || color == 2) && lw >= 24 && lh >= 10) {
        float cut = lh >= 28 ? 4 * k : 2 * k;
        if (color == 2) {   /* the chosen row: amber edge and a glow that breathes */
            for (int g = 3; g >= 1; g--) gfx_chamfer(l - g * k, t - g * k, w + 2 * g * k, h + 2 * g * k, cut + g * k, col4(0xF2, 0xA3, 0x3A, (0.10f + 0.10f * pulse) / g));
            gfx_chamfer(l, t, w, h, cut, col4(0xF2, 0xA3, 0x3A, 0.55f + 0.45f * pulse));
            gfx_chamfer(l + k, t + k, w - 2 * k, h - 2 * k, cut > k ? cut - k : 0, col4(0x24, 0x22, 0x1C, 1));
            gfx_rect(l + k, t + k, w / 2, h - 2 * k, col4(0xF2, 0xA3, 0x3A, 0.10f));
        } else {
            gfx_chamfer(l, t, w, h, cut, col4(0x25, 0x32, 0x4A, 1));
            gfx_chamfer(l + k * 0.7f, t + k * 0.7f, w - 1.4f * k, h - 1.4f * k, cut > k ? cut - k * 0.7f : 0, col4(0x14, 0x1B, 0x27, 1));
        }
    } else gfx_rect(l, t, w, h, gfx_ui_color(color));
}

void gfx_backdrop_ui(float k, float ox, float oy)
{
    backdrop(0, 0, ox, oy, k);
}

/* A ring centred on cx, cy: from the top, clockwise, over frac of the turn. */
static void ring(float cx, float cy, float r, float th, float frac, SDL_Color c)
{
    enum { SEG = 96 };
    SDL_Vertex v[2 * (SEG + 1)];
    int idx[6 * SEG], n = (int)(SEG * frac + 0.5f);
    if (n < 1) return;
    for (int i = 0; i <= n; i++) {
        float a = -1.5707963f + 6.2831853f * frac * i / n, ca = cosf(a), sa = sinf(a);
        v[2 * i] = (SDL_Vertex){{cx + ca * r, cy + sa * r}, c, {0, 0}};
        v[2 * i + 1] = (SDL_Vertex){{cx + ca * (r - th), cy + sa * (r - th)}, c, {0, 0}};
    }
    for (int i = 0; i < n; i++) {
        int *q = idx + 6 * i, a = 2 * i;
        q[0] = a; q[1] = a + 1; q[2] = a + 2; q[3] = a + 1; q[4] = a + 3; q[5] = a + 2;
    }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(R, NULL, v, 2 * (n + 1), idx, 6 * n);
}

float gfx_ui(const SmzUi *u, int win_w, int win_h, float *ox, float *oy)
{
    float k = (float)win_w / UI_W < (float)win_h / UI_H ? (float)win_w / UI_W : (float)win_h / UI_H;
    *ox = (win_w - UI_W * k) / 2; *oy = (win_h - UI_H * k) / 2;
    backdrop(win_w, win_h, *ox, *oy, k);
    char hint[160] = "";
    float pulse = 0.5f + 0.5f * sinf(SDL_GetTicks() / 1000.0f * 3.4f);
    for (int i = 0; i < u->cmd_count; i++) {
        const UiCmd *c = &u->cmd[i];
        float x = *ox + c->x * k, y = *oy + c->y * k;
        SDL_Color col = gfx_ui_color(c->color);
        if (c->kind == 0) {
            gfx_rect_idx(k, *ox, *oy, c->x, c->y, c->w, c->h, c->color);
        } else if (c->kind == 3) {
            SDL_Color end = gfx_ui_color(c->align);
            float l = roundf(x), t = roundf(y), r = roundf(x + c->w * k), b = roundf(y + c->h * k);
            SDL_Color a = col, e = end; a.a = e.a = 191;
            SDL_Vertex v[4] = {{{l, t}, a, {0, 0}}, {{r, t}, e, {0, 0}}, {{r, b}, e, {0, 0}}, {{l, b}, a, {0, 0}}};
            int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(R, NULL, v, 4, idx, 6);
        } else if (c->kind == 4) {   /* progress ring: a dark track and the filled part, with a soft glow */
            float r = c->w * k / 2, th = 4.2f * k, cx = x + r, cy = y + c->h * k / 2;
            ring(cx, cy, r, th, 1, (SDL_Color){0x25, 0x32, 0x4A, 255});
            SDL_Color glow = col; glow.a = 60;
            ring(cx, cy, r + 1.5f * k, th + 3 * k, c->align / 100.0f, glow);
            ring(cx, cy, r, th, c->align / 100.0f, col);
        } else if (c->kind == 5) {   /* orb: a soap bubble, a faint body, a bright rim with colours that turn, highlights */
            float t = SDL_GetTicks() / 1000.0f, r = 9 * k * c->align / 100.0f * (1 + 0.035f * sinf(t * 4.2f));
            float cx = x + c->w * k / 2, cy = y + c->h * k / 2;
            SDL_Color halo = col, body = col, rim = col;
            halo.a = (Uint8)(30 + 25 * pulse); body.a = 40; rim.a = 220;
            ring(cx, cy, r * 2.1f, r * 1.1f, 1, halo);
            ring(cx, cy, r, r, 1, body);
            static const SDL_Color film[3] = {{0x44, 0xE0, 0xFF, 150}, {0xFF, 0x6A, 0xD5, 150}, {0xFF, 0xE0, 0x7A, 150}};
            for (int s = 0; s < 3; s++) {   /* three arcs of colour circling just inside the rim */
                float a0 = t * 1.6f + s * 2.0943951f;
                for (int j = 0; j < 10; j++) {
                    float a = a0 + j * 0.09f, ca = cosf(a), sa = sinf(a);
                    SDL_Color fc = film[s]; fc.a = (Uint8)(fc.a * (1 - j / 10.0f));
                    ring(cx + ca * r * 0.82f, cy + sa * r * 0.82f, 0.9f * k, 0.9f * k, 1, fc);
                }
            }
            ring(cx, cy, r, 0.9f * k, 1, rim);
            ring(cx - r * 0.38f, cy - r * 0.45f, r * 0.24f, r * 0.24f, 1, (SDL_Color){0xFF, 0xFF, 0xFF, 210});
            ring(cx + r * 0.45f, cy + r * 0.5f, r * 0.1f, r * 0.1f, 1, (SDL_Color){0xFF, 0xFF, 0xFF, 110});
        } else if (c->kind == 2 && !strcmp(c->text, "logo")) {
            gfx_logo(x, y, c->w * k, 1);
        } else if (c->kind == 2) {
            const Preview *p = get_preview(c->text);
            if (p) {   /* centred in its box and scaled to fit, the way the other frontends draw it */
                float bw = c->w * k, bh = c->h * k, s = bw / p->w < bh / p->h ? bw / p->w : bh / p->h;
                SDL_FRect d = {x + (bw - p->w * s) / 2, y + (bh - p->h * s) / 2, p->w * s, p->h * s};
                SDL_SetTextureColorMod(p->tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(p->tex, c->color == 4 ? 64 : 255);   /* dim colour: what is still missing (a boss not beaten) */
                SDL_RenderCopyF(R, p->tex, NULL, &d);
            }
        } else if (c->kind == 1) {
            if (c->y >= UI_H - 16 && help_is_hint(c->text)) {   /* the hint line: drawn as a bar with the buttons of the device in use */
                size_t n = strlen(hint);
                snprintf(hint + n, sizeof hint - n, "%s%s", n ? "  " : "", c->text);
            } else gfx_text(x, y - 2 * k - (c->y >= UI_H - 20 && c->align == 0 ? 7 * k : 0), 12 * k, col, c->align, c->text);
        }
    }
    if (hint[0]) help_bar_hint(k, *ox, *oy, hint);
    return k;
}
