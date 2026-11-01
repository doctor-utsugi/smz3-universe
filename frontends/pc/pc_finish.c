#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_finish.h"
#include "pc_gfx.h"
#include "pc_help.h"
#include "pc_sfx.h"

/* The timeline, in seconds from the card's first frame. */
#define T_CARD 0.10f
#define T_TITLE 0.35f
#define T_COUNT 0.70f
#define T_LAND 2.10f
#define T_ROWS 3.00f   /* one detail row per second from here */
#define ROWS 4
#define T_BUTTONS (T_ROWS + ROWS)

#define PARTS 160

static const SDL_Color CONFETTI[] = {
    {0xF2, 0xA3, 0x3A, 255}, {0xFF, 0xE0, 0x7A, 255}, {0x5B, 0xD4, 0x6A, 255}, {0xFF, 0x8A, 0x3D, 255}, {0x4F, 0xC3, 0xF7, 255}, {0xFF, 0x6A, 0xD5, 255},
};

typedef struct { float x, y, vx, vy, rot, vrot, size, flutter; int color, on; } Part;
static Part part[PARTS];
static Uint32 start_ms;
static float last_t, skip_to;
static int landed, rows_said, ticks_said;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease_out(float p) { p = clamp01(p); return 1 - (1 - p) * (1 - p) * (1 - p); }
static float back_out(float p) { p = clamp01(p); float c = 1.9f; return 1 + (c + 1) * powf(p - 1, 3) + c * powf(p - 1, 2); }
static float frand(void) { return (float)rand() / RAND_MAX; }
static SDL_Color with_a(SDL_Color c, float a) { c.a = (Uint8)(255 * clamp01(a)); return c; }

void finish_start(void)
{
    start_ms = SDL_GetTicks();
    last_t = 0; skip_to = 0; landed = 0; rows_said = 0; ticks_said = 0;
    memset(part, 0, sizeof part);
}

static float now(void) { float t = (SDL_GetTicks() - start_ms) / 1000.0f; return t > skip_to ? t : skip_to; }

int finish_skip(void)
{
    if (now() >= T_BUTTONS) return 0;
    skip_to = T_BUTTONS;
    start_ms = SDL_GetTicks() - (Uint32)(T_BUTTONS * 1000);
    return 1;
}

static void burst(float x, float y, int n, float speed)
{
    for (int i = 0; i < PARTS && n > 0; i++) {
        if (part[i].on) continue;
        float a = -(float)M_PI / 2 + (frand() - 0.5f) * 2.6f, v = speed * (0.45f + 0.55f * frand());
        part[i] = (Part){x, y, cosf(a) * v, sinf(a) * v, frand() * 6.3f, (frand() - 0.5f) * 14, 0.6f + 0.6f * frand(), frand() * 6.3f, rand() % 6, 1};
        n--;
    }
}

static void rain(int ww, float u)
{
    for (int i = 0; i < PARTS; i++) {
        if (part[i].on) continue;
        part[i] = (Part){frand() * ww, -20 * u, (frand() - 0.5f) * 40 * u, 60 * u + frand() * 80 * u, frand() * 6.3f, (frand() - 0.5f) * 8, 0.5f + 0.5f * frand(), frand() * 6.3f, rand() % 6, 1};
        return;
    }
}

static void draw_parts(float dt, int wh, float u)
{
    for (int i = 0; i < PARTS; i++) {
        if (!part[i].on) continue;
        part[i].vy += 520 * u * dt;
        part[i].vx *= 1 - 1.6f * dt;
        if (part[i].vy > 170 * u) part[i].vy = 170 * u;   /* paper falls slowly once it has opened */
        part[i].x += (part[i].vx + sinf(part[i].flutter + part[i].y / (40 * u)) * 30 * u) * dt;
        part[i].y += part[i].vy * dt;
        part[i].rot += part[i].vrot * dt;
        if (part[i].y > wh + 30 * u) { part[i].on = 0; continue; }
        float w = 9 * u * part[i].size, h = 5 * u * part[i].size * fabsf(cosf(part[i].rot * 1.7f)) + u;   /* the spin shows its edge */
        float c = cosf(part[i].rot), s = sinf(part[i].rot), xy[8];
        const float cx[4] = {-w, w, w, -w}, cy[4] = {-h, -h, h, h};
        for (int k = 0; k < 4; k++) { xy[k * 2] = part[i].x + cx[k] * c - cy[k] * s; xy[k * 2 + 1] = part[i].y + cx[k] * s + cy[k] * c; }
        SDL_Color col = CONFETTI[part[i].color], cs[4] = {col, col, col, col};
        gfx_quad(xy, cs);
    }
}

