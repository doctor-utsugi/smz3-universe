#include <math.h>
#include <string.h>
#include <ctype.h>
#include "pc_gfx.h"
#include "pc_help.h"

enum { K_XBOX, K_PS, K_NINTENDO };
static int using_pad, pad_kind;

void help_note_keyboard(void) { using_pad = 0; }
int help_pad_in_use(void) { return using_pad; }

void help_note_pad(SDL_GameController *c)
{
    using_pad = 1;
    if (!c) return;
    switch (SDL_GameControllerGetType(c)) {
    case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4: case SDL_CONTROLLER_TYPE_PS5: pad_kind = K_PS; break;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: pad_kind = K_NINTENDO; break;
    default: pad_kind = K_XBOX;
    }
}

static SDL_Color rgb(int r, int g, int b, float a) { return (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)(255 * (a < 0 ? 0 : a > 1 ? 1 : a))}; }

static void line(float x1, float y1, float x2, float y2, float t, SDL_Color c)
{
    float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) return;
    float nx = -dy / l * t / 2, ny = dx / l * t / 2;
    float q[8] = {x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny};
    SDL_Color cs[4] = {c, c, c, c};
    gfx_quad(q, cs);
}

static void disc(float cx, float cy, float r, SDL_Color c)
{
    for (float y = -r; y <= r; y += 1) {
        float half = sqrtf(fmaxf(r * r - y * y, 0));
        gfx_rect(cx - half, cy + y, half * 2, 1, c);
    }
}

static void ring(float cx, float cy, float r, float t, SDL_Color c)
{
    int n = 28;
    for (int i = 0; i < n; i++) {
        float a1 = 6.2831853f * i / n, a2 = 6.2831853f * (i + 1) / n;
        line(cx + cosf(a1) * r, cy + sinf(a1) * r, cx + cosf(a2) * r, cy + sinf(a2) * r, t, c);
    }
}

/* A key cap: a light edge with a darker face and a thicker lower lip. */
static void cap(float x, float y, float w, float h, SDL_Color edge, SDL_Color fill)
{
    float cut = fmaxf(2, h * 0.2f), t = fmaxf(1.5f, h * 0.07f);
    gfx_chamfer(x, y, w, h, cut, edge);
    gfx_chamfer(x + t, y + t, w - 2 * t, h - 2 * t - t, cut > t ? cut - t : 0, fill);
}

static float text_w(float h, const char *s) { return gfx_text_width(h * 0.56f, s); }

static float pill(float x, float cy, float h, const char *label, SDL_Color edge)
{
    float w = fmaxf(h, text_w(h, label) + h * 0.7f);
    cap(x, cy - h / 2, w, h, edge, rgb(0x1D, 0x24, 0x30, 1));
    gfx_text(x + w / 2, cy - h * 0.34f, h * 0.56f, rgb(0xE8, 0xEC, 0xF2, 1), 1, label);
    return w;
}

static float face(float x, float cy, float h, const char *pad)
{
    int which = pad[0] == 'A' ? 0 : pad[0] == 'B' ? 1 : pad[0] == 'X' ? 2 : 3;
    float cx = x + h / 2, r = h / 2;
    SDL_Color edge, glyph;
    static const SDL_Color XB[4] = {{0x5F, 0xD0, 0x68, 255}, {0xE0, 0x5A, 0x4F, 255}, {0x4F, 0xC3, 0xF7, 255}, {0xF2, 0xC2, 0x4A, 255}};
    static const SDL_Color PS[4] = {{0x6E, 0xA8, 0xFF, 255}, {0xFF, 0x6B, 0x6B, 255}, {0xFF, 0x8A, 0xD8, 255}, {0x5F, 0xE0, 0xA0, 255}};
    if (pad_kind == K_PS) {
        edge = glyph = PS[which];
        disc(cx, cy, r, rgb(0x1D, 0x24, 0x30, 1));
        ring(cx, cy, r - 1, fmaxf(1.2f, h * 0.07f), rgb(edge.r, edge.g, edge.b, 0.55f));
        float g = h * 0.22f, t = fmaxf(1.6f, h * 0.09f);
        if (which == 0) { line(cx - g, cy - g, cx + g, cy + g, t, glyph); line(cx + g, cy - g, cx - g, cy + g, t, glyph); }
        else if (which == 1) ring(cx, cy, g * 1.15f, t, glyph);
        else if (which == 2) { line(cx - g, cy - g, cx + g, cy - g, t, glyph); line(cx + g, cy - g, cx + g, cy + g, t, glyph); line(cx + g, cy + g, cx - g, cy + g, t, glyph); line(cx - g, cy + g, cx - g, cy - g, t, glyph); }
        else { line(cx, cy - g * 1.2f, cx + g * 1.15f, cy + g * 0.9f, t, glyph); line(cx + g * 1.15f, cy + g * 0.9f, cx - g * 1.15f, cy + g * 0.9f, t, glyph); line(cx - g * 1.15f, cy + g * 0.9f, cx, cy - g * 1.2f, t, glyph); }
    } else {
        int pos = which;
        if (pad_kind == K_NINTENDO) pos = which == 0 ? 1 : which == 1 ? 0 : which == 2 ? 3 : 2;
        edge = pad_kind == K_NINTENDO ? (SDL_Color){0xC7, 0xD0, 0xDE, 255} : XB[which];
        disc(cx, cy, r, rgb(0x1D, 0x24, 0x30, 1));
        ring(cx, cy, r - 1, fmaxf(1.2f, h * 0.07f), rgb(edge.r, edge.g, edge.b, 0.9f));
        char s[2] = {"ABXY"[pos], 0};
        gfx_text(cx, cy - h * 0.34f, h * 0.56f, edge, 1, s);
    }
    return h;
}