static void rays(float cx, float cy, float len, float t, float a)
{
    enum { N = 14 };
    for (int i = 0; i < N; i++) {
        float a0 = t * 0.12f + i * 2 * (float)M_PI / N, a1 = a0 + (float)M_PI / N * 0.55f;
        float xy[8] = {cx, cy, cx + cosf(a0) * len, cy + sinf(a0) * len, cx + cosf(a1) * len, cy + sinf(a1) * len, cx, cy};
        SDL_Color in = {0xF2, 0xA3, 0x3A, (Uint8)(70 * a)}, out = {0xF2, 0xA3, 0x3A, 0}, cs[4] = {in, out, out, in};
        gfx_quad(xy, cs);
    }
}

static void ring(float cx, float cy, float r, float th, SDL_Color c)
{
    enum { N = 48 };
    for (int i = 0; i < N; i++) {
        float a0 = i * 2 * (float)M_PI / N, a1 = (i + 1) * 2 * (float)M_PI / N;
        float xy[8] = {cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r,
                       cx + cosf(a1) * (r - th), cy + sinf(a1) * (r - th), cx + cosf(a0) * (r - th), cy + sinf(a0) * (r - th)};
        SDL_Color cs[4] = {c, c, c, c};
        gfx_quad(xy, cs);
    }
}

static void fmt_time(int64_t ms, char *out, size_t cap)
{
    long long s = ms / 1000;
    snprintf(out, cap, "%lld:%02lld:%02lld.%02lld", s / 3600, s / 60 % 60, s % 60, ms % 1000 / 10);
}

static int pct(unsigned a, unsigned b) { return b ? (int)(100.0 * a / b + 0.5) : 0; }