static float dpad(float x, float cy, float h, int vertical, int horizontal)
{
    float cx = x + h / 2, a = h * 0.17f, L = h * 0.48f;
    SDL_Color off = rgb(0x4A, 0x58, 0x72, 1), on = rgb(0xE8, 0xEC, 0xF2, 1);
    gfx_rect(cx - a, cy - L, 2 * a, 2 * L, vertical ? on : off);
    gfx_rect(cx - L, cy - a, 2 * L, 2 * a, horizontal ? on : off);
    gfx_rect(cx - a, cy - a, 2 * a, 2 * a, on);
    return h;
}

static void arrow(float cx, float cy, float s, int dir, SDL_Color c)
{
    float q[8];
    float dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    float fx = dx[dir], fy = dy[dir], px = -fy, py = fx;
    float tip[2] = {cx + fx * s, cy + fy * s}, b1[2] = {cx - fx * s * 0.7f + px * s, cy - fy * s * 0.7f + py * s}, b2[2] = {cx - fx * s * 0.7f - px * s, cy - fy * s * 0.7f - py * s};
    q[0] = tip[0]; q[1] = tip[1]; q[2] = b1[0]; q[3] = b1[1]; q[4] = b1[0] * 0.5f + b2[0] * 0.5f; q[5] = b1[1] * 0.5f + b2[1] * 0.5f; q[6] = b2[0]; q[7] = b2[1];
    SDL_Color cs[4] = {c, c, c, c};
    gfx_quad(q, cs);
}

static float key(float x, float cy, float h, const char *kb)
{
    SDL_Color edge = rgb(0x6F, 0x7D, 0x93, 1);
    int dir = !strcmp(kb, "^") ? 0 : !strcmp(kb, "v") ? 1 : !strcmp(kb, "<") ? 2 : !strcmp(kb, ">") ? 3 : -1;
    if (dir >= 0) {
        cap(x, cy - h / 2, h, h, edge, rgb(0x1D, 0x24, 0x30, 1));
        arrow(x + h / 2, cy, h * 0.24f, dir, rgb(0xE8, 0xEC, 0xF2, 1));
        return h;
    }
    return pill(x, cy, h, kb, edge);
}

static float pad_icon(float x, float cy, float h, const char *p)
{
    int ps = pad_kind == K_PS;
    if (!strcmp(p, "A") || !strcmp(p, "B") || !strcmp(p, "X") || !strcmp(p, "Y")) return face(x, cy, h, p);
    SDL_Color edge = rgb(0x6F, 0x7D, 0x93, 1);
    if (!strcmp(p, "LR2")) { float w1 = pill(x, cy, h, ps ? "L1" : "LB", edge); return w1 + h * 0.2f + pill(x + w1 + h * 0.2f, cy, h, ps ? "R1" : "RB", edge); }
    if (!strcmp(p, "L")) return pill(x, cy, h, ps ? "L1" : "LB", edge);
    if (!strcmp(p, "R")) return pill(x, cy, h, ps ? "R1" : "RB", edge);
    if (!strcmp(p, "LT")) return pill(x, cy, h, ps ? "L2" : "LT", edge);
    if (!strcmp(p, "RT")) return pill(x, cy, h, ps ? "R2" : "RT", edge);
    if (!strcmp(p, "SELECT")) return pill(x, cy, h, ps ? "Share" : "View", edge);
    if (!strcmp(p, "START")) return pill(x, cy, h, ps ? "Options" : "Menu", edge);
    if (!strcmp(p, "R3") || !strcmp(p, "L3")) return pill(x, cy, h, p, edge);
    if (!strcmp(p, "STICK")) { float cx = x + h / 2; disc(cx, cy, h * 0.48f, edge); disc(cx, cy, h * 0.4f, rgb(0x1D, 0x24, 0x30, 1)); disc(cx, cy, h * 0.16f, rgb(0xE8, 0xEC, 0xF2, 1)); return h; }
    if (!strcmp(p, "UD")) return dpad(x, cy, h, 1, 0);
    if (!strcmp(p, "LR")) return dpad(x, cy, h, 0, 1);
    if (!strcmp(p, "UP")) return dpad(x, cy, h, 1, 0);
    if (!strcmp(p, "DOWN")) return dpad(x, cy, h, 1, 0);
    if (!strcmp(p, "LEFT") || !strcmp(p, "RIGHT")) return dpad(x, cy, h, 0, 1);
    return dpad(x, cy, h, 1, 1);
}

float help_icon(float x, float y, float h, const char *p, const char *kb)
{
    if (using_pad && p) return pad_icon(x, y, h, p);
    return key(x, y, h, kb ? kb : p);
}

static float icon_w(float h, const char *p, const char *kb)
{
    if (using_pad && p) {
        if (!strcmp(p, "A") || !strcmp(p, "B") || !strcmp(p, "X") || !strcmp(p, "Y") || !strncmp(p, "D", 1) || !strcmp(p, "UD") || !strcmp(p, "LR") || !strcmp(p, "UP") || !strcmp(p, "DOWN") || !strcmp(p, "LEFT") || !strcmp(p, "RIGHT")) return h;
        int ps = pad_kind == K_PS;
        if (!strcmp(p, "STICK")) return h;
        if (!strcmp(p, "R3") || !strcmp(p, "L3")) return fmaxf(h, text_w(h, p) + h * 0.7f);
        if (!strcmp(p, "LR2")) return 2 * fmaxf(h, text_w(h, ps ? "L1" : "LB") + h * 0.7f) + h * 0.2f;
        const char *t = !strcmp(p, "L") ? (ps ? "L1" : "LB") : !strcmp(p, "R") ? (ps ? "R1" : "RB") : !strcmp(p, "LT") ? (ps ? "L2" : "LT") : !strcmp(p, "RT") ? (ps ? "R2" : "RT")
                      : !strcmp(p, "SELECT") ? (ps ? "Share" : "View") : (ps ? "Options" : "Menu");
        return fmaxf(h, text_w(h, t) + h * 0.7f);
    }
    const char *k = kb ? kb : p;
    if (!strcmp(k, "^") || !strcmp(k, "v") || !strcmp(k, "<") || !strcmp(k, ">")) return h;
    return fmaxf(h, text_w(h, k) + h * 0.7f);
}

static float item_w(float h, const HelpItem *it)
{
    return icon_w(h, it->pad, it->kb) + h * 0.3f + gfx_text_width(h * 0.58f, it->label);
}

static int shown(const HelpItem *it) { return using_pad ? it->pad != NULL : it->kb != NULL; }

void help_bar(float x, float y, float w, float h, const HelpItem *all, int count)
{
    HelpItem items[16];
    int n = 0;
    for (int i = 0; i < count && n < 16; i++) if (shown(&all[i])) items[n++] = all[i];
    gfx_rect(x, y, w, h, rgb(0x0C, 0x11, 0x19, 1));
    gfx_rect(x, y, w, fmaxf(1, h * 0.03f), rgb(0x25, 0x32, 0x4A, 1));
    float ih = h * 0.6f, gap = ih * 1.1f, total = 0;
    for (int i = 0; i < n; i++) total += item_w(ih, &items[i]) + (i ? gap : 0);
    float px = x + w - total - h * 0.8f, cy = y + h / 2;
    if (px < x + 4) px = x + 4;
    for (int i = 0; i < n; i++) {
        float iw = help_icon(px, cy, ih, items[i].pad, items[i].kb);
        gfx_text(px + iw + ih * 0.3f, cy - ih * 0.34f, ih * 0.58f, rgb(0xB8, 0xC2, 0xD4, 1), 0, items[i].label);
        px += item_w(ih, &items[i]) + gap;
    }
}

void help_prompt(float cx, float y, float h, const HelpItem *all, int count)
{
    HelpItem items[8];
    int n = 0;
    for (int i = 0; i < count && n < 8; i++) if (shown(&all[i])) items[n++] = all[i];
    float total = 0, gap = h * 1.6f;
    for (int i = 0; i < n; i++) total += item_w(h, &items[i]) + (i ? gap : 0);
    float px = cx - total / 2, cy = y + h / 2;
    for (int i = 0; i < n; i++) {
        float iw = help_icon(px, cy, h, items[i].pad, items[i].kb);
        gfx_text(px + iw + h * 0.3f, cy - h * 0.34f, h * 0.58f, rgb(0xC7, 0xD0, 0xDE, 1), 0, items[i].label);
        px += item_w(h, &items[i]) + gap;
    }
}