void finish_draw(const FinishCard *f, int ww, int wh)
{
    float t = now(), dt = fminf(0.05f, fmaxf(0, t - last_t));
    last_t = t;
    float u = wh / 720.0f, cw = 700 * u, ch = 470 * u, mx = ww / 2.0f;
    float pop = back_out((t - T_CARD) / 0.45f), cy0 = (wh - ch) / 2, cy = cy0 + (1 - pop) * 60 * u;
    float in = clamp01((t - T_CARD) / 0.3f);

    gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){0, 0, 0, (Uint8)(185 * clamp01(t / 0.3f))});
    rays(mx, cy0 + 120 * u, (float)ww, t, in * (0.7f + 0.3f * sinf(t * 2.2f)));
    if (t >= T_TITLE && t < T_LAND && frand() < 0.25f) rain(ww, u);
    if (t >= T_LAND && frand() < 0.4f) rain(ww, u);
    draw_parts(dt, wh, u);   /* behind the card, so the numbers stay readable */

    /* the card, dropping in with a little overshoot */
    float glow = 0.5f + 0.5f * sinf(t * 3.4f), sc = 0.86f + 0.14f * pop, w = cw * sc, h = ch * sc, x = mx - w / 2, y = cy + (ch - h) / 2;
    gfx_rect(x - 4 * u, y - 4 * u, w + 8 * u, h + 8 * u, (SDL_Color){0xF2, 0xA3, 0x3A, (Uint8)((80 + 110 * glow) * in)});
    gfx_rect(x, y, w, h, (SDL_Color){0x10, 0x16, 0x1F, (Uint8)(255 * in)});
    gfx_rect(x, y, w, 3 * u, with_a((SDL_Color){0xFF, 0xE0, 0x7A, 255}, in));

    /* the title, letter by letter */
    float tpx = 34 * u, tw = gfx_text_width(tpx, f->title), tx = mx - tw / 2;
    for (int i = 0; f->title[i]; i++) {
        char pre[64], ch1[2] = {f->title[i], 0};
        snprintf(pre, sizeof pre, "%.*s", i, f->title);
        float lp = clamp01((t - T_TITLE - i * 0.035f) / 0.22f), ls = 1 + 0.9f * (1 - back_out(lp));
        if (lp <= 0 || f->title[i] == ' ') continue;
        float lx = tx + gfx_text_width(tpx, pre), lw = gfx_text_width(tpx, ch1);
        gfx_text(lx + lw / 2, cy + 30 * u - (ls - 1) * tpx / 2, tpx * ls, with_a((SDL_Color){0xF2, 0xA3, 0x3A, 255}, lp), 1, ch1);
    }

    /* the final time, counting up, then a punch, a ring and the fanfare when it lands */
    float cp = ease_out((t - T_COUNT) / (T_LAND - T_COUNT));
    if (t >= T_COUNT && t < T_LAND && (int)((t - T_COUNT) / 0.11f) > ticks_said) { ticks_said = (int)((t - T_COUNT) / 0.11f); sfx_play(SFX_TICK); }
    if (t >= T_LAND && !landed) {
        landed = 1;
        sfx_play(SFX_FANFARE);
        burst(mx - 365 * u, cy + 150 * u, 60, 900 * u);
        burst(mx + 365 * u, cy + 150 * u, 60, 900 * u);
    }
    char ts[40];
    fmt_time(t >= T_LAND ? f->ms : (int64_t)(f->ms * cp), ts, sizeof ts);
    float punch = t >= T_LAND ? 1 + 0.18f * expf(-(t - T_LAND) * 7) : 1;
    if (t >= T_COUNT) gfx_text(mx, cy + 84 * u - (punch - 1) * 40 * u, 78 * u * punch, with_a((SDL_Color){0xE8, 0xEC, 0xF2, 255}, (t - T_COUNT) / 0.2f), 1, ts);
    if (t >= T_LAND && t < T_LAND + 0.7f) {
        float rp = (t - T_LAND) / 0.7f;
        ring(mx, cy + 124 * u, 60 * u + 300 * u * ease_out(rp), 6 * u * (1 - rp) + u, (SDL_Color){0xFF, 0xE0, 0x7A, (Uint8)(200 * (1 - rp))});
    }
    if (t >= T_LAND) gfx_text(mx, cy + 172 * u, 18 * u, with_a((SDL_Color){0x87, 0x93, 0xA6, 255}, (t - T_LAND) / 0.4f), 1, f->name);

    /* the details, one per second */
    unsigned all = f->zelda + f->metroid, all_total = f->zelda_total + f->metroid_total;
    for (int r = 0; r < ROWS; r++) {
        float rt = t - T_ROWS - r, rp = ease_out(rt / 0.35f);
        if (rt < 0) continue;
        if (r >= rows_said) { rows_said = r + 1; if (skip_to < T_BUTTONS) sfx_play(SFX_SELECT); }
        float ry = cy + 214 * u + r * 46 * u + (1 - rp) * 14 * u, lx = mx - 300 * u, rx = mx + 300 * u, fill = ease_out(rt / 0.7f);
        SDL_Color dim = with_a((SDL_Color){0x87, 0x93, 0xA6, 255}, rp), text = with_a((SDL_Color){0xE8, 0xEC, 0xF2, 255}, rp);
        char v[64];
        if (r == 0) {
            gfx_text(lx, ry, 18 * u, dim, 0, "COMPLETION");
            snprintf(v, sizeof v, "%d%%", (int)(pct(all, all_total) * fill + 0.5f));
            gfx_text(rx, ry - 4 * u, 26 * u, with_a((SDL_Color){0xFF, 0xE0, 0x7A, 255}, rp), 2, v);
        } else if (r <= 2) {
            unsigned got = r == 1 ? f->zelda : f->metroid, of = r == 1 ? f->zelda_total : f->metroid_total;
            SDL_Color bar = r == 1 ? (SDL_Color){0x5B, 0xD4, 0x6A, 255} : (SDL_Color){0xFF, 0x8A, 0x3D, 255};
            gfx_text(lx, ry, 18 * u, dim, 0, r == 1 ? "ZELDA" : "METROID");
            snprintf(v, sizeof v, "%u/%u   %d%%", got, of, pct(got, of));
            gfx_text(rx, ry, 20 * u, text, 2, v);
            float bx = lx + 120 * u, bw = 300 * u - 120 * u;
            gfx_rect(bx, ry + 6 * u, bw, 8 * u, with_a((SDL_Color){0x1D, 0x24, 0x30, 255}, rp));
            gfx_rect(bx, ry + 6 * u, bw * fill * (of ? (float)got / of : 0), 8 * u, with_a(bar, rp));
        } else {
            gfx_text(mx, ry, 19 * u, with_a(f->ladder_color, rp), 1, f->ladder);
        }
    }

    if (t >= T_BUTTONS) {
        static const HelpItem OK[] = {{"A", "Enter", "Main menu"}, {"B", "Esc", "Watch the credits"}};
        help_prompt(mx, cy + ch - 56 * u + (1 - ease_out((t - T_BUTTONS) / 0.3f)) * 10 * u, 34 * u, OK, 2);
    }

    if (t < 0.35f) gfx_rect(0, 0, (float)ww, (float)wh, (SDL_Color){255, 250, 235, (Uint8)(150 * (1 - t / 0.35f))});
}