/* ---- the hint text of the shared menus ("A choose  Right: Random  B back") into buttons ---- */
static const struct { const char *word, *pad, *kb; } KEYWORD[] = {
    {"A", "A", "Enter"}, {"B", "B", "Esc"}, {"X", "X", "X"}, {"Y", "Y", "Y"}, {"L", "L", "Q"}, {"R", "R", "W"}, {"L/R", "LR2", "Q W"},
    {"Select", "SELECT", "Tab"}, {"Start", "START", "F1"}, {"Up", "UP", "^"}, {"Down", "DOWN", "v"}, {"Left", "LEFT", "<"}, {"Right", "RIGHT", ">"},
};

static int keyword(const char *w) { for (unsigned i = 0; i < sizeof KEYWORD / sizeof KEYWORD[0]; i++) if (!strcmp(KEYWORD[i].word, w)) return (int)i; return -1; }

int help_is_hint(const char *text)
{
    char w[24];
    int l = 0;
    while (text[l] && text[l] != ' ' && text[l] != ':' && l < 23) { w[l] = text[l]; l++; }
    w[l] = 0;
    return keyword(w) >= 0;
}

void help_bar_hint(float k, float ox, float oy, const char *hint)
{
    HelpItem items[10];
    char labels[10][40], words[24][24];
    int n = 0, nw = 0;
    for (const char *p = hint; *p && nw < 24;) {
        while (*p == ' ' || *p == ',') p++;
        int l = 0;
        while (p[l] && p[l] != ' ' && p[l] != ',' && l < 23) l++;
        memcpy(words[nw], p, (size_t)l);
        words[nw][l] = 0;
        if (l && words[nw][l - 1] == ':') words[nw][l - 1] = 0;
        nw++;
        p += l;
    }
    for (int i = 0; i < nw && n < 10;) {
        int kw = keyword(words[i]);
        if (kw < 0) { i++; continue; }
        int j = i + 1;
        const char *pad = KEYWORD[kw].pad, *kb = KEYWORD[kw].kb;
        if (j < nw && keyword(words[j]) >= 0 && ((!strcmp(words[i], "Left") && !strcmp(words[j], "Right")) || (!strcmp(words[i], "Up") && !strcmp(words[j], "Down")) || (!strcmp(words[i], "L") && !strcmp(words[j], "R")))) {
            pad = !strcmp(words[i], "Up") ? "UD" : !strcmp(words[i], "Left") ? "LR" : "LR2";
            kb = !strcmp(words[i], "Up") ? "^" : !strcmp(words[i], "Left") ? "<" : "Q";
            j++;
        }
        labels[n][0] = 0;
        while (j < nw && keyword(words[j]) < 0) {
            size_t cur = strlen(labels[n]);
            snprintf(labels[n] + cur, sizeof labels[n] - cur, "%s%s", cur ? " " : "", words[j]);
            j++;
        }
        if (labels[n][0]) labels[n][0] = (char)toupper((unsigned char)labels[n][0]);
        items[n] = (HelpItem){pad, kb, labels[n]};
        n++;
        i = j;
    }
    help_bar(ox, oy + (240 - 16) * k, UI_W * k, 16 * k, items, n);
}

float help_list(float x, float y, float row_h, const HelpItem *items, int n, int draw)
{
    float ih = row_h * 0.62f, col = 0, top = y;
    for (int i = 0; i < n; i++) if (items[i].label[0] != '#' && shown(&items[i])) col = fmaxf(col, icon_w(ih, items[i].pad, items[i].kb));
    for (int i = 0; i < n; i++) {
        const HelpItem *it = &items[i];
        if (it->label[0] == '#') {
            if (y > top) y += row_h * 0.35f;
            if (draw) gfx_text(x, y + row_h * 0.12f, row_h * 0.52f, rgb(0xF2, 0xA3, 0x3A, 1), 0, it->label + 1);
            y += row_h;
            continue;
        }
        if (!shown(it)) continue;
        if (draw) {
            float cy = y + row_h / 2;
            help_icon(x + col - icon_w(ih, it->pad, it->kb), cy, ih, it->pad, it->kb);
            gfx_text(x + col + ih * 0.6f, cy - ih * 0.36f, ih * 0.62f, rgb(0xC7, 0xD0, 0xDE, 1), 0, it->label);
        }
        y += row_h;
    }
    return y - top;
}