void finish_panel(const FinishCard *f, float x, float w, float h)
{
    float u = fminf(h / 720.0f, w / 260.0f), lx = x + 14 * u, rx = x + w - 14 * u, y = 26 * u;
    SDL_Color dim = {0x87, 0x93, 0xA6, 255}, text = {0xE8, 0xEC, 0xF2, 255};
    gfx_rect(x, 0, w, h, (SDL_Color){0x0A, 0x0E, 0x15, 255});
    gfx_rect(x, 0, w, 3 * u, (SDL_Color){0xF2, 0xA3, 0x3A, 255});
    gfx_text(x + w / 2, y, 20 * u, (SDL_Color){0xF2, 0xA3, 0x3A, 255}, 1, f->title); y += 32 * u;
    gfx_text(x + w / 2, y, 13 * u, dim, 1, f->name); y += 44 * u;
    char v[48];
    fmt_time(f->ms, v, sizeof v);
    gfx_text(lx, y, 12 * u, dim, 0, "TIME"); y += 20 * u;
    gfx_text(lx, y, 30 * u, text, 0, v); y += 58 * u;
    unsigned all = f->zelda + f->metroid, all_total = f->zelda_total + f->metroid_total;
    gfx_text(lx, y, 12 * u, dim, 0, "COMPLETION"); y += 20 * u;
    snprintf(v, sizeof v, "%d%%", pct(all, all_total));
    gfx_text(lx, y, 30 * u, (SDL_Color){0xFF, 0xE0, 0x7A, 255}, 0, v); y += 58 * u;
    for (int r = 0; r < 2; r++, y += 58 * u) {
        unsigned got = r ? f->metroid : f->zelda, of = r ? f->metroid_total : f->zelda_total;
        gfx_text(lx, y, 12 * u, dim, 0, r ? "METROID" : "ZELDA");
        snprintf(v, sizeof v, "%u/%u  %d%%", got, of, pct(got, of));
        gfx_text(rx, y - 2 * u, 14 * u, text, 2, v);
        gfx_rect(lx, y + 20 * u, rx - lx, 7 * u, (SDL_Color){0x1D, 0x24, 0x30, 255});
        gfx_rect(lx, y + 20 * u, (rx - lx) * (of ? (float)got / of : 0), 7 * u, r ? (SDL_Color){0xFF, 0x8A, 0x3D, 255} : (SDL_Color){0x5B, 0xD4, 0x6A, 255});
    }
}
