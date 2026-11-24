/* alttpr's dialog encoder (Support/Dialog.php), text table (Text.php) and credits (Support/Credits.php), and the
   text half of Randomizer::randomize: setTexts, randomizeCredits and HintService::applyHints, drawing from z3_rng in
   the PHP's order. Ported from alttp_php_randomizer (sporchia, MIT). */
#include <stdio.h>
#include <string.h>
#include "z3_text.h"
#include "z3_fill.h"

/* ------------------------------------------------------------------ World::$texts / $credits */

void z3_texts_init(Z3Texts *t)
{
    memset(t->text, 0xFF, sizeof t->text);
    memset(t->credit, 0xFF, sizeof t->credit);
    t->used = 0;
}

static int16_t store(Z3Texts *t, const char *s)
{
    size_t n = strlen(s) + 1;
    if (t->used + n > sizeof t->pool) return -1;
    memcpy(t->pool + t->used, s, n);
    t->used += (int)n;
    return (int16_t)(t->used - (int)n);
}

bool z3_texts_set(Z3Texts *t, int key, const char *s)
{
    int16_t o = store(t, s);
    if (o < 0) return false;
    t->text[key] = o;
    return true;
}

bool z3_texts_credit(Z3Texts *t, int scene, const char *s)
{
    int16_t o = store(t, s);
    if (o < 0) return false;
    t->credit[scene] = o;
    return true;
}

/* ------------------------------------------------------------------ Dialog::convertDialogCompressed */

typedef struct { const char *ch; uint8_t n; uint8_t b[3]; } Glyph;

/* Dialog::$characters */
static const Glyph GLYPHS[] = {
    { " ", 1, { 0xFF } },
    { "\342\211\245", 1, { 0x99 } },
    { "\342\200\246", 1, { 0x9F } },
    { "?", 1, { 0xC6 } },
    { "!", 1, { 0xC7 } },
    { ",", 1, { 0xC8 } },
    { "-", 1, { 0xC9 } },
    { ".", 1, { 0xCD } },
    { "~", 1, { 0xCE } },
    { "\357\275\236", 1, { 0xCE } },
    { ":", 1, { 0xEA } },
    { "'", 1, { 0x9D } },
    { "\342\200\231", 1, { 0x9D } },
    { "@", 2, { 0xFE, 0x6A } },
    { ">", 2, { 0x9B, 0x9C } },
    { "%", 2, { 0xFD, 0x10 } },
    { "^", 2, { 0xFD, 0x11 } },
    { "=", 2, { 0xFD, 0x12 } },
    { "\342\206\221", 2, { 0xFD, 0x13 } },
    { "\342\206\223", 2, { 0xFD, 0x14 } },
    { "\342\206\222", 2, { 0xFD, 0x15 } },
    { "\342\206\220", 2, { 0xFD, 0x16 } },
    { "\302\274", 2, { 0xE5, 0xE7 } },
    { "\302\275", 2, { 0xE6, 0xE7 } },
    { "\302\276", 2, { 0xE8, 0xE9 } },
    { "\342\231\245", 2, { 0xEA, 0xEB } },
    { "\341\232\213", 3, { 0xFE, 0x6C, 0x00 } },
    { "\341\232\214", 3, { 0xFE, 0x6C, 0x01 } },
    { "\341\232\215", 3, { 0xFE, 0x6C, 0x02 } },
    { "\341\232\216", 3, { 0xFE, 0x6C, 0x03 } },
    { "\343\201\202", 1, { 0x00 } },
    { "\343\201\204", 1, { 0x01 } },
    { "\343\201\206", 1, { 0x02 } },
    { "\343\201\210", 1, { 0x03 } },
    { "\343\201\212", 1, { 0x04 } },
    { "\343\202\204", 1, { 0x05 } },
    { "\343\202\206", 1, { 0x06 } },
    { "\343\202\210", 1, { 0x07 } },
    { "\343\201\213", 1, { 0x08 } },
    { "\343\201\215", 1, { 0x09 } },
    { "\343\201\217", 1, { 0x0A } },
    { "\343\201\221", 1, { 0x0B } },
    { "\343\201\223", 1, { 0x0C } },
    { "\343\202\217", 1, { 0x0D } },
    { "\343\202\222", 1, { 0x0E } },
    { "\343\202\223", 1, { 0x0F } },
    { "\343\201\225", 1, { 0x10 } },
    { "\343\201\227", 1, { 0x11 } },
    { "\343\201\231", 1, { 0x12 } },
    { "\343\201\233", 1, { 0x13 } },
    { "\343\201\235", 1, { 0x14 } },
    { "\343\201\214", 1, { 0x15 } },
    { "\343\201\216", 1, { 0x16 } },
    { "\343\201\220", 1, { 0x17 } },
    { "\343\201\237", 1, { 0x18 } },
    { "\343\201\241", 1, { 0x19 } },
    { "\343\201\244", 1, { 0x1A } },
    { "\343\201\246", 1, { 0x1B } },
    { "\343\201\250", 1, { 0x1C } },
    { "\343\201\222", 1, { 0x1D } },
    { "\343\201\224", 1, { 0x1E } },
    { "\343\201\226", 1, { 0x1F } },
    { "\343\201\252", 1, { 0x20 } },
    { "\343\201\253", 1, { 0x21 } },
    { "\343\201\254", 1, { 0x22 } },
    { "\343\201\255", 1, { 0x23 } },
    { "\343\201\256", 1, { 0x24 } },
    { "\343\201\230", 1, { 0x25 } },
    { "\343\201\232", 1, { 0x26 } },
    { "\343\201\234", 1, { 0x27 } },
    { "\343\201\257", 1, { 0x28 } },
    { "\343\201\262", 1, { 0x29 } },
    { "\343\201\265", 1, { 0x2A } },
    { "\343\201\270", 1, { 0x2B } },
    { "\343\201\273", 1, { 0x2C } },
    { "\343\201\236", 1, { 0x2D } },
    { "\343\201\240", 1, { 0x2E } },
    { "\343\201\242", 1, { 0x2F } },
    { "\343\201\276", 1, { 0x30 } },
    { "\343\201\277", 1, { 0x31 } },
    { "\343\202\200", 1, { 0x32 } },
    { "\343\202\201", 1, { 0x33 } },
    { "\343\202\202", 1, { 0x34 } },
    { "\343\201\245", 1, { 0x35 } },
    { "\343\201\247", 1, { 0x36 } },
    { "\343\201\251", 1, { 0x37 } },
    { "\343\202\211", 1, { 0x38 } },
    { "\343\202\212", 1, { 0x39 } },
    { "\343\202\213", 1, { 0x3A } },
    { "\343\202\214", 1, { 0x3B } },
    { "\343\202\215", 1, { 0x3C } },
    { "\343\201\260", 1, { 0x3D } },
    { "\343\201\263", 1, { 0x3E } },
    { "\343\201\266", 1, { 0x3F } },
    { "\343\201\271", 1, { 0x40 } },
    { "\343\201\274", 1, { 0x41 } },
    { "\343\201\261", 1, { 0x42 } },
    { "\343\201\264", 1, { 0x43 } },
    { "\343\201\267", 1, { 0x44 } },
    { "\343\201\272", 1, { 0x45 } },
    { "\343\201\275", 1, { 0x46 } },
    { "\343\202\203", 1, { 0x47 } },
    { "\343\202\205", 1, { 0x48 } },
    { "\343\202\207", 1, { 0x49 } },
    { "\343\201\243", 1, { 0x4A } },
    { "\343\201\201", 1, { 0x4B } },
    { "\343\201\203", 1, { 0x4C } },
    { "\343\201\205", 1, { 0x4D } },
    { "\343\201\207", 1, { 0x4E } },
    { "\343\201\211", 1, { 0x4F } },
    { "\343\202\242", 1, { 0x50 } },
    { "\343\202\244", 1, { 0x51 } },
    { "\343\202\246", 1, { 0x52 } },
    { "\343\202\250", 1, { 0x53 } },
    { "\343\202\252", 1, { 0x54 } },
    { "\343\203\244", 1, { 0x55 } },
    { "\343\203\246", 1, { 0x56 } },
    { "\343\203\250", 1, { 0x57 } },
    { "\343\202\253", 1, { 0x58 } },
    { "\343\202\255", 1, { 0x59 } },
    { "\343\202\257", 1, { 0x5A } },
    { "\343\202\261", 1, { 0x5B } },
    { "\343\202\263", 1, { 0x5C } },
    { "\343\203\257", 1, { 0x5D } },
    { "\343\203\262", 1, { 0x5E } },
    { "\343\203\263", 1, { 0x5F } },
    { "\343\202\265", 1, { 0x60 } },
    { "\343\202\267", 1, { 0x61 } },
    { "\343\202\271", 1, { 0x62 } },
    { "\343\202\273", 1, { 0x63 } },
    { "\343\202\275", 1, { 0x64 } },
    { "\343\202\254", 1, { 0x65 } },
    { "\343\202\256", 1, { 0x66 } },
    { "\343\202\260", 1, { 0x67 } },
    { "\343\202\277", 1, { 0x68 } },
    { "\343\203\201", 1, { 0x69 } },
    { "\343\203\204", 1, { 0x6A } },
    { "\343\203\206", 1, { 0x6B } },
    { "\343\203\210", 1, { 0x6C } },
    { "\343\202\262", 1, { 0x6D } },
    { "\343\202\264", 1, { 0x6E } },
    { "\343\202\266", 1, { 0x6F } },
    { "\343\203\212", 1, { 0x70 } },
    { "\343\203\213", 1, { 0x71 } },
    { "\343\203\214", 1, { 0x72 } },
    { "\343\203\215", 1, { 0x73 } },
    { "\343\203\216", 1, { 0x74 } },
    { "\343\202\270", 1, { 0x75 } },
    { "\343\202\272", 1, { 0x76 } },
    { "\343\202\274", 1, { 0x77 } },
    { "\343\203\217", 1, { 0x78 } },
    { "\343\203\222", 1, { 0x79 } },
    { "\343\203\225", 1, { 0x7A } },
    { "\343\203\230", 1, { 0x7B } },
    { "\343\203\233", 1, { 0x7C } },
    { "\343\202\276", 1, { 0x7D } },
    { "\343\203\200", 1, { 0x7E } },
    { "\343\203\236", 1, { 0x80 } },
    { "\343\203\237", 1, { 0x81 } },
    { "\343\203\240", 1, { 0x82 } },
    { "\343\203\241", 1, { 0x83 } },
    { "\343\203\242", 1, { 0x84 } },
    { "\343\203\205", 1, { 0x85 } },
    { "\343\203\207", 1, { 0x86 } },
    { "\343\203\211", 1, { 0x87 } },
    { "\343\203\251", 1, { 0x88 } },
    { "\343\203\252", 1, { 0x89 } },
    { "\343\203\253", 1, { 0x8A } },
    { "\343\203\254", 1, { 0x8B } },
    { "\343\203\255", 1, { 0x8C } },
    { "\343\203\220", 1, { 0x8D } },
    { "\343\203\223", 1, { 0x8E } },
    { "\343\203\226", 1, { 0x8F } },
    { "\343\203\231", 1, { 0x90 } },
    { "\343\203\234", 1, { 0x91 } },
    { "\343\203\221", 1, { 0x92 } },
    { "\343\203\224", 1, { 0x93 } },
    { "\343\203\227", 1, { 0x94 } },
    { "\343\203\232", 1, { 0x95 } },
    { "\343\203\235", 1, { 0x96 } },
    { "\343\203\243", 1, { 0x97 } },
    { "\343\203\245", 1, { 0x98 } },
    { "\343\203\247", 1, { 0x99 } },
    { "\343\203\203", 1, { 0x9A } },
    { "\343\202\241", 1, { 0x9B } },
    { "\343\202\243", 1, { 0x9C } },
    { "\343\202\245", 1, { 0x9D } },
    { "\343\202\247", 1, { 0x9E } },
    { "\343\202\251", 1, { 0x9F } }

};

static int u8len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

static int mb_len(const char *s, int n)
{
    int k = 0;
    for (int i = 0; i < n; i += u8len((unsigned char)s[i])) k++;
    return k;
}

/* Byte length of the first `chars` code points of s[0..n). */
static int mb_prefix(const char *s, int n, int chars)
{
    int i = 0;
    while (i < n && chars-- > 0) i += u8len((unsigned char)s[i]);
    return i < n ? i : n;
}

/* Dialog::charToHex for one UTF-8 character. */
static int char_to_hex(const char *c, int n, uint8_t *out)
{
    if (n == 1) {
        if (*c >= '0' && *c <= '9') { out[0] = (uint8_t)(*c - '0' + 0xA0); return 1; }
        if (*c >= 'A' && *c <= 'Z') { out[0] = (uint8_t)(*c - 'A' + 0xAA); return 1; }
        if (*c >= 'a' && *c <= 'z') { out[0] = (uint8_t)(*c + 0x6F); return 1; }
    }
    for (size_t g = 0; g < sizeof GLYPHS / sizeof *GLYPHS; g++)
        if ((int)strlen(GLYPHS[g].ch) == n && !memcmp(GLYPHS[g].ch, c, (size_t)n)) {
            memcpy(out, GLYPHS[g].b, GLYPHS[g].n);
            return GLYPHS[g].n;
        }
    out[0] = 0xFF;
    return 1;
}

static bool is_trim(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0' || c == '\x0B'; }

/* PHP's wordwrap($s, $width, "\n", false): the single-character break fast path of ext/standard/string.c. */
static void php_wordwrap(char *s, int n, int width)
{
    int laststart = 0, lastspace = 0;
    for (int cur = 0; cur < n; cur++) {
        if (s[cur] == '\n') {
            laststart = lastspace = cur + 1;
        } else if (s[cur] == ' ') {
            if (cur - laststart >= width) {
                s[cur] = '\n';
                laststart = cur + 1;
            }
            lastspace = cur;
        } else if (cur - laststart >= width && laststart != lastspace) {
            s[lastspace] = '\n';
            laststart = lastspace + 1;
        }
    }
}

/* app/Helpers/str.php mb_wordwrap for one line (no "\n" in it): PHP's wordwrap when it is all ASCII, else its own
   word loop. Writes the result to out (NUL-terminated) and returns its length. */
static int mb_wordwrap(const char *s, int n, int width, char *out, int cap)
{
    if (mb_len(s, n) == n) {
        if (n >= cap) n = cap - 1;
        memcpy(out, s, (size_t)n);
        out[n] = 0;
        php_wordwrap(out, n, width);
        return n;
    }
    while (n > 0 && is_trim(s[n - 1])) n--;   /* rtrim, kept even when the line is short enough */
    if (mb_len(s, n) <= width) {
        memcpy(out, s, (size_t)n);
        out[n] = 0;
        return n;
    }
    char actual[2100];
    int an = 0, on = 0;
    for (int i = 0; i <= n;) {   /* explode(' ', $line) */
        int j = i;
        while (j < n && s[j] != ' ') j++;
        const char *word = s + i;
        int wn = j - i;
        if (mb_len(actual, an) + mb_len(word, wn) <= width) {
            memcpy(actual + an, word, (size_t)wn);
            an += wn;
            actual[an++] = ' ';
        } else {
            if (an) {
                int k = an;
                while (k > 0 && is_trim(actual[k - 1])) k--;
                memcpy(out + on, actual, (size_t)k);
                on += k;
                out[on++] = '\n';
            }
            memcpy(actual, word, (size_t)wn);
            an = wn;
            actual[an++] = ' ';
        }
        i = j + 1;
    }
    int a = 0, b = an;   /* trim($actual) */
    while (a < b && is_trim(actual[a])) a++;
    while (b > a && is_trim(actual[b - 1])) b--;
    memcpy(out + on, actual + a, (size_t)(b - a));
    on += b - a;
    out[on] = 0;
    (void)cap;
    return on;
}

typedef struct { const char *cmd; uint8_t n; uint8_t b[13]; } Command;

static const Command COMMANDS[] = {
    { "{SPEED0}", 2, { 0xFC, 0x00 } }, { "{SPEED2}", 2, { 0xFC, 0x02 } }, { "{SPEED6}", 2, { 0xFC, 0x06 } },
    { "{PAUSE1}", 3, { 0xFE, 0x78, 0x01 } }, { "{PAUSE3}", 3, { 0xFE, 0x78, 0x03 } },
    { "{PAUSE5}", 3, { 0xFE, 0x78, 0x05 } }, { "{PAUSE7}", 3, { 0xFE, 0x78, 0x07 } },
    { "{PAUSE9}", 3, { 0xFE, 0x78, 0x09 } }, { "{INPUT}", 1, { 0xFA } }, { "{CHOICE}", 2, { 0xFE, 0x68 } },
    { "{ITEMSELECT}", 2, { 0xFE, 0x69 } }, { "{CHOICE2}", 2, { 0xFE, 0x71 } }, { "{CHOICE3}", 2, { 0xFE, 0x72 } },
    { "{C:GREEN}", 3, { 0xFE, 0x77, 0x07 } }, { "{C:YELLOW}", 3, { 0xFE, 0x77, 0x02 } },
    { "{HARP}", 3, { 0xFE, 0x79, 0x2D } }, { "{MENU}", 3, { 0xFE, 0x6D, 0x00 } },
    { "{BOTTOM}", 3, { 0xFE, 0x6D, 0x01 } }, { "{NOBORDER}", 3, { 0xFE, 0x6B, 0x02 } },
    { "{CHANGEPIC}", 4, { 0xFE, 0x67, 0xFE, 0x67 } }, { "{CHANGEMUSIC}", 2, { 0xFE, 0x67 } },
    { "{INTRO}", 13, { 0xFE, 0x6E, 0x00, 0xFE, 0x77, 0x07, 0xFC, 0x03, 0xFE, 0x6B, 0x02, 0xFE, 0x67 } },
    { "{IBOX}", 9, { 0xFE, 0x6B, 0x02, 0xFE, 0x77, 0x07, 0xFC, 0x03, 0xF7 } },
};

#define MAX_LINES 512
#define WORK 8192

int z3_dialog_encode(const char *s, bool pause, uint8_t *out)
{
    static char text[WORK];          /* every line after wrapping, NUL-separated */
    static int line_at[MAX_LINES], line_n[MAX_LINES];
    static uint8_t buf[WORK];
    int nl = 0, used = 0;
    for (const char *p = s;;) {      /* explode("\n"), then mb_wordwrap each piece and explode again */
        int n = (int)strcspn(p, "\n");
        char wrapped[2048];
        int wn = mb_wordwrap(p, n < 2000 ? n : 2000, 19, wrapped, sizeof wrapped);
        for (int a = 0;;) {
            int b = a;
            while (b < wn && wrapped[b] != '\n') b++;
            if (nl < MAX_LINES && used + (b - a) + 1 < WORK) {
                memcpy(text + used, wrapped + a, (size_t)(b - a));
                line_at[nl] = used;
                line_n[nl++] = b - a;
                used += b - a;
                text[used++] = 0;
            }
            if (b >= wn) break;
            a = b + 1;
        }
        if (!p[n]) break;
        p += n + 1;
    }

    int len = 0, i = 0;
    bool pad_out = false;
    buf[len++] = 0xFB;
    int line_count = line_n[nl - 1] > 0 && text[line_at[nl - 1]] == '{' ? nl - 1 : nl;
    for (int l = 0; l < nl; l++) {
        const char *line = text + line_at[l];
        int n = line_n[l], cut = mb_prefix(line, n, 19);   /* mb_substr($line, 0, 19) */
        if (cut > 0 && line[0] == '{') {
            int a = 0, b = n;   /* trim($line) */
            while (a < b && is_trim(line[a])) a++;
            while (b > a && is_trim(line[b - 1])) b--;
            if (b - a == 8 && !memcmp(line + a, "{NOTEXT}", 8)) {
                static const uint8_t notext[] = { 0xFB, 0xFE, 0x6E, 0x00, 0xFE, 0x6B, 0x04 };
                memcpy(out, notext, sizeof notext);
                return (int)sizeof notext;
            }
            for (size_t c = 0; c < sizeof COMMANDS / sizeof *COMMANDS; c++)
                if ((int)strlen(COMMANDS[c].cmd) == b - a && !memcmp(COMMANDS[c].cmd, line + a, (size_t)(b - a))) {
                    memcpy(buf + len, COMMANDS[c].b, COMMANDS[c].n);
                    len += COMMANDS[c].n;
                    if (c == sizeof COMMANDS / sizeof *COMMANDS - 2) pad_out = true;   /* {INTRO} */
                }
            line_count--;
            continue;
        }
        if (i == 1) buf[len++] = 0xF8;                             /* row 2 */
        else if (i >= 2) buf[len++] = i >= 3 && i < nl ? 0xF6 : 0xF9;   /* scroll, or row 3 */
        int chars = 0;
        for (int k = 0; k < cut; chars++) {
            int cn = u8len((unsigned char)line[k]);
            if (k + cn > cut) cn = cut - k;
            if (len + 3 < WORK) len += char_to_hex(line + k, cn, buf + len);
            k += cn;
        }
        if (pad_out && i < 3)                                      /* array_pad($line_chars, 19, ' ') */
            for (; chars < 19 && len < WORK; chars++) buf[len++] = 0xFF;
        i++;
        if (pause && i % 3 == 0 && line_count > i && len < WORK) buf[len++] = 0xFA;   /* wait for input */
    }
    if (len > Z3_DIALOG_MAX) len = Z3_DIALOG_MAX;
    memcpy(out, buf, (size_t)len);
    return len;
}

/* ------------------------------------------------------------------ Text::getByteArray */

int z3_text_table(const Z3Texts *t, uint8_t *out)
{
    static uint8_t enc[Z3_DIALOG_MAX];
    int len = 0;
    for (int k = 0; k < Z3_TXT_COUNT; k++) {
        const uint8_t *b = Z3_TEXT_DATA + Z3_TEXT_OFF[k];
        int n = Z3_TEXT_OFF[k + 1] - Z3_TEXT_OFF[k];
        if (t->text[k] >= 0) {
            n = z3_dialog_encode(t->pool + t->text[k], true, enc);
            b = enc;
        }
        if (len + n > Z3_TEXT_MAX) return -1;
        memcpy(out + len, b, (size_t)n);
        len += n;
    }
    return len;
}

/* ------------------------------------------------------------------ Credits */

enum { SMALL, SMALL_ALT, LARGE };
typedef struct { uint8_t type, x, y; const char *text; } CreditPart;
typedef struct { CreditPart part[3]; uint8_t n; } CreditScene;

static const CreditScene SCENES[Z3_CREDIT_COUNT] = {
    { { { SMALL, 5, 19, "The return of the King" }, { LARGE, 9, 23, "Hyrule Castle" } }, 2 },
    { { { SMALL, 8, 19, "The loyal priest" }, { LARGE, 11, 23, "Sanctuary" } }, 2 },
    { { { SMALL, 4, 19, "Sahasralah's Homecoming" }, { LARGE, 9, 23, "Kakariko Town" } }, 2 },
    { { { SMALL, 4, 19, "vultures rule the desert" }, { LARGE, 9, 23, "Desert Palace" } }, 2 },
    { { { SMALL, 4, 19, "the bully makes a friend" }, { LARGE, 9, 23, "Mountain Tower" } }, 2 },
    { { { SMALL, 6, 19, "your uncle recovers" }, { LARGE, 11, 23, "Your House" } }, 2 },
    { { { SMALL, 6, 19, "finger webs for sale" }, { LARGE, 8, 23, "Zora's Waterfall" } }, 2 },
    { { { SMALL, 4, 19, "the witch and assistant" }, { LARGE, 11, 23, "Magic Shop" } }, 2 },
    { { { SMALL, 8, 19, "twin lumberjacks" }, { LARGE, 9, 23, "Woodsmen's Hut" } }, 2 },
    { { { SMALL, 4, 19, "ocarina boy plays again" }, { LARGE, 9, 23, "Haunted Grove" } }, 2 },
    { { { SMALL, 4, 19, "venus, queen of faeries" }, { LARGE, 10, 23, "Wishing Well" } }, 2 },
    { { { SMALL, 4, 19, "the dwarven swordsmiths" }, { LARGE, 12, 23, "Smithery" } }, 2 },
    { { { SMALL, 6, 19, "the bug-catching kid" }, { LARGE, 9, 23, "Kakariko Town" } }, 2 },
    { { { SMALL, 8, 19, "the lost old man" }, { LARGE, 9, 23, "Death Mountain" } }, 2 },
    { { { SMALL, 8, 19, "the forest thief" }, { LARGE, 11, 23, "Lost Woods" } }, 2 },
    { { { SMALL, 6, 19, "and the master sword" }, { SMALL_ALT, 8, 21, "sleeps again..." }, { LARGE, 12, 23, "Forever!" } }, 3 },
};

typedef struct { uint8_t *d; int n, cap; } Out;

static void put(Out *o, int b) { if (o->n < o->cap) o->d[o->n] = (uint8_t)b; o->n++; }

/* Credits::getHeader: big-endian (position << 16 | length * 2 - 1). */
static void header(Out *o, int x, int y, int length)
{
    uint32_t v = (uint32_t)(0x6000 | (y >> 5 << 11) | ((y & 0x1F) << 5) | (x >> 5 << 10) | (x & 0x1F)) << 16
        | (uint32_t)(length * 2 - 1);
    put(o, (int)(v >> 24)); put(o, (int)(v >> 16 & 0xFF)); put(o, (int)(v >> 8 & 0xFF)); put(o, (int)(v & 0xFF));
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static int credit_small(char c)
{
    c = lower(c);
    if (c >= 'a' && c <= 'z') return c - 0x47;
    switch (c) {
    case ',': return 0x34;
    case '.': return 0x37;
    case '-': return 0x36;
    case '\'': return 0x35;
    default: return 0x9F;
    }
}

/* The apostrophe and comma rows of getSmallConverted: every other byte a space, the marks at their columns. */
static void small_marks(Out *o, const char *text, int n, int x, int y, char mark, int dy)
{
    int a = 0, b = n;
    while (a < b && text[a] != mark) a++;
    while (b > a && text[b - 1] != mark) b--;
    if (a == b) return;
    header(o, x + a, y + dy, b - a);
    for (int i = a; i < b; i++) put(o, text[i] == mark ? credit_small(mark == '\'' ? ',' : '\'') : 0x9F);
}

static void credit_part(Out *o, const CreditPart *p, const char *text, int x)
{
    int n = (int)strlen(text);
    switch (p->type) {
    case SMALL:
        header(o, x, p->y, n);
        for (int i = 0; i < n; i++) put(o, credit_small(text[i]));
        small_marks(o, text, n, x, p->y, '\'', -1);
        small_marks(o, text, n, x, p->y, ',', 1);
        break;
    case SMALL_ALT:
        header(o, x, p->y, n);
        for (int i = 0; i < n; i++) {
            char c = lower(text[i]);
            put(o, c >= 'a' && c <= 'z' ? c - 0x29 : c == '.' ? 0x52 : 0x9F);
        }
        break;
    default:
        header(o, x, p->y, n);
        for (int i = 0; i < n; i++) {
            char c = lower(text[i]);
            put(o, c >= 'a' && c <= 'z' ? c - 0x4 : c == '\'' ? 0xD9 : c == '!' ? 0xE5 : c == '_' ? 0xDE : 0x9F);
        }
        header(o, x, p->y + 1, n);
        for (int i = 0; i < n; i++) {
            char c = lower(text[i]);
            put(o, c >= 'a' && c <= 'z' ? c + 0x22 : c == '\'' ? 0xEC : c == '!' ? 0xF8 : c == '_' ? 0xF1 : 0x9F);
        }
        break;
    }
}

int z3_credits_data(const Z3Texts *t, uint8_t *data, int cap, uint16_t ptrs[Z3_CREDIT_COUNT + 1])
{
    Out o = { data, 0, cap };
    for (int s = 0; s < Z3_CREDIT_COUNT; s++) {
        ptrs[s] = (uint16_t)o.n;
        for (int p = 0; p < SCENES[s].n; p++) {
            const CreditPart *part = &SCENES[s].part[p];
            if (p == 0 && t->credit[s] >= 0) {   /* updateCreditLine: first 32 bytes, centred */
                char line[33];
                size_t n = strlen(t->pool + t->credit[s]);
                if (n > 32) n = 32;
                memcpy(line, t->pool + t->credit[s], n);
                line[n] = 0;
                credit_part(&o, part, line, (32 - (int)n) / 2);
            } else {
                credit_part(&o, part, part->text, part->x);
            }
        }
    }
    ptrs[Z3_CREDIT_COUNT] = (uint16_t)o.n;
    return o.n < cap ? o.n : cap;
}

/* ------------------------------------------------------------------ Randomizer::setTexts, randomizeCredits */

/* Arr::first(fy_shuffle($array)) for an array of n: n draws, the index that ends up first. */
static int shuffle_first(Z3Rng *r, int n)
{
    int idx[256];
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) idx[i] = i;
    z3_rng_shuffle(r, idx, n);
    return n ? idx[0] : 0;
}

static const char *pick_str(Z3Rng *r, int pool)
{
    return Z3_STRS[pool].s[shuffle_first(r, Z3_STRS[pool].n)];
}

static const char *pick_name(Z3Rng *r, const Z3HintName *h)
{
    return h->is_array ? h->s[shuffle_first(r, h->n)] : h->s[0];
}

/* World::getLocationsWithItem($item)->first(), -1 for none. The setTexts bow swap is another Item object. */
static int first_with(const Z3Seed *s, int item)
{
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (s->w.item_at[l] == item && z3_loc_exists(&s->w, l) && l != s->bow_alt_loc) return l;
    return -1;
}

static const char *region_name(int loc) { return loc < 0 ? "" : Z3_REGIONS[Z3_LOCS[loc].region].name; }

static void set_text(Z3Seed *s, int key, const char *str) { z3_texts_set(&s->tx, key, str); }

static void set_texts(Z3Seed *s, Z3Rng *r)
{
    const Z3Cfg *c = &s->w.cfg;
    char buf[512];
    /* World\Standard with spoil.BootsLocation off draws the same as Open. */
    set_text(s, Z3_TXT_uncle_leaving_text, pick_str(r, Z3_STRS_UNCLE));
    snprintf(buf, sizeof buf, "Want something\nfor free? Go\nearn the green\npendant in\n%s\nand I'll give\nyou something.",
             region_name(first_with(s, Z3_ITEM_PendantOfCourage)));
    set_text(s, Z3_TXT_sahasrahla_bring_courage, buf);
    snprintf(buf, sizeof buf, "bring me the\ncrystals from\n%s\nand\n%s\nso I can make\na big bomb!",
             region_name(first_with(s, Z3_ITEM_Crystal5)), region_name(first_with(s, Z3_ITEM_Crystal6)));
    set_text(s, Z3_TXT_bomb_shop, buf);
    set_text(s, Z3_TXT_blind_by_the_light, pick_str(r, Z3_STRS_BLIND));
    set_text(s, Z3_TXT_kakariko_tavern_fisherman, pick_str(r, Z3_STRS_TAVERN_MAN));
    set_text(s, Z3_TXT_ganon_fall_in, pick_str(r, Z3_STRS_GANON_1));
    set_text(s, Z3_TXT_ganon_phase_3_alt, "Got wax in\nyour ears?\nI cannot die!");

    int silvers = first_with(s, Z3_ITEM_SilverArrowUpgrade);
    if (silvers < 0) silvers = first_with(s, Z3_ITEM_BowAndSilverArrows);
    int bows[8], nb = 0, picked[2], np = 0;
    for (int l = 0; l < Z3_LOC_COUNT && nb < 8; l++)
        if (s->w.item_at[l] == Z3_ITEM_ProgressiveBow && z3_loc_exists(&s->w, l)) bows[nb++] = l;
    while (np < 2 && nb > 0) {   /* randomCollection(2) */
        int k = z3_rng_int(r, 0, nb - 1);
        picked[np++] = bows[k];
        memmove(bows + k, bows + k + 1, (size_t)(--nb - k) * sizeof *bows);
    }
    int of_bow = c->item_pool == Z3_ITEM_POOL_NORMAL ? 2 : 1;   /* item.overflow.count.Bow */
    if (c->mode_weapons == Z3_MODE_WEAPONS_SWORDLESS) of_bow = 2;
    static const char *const gt = "Did you find\nthe arrows in\nMy tower?";
    if (np >= 2 && of_bow >= 2) {
        int first = picked[1], second = picked[0];   /* pop() takes the last drawn first */
        if (Z3_LOCS[first].region == Z3_REG_Ganons_Tower) set_text(s, Z3_TXT_ganon_phase_3_no_silvers, gt);
        else {
            snprintf(buf, sizeof buf, "Did you find\nthe arrows in\n%s", region_name(first));
            set_text(s, Z3_TXT_ganon_phase_3_no_silvers, buf);
        }
        s->bow_alt_loc = (int16_t)first;   /* setItem(new Item\Bow('ProgressiveBow', [0x65])) */
        if (Z3_LOCS[second].region == Z3_REG_Ganons_Tower) set_text(s, Z3_TXT_ganon_phase_3_no_silvers_alt, gt);
        else {
            snprintf(buf, sizeof buf, "Did you find\nthe arrows in\n%s", region_name(second));
            set_text(s, Z3_TXT_ganon_phase_3_no_silvers_alt, buf);
        }
    } else if (silvers >= 0) {
        if (Z3_LOCS[silvers].region == Z3_REG_Ganons_Tower) snprintf(buf, sizeof buf, "%s", gt);
        else snprintf(buf, sizeof buf, "Did you find\nthe arrows in\n%s", region_name(silvers));
        set_text(s, Z3_TXT_ganon_phase_3_no_silvers, buf);
        set_text(s, Z3_TXT_ganon_phase_3_no_silvers_alt, buf);
    } else {
        const char *fake = pick_str(r, Z3_STRS_GANON_PHASE_3_NO_SILVERS);
        if (c->item_pool == Z3_ITEM_POOL_CROWD_CONTROL) fake = "Chat said no\nto Silvers.\nIt's over Hero";
        set_text(s, Z3_TXT_ganon_phase_3_no_silvers, fake);
        set_text(s, Z3_TXT_ganon_phase_3_no_silvers_alt, fake);
    }

    if (c->crystals_tower < 7) {
        snprintf(buf, sizeof buf, c->crystals_tower == 1 ? "You need %d crystal to enter." : "You need %d crystals to enter.",
                 c->crystals_tower);
        set_text(s, Z3_TXT_sign_ganons_tower, buf);
    }
    static const char *const dingus = "You think you\nare ready to\nface me?\n\nI will not die\n\nunless you\ncomplete your\ngoals. Dingus!";
    bool one = c->crystals_ganon == 1;
    switch (c->goal) {
    case Z3_GOAL_GANON:
        snprintf(buf, sizeof buf, one ? "To beat Ganon you must collect %d Crystal and defeat his minion at the top of his tower."
                                      : "To beat Ganon you must collect %d Crystals and defeat his minion at the top of his tower.",
                 c->crystals_ganon);
        set_text(s, Z3_TXT_sign_ganon, buf);
        set_text(s, Z3_TXT_ganon_fall_in_alt, dingus);
        break;
    case Z3_GOAL_FAST_GANON:
        snprintf(buf, sizeof buf, one ? "You need %d Crystal to beat Ganon." : "You need %d Crystals to beat Ganon.", c->crystals_ganon);
        set_text(s, Z3_TXT_sign_ganon, buf);
        set_text(s, Z3_TXT_ganon_fall_in_alt, dingus);
        break;
    case Z3_GOAL_GANONHUNT:
        snprintf(buf, sizeof buf, "To beat Ganon you must collect %d Triforce Pieces.", c->item_Goal_Required);
        set_text(s, Z3_TXT_sign_ganon, buf);
        set_text(s, Z3_TXT_ganon_fall_in_alt, dingus);
        break;
    case Z3_GOAL_PEDESTAL:
        set_text(s, Z3_TXT_ganon_fall_in_alt, "You cannot\nkill me. You\nshould go for\nyour real goal\nIt's on the\npedestal.\n\nYou dingus!\n");
        set_text(s, Z3_TXT_sign_ganon, "You need to get to the pedestal... Dingus!");
        break;
    case Z3_GOAL_TRIFORCE_HUNT:
        set_text(s, Z3_TXT_ganon_fall_in_alt, "So you thought\nyou could come\nhere and beat\nme? I have\nhidden the\nTriforce\npieces well.\nWithout them,\nyou can't win!");
        set_text(s, Z3_TXT_sign_ganon, "Go find the Triforce pieces... Dingus!");
        snprintf(buf, sizeof buf, "Hello @. I\nam Murahdahla, brother of\nSahasrahla and Aginah. Behold the power of\ninvisibility.\n\n\n\n"
                 "\xE2\x80\xA6 \xE2\x80\xA6 \xE2\x80\xA6\n\nWait! you can see me? I knew I should have\nhidden in  a hollow tree. "
                 "If you bring\n%d triforce pieces, I can reassemble it.", c->item_Goal_Required);
        set_text(s, Z3_TXT_murahdahla, buf);
        break;
    case Z3_GOAL_DUNGEONS:
        set_text(s, Z3_TXT_sign_ganon, "You need to defeat all of Ganon's bosses.");
        set_text(s, Z3_TXT_ganon_fall_in_alt, dingus);
        break;
    case Z3_GOAL_COMPLETIONIST:
        set_text(s, Z3_TXT_sign_ganon, "You need to collect EVERY item and defeat EVERY boss.");
        /* fall through */
    default:
        set_text(s, Z3_TXT_ganon_fall_in_alt, dingus);
        break;
    }
    snprintf(buf, sizeof buf, "{NOBORDER}\n%s", pick_str(r, Z3_STRS_TRIFORCE));
    set_text(s, Z3_TXT_end_triforce, buf);
}

static const char *pick_of(Z3Rng *r, const char *const *list, int n) { return list[shuffle_first(r, n)]; }
#define PICK(r, ...) pick_of(r, (const char *const[]){ __VA_ARGS__ }, (int)(sizeof (const char *const[]){ __VA_ARGS__ } / sizeof(char *)))

static void randomize_credits(Z3Seed *s, Z3Rng *r)
{
    Z3Texts *t = &s->tx;
    char buf[64];
    z3_texts_credit(t, Z3_CREDIT_castle, PICK(r, "the return of the king", "fellowship of the ring", "the two towers"));
    z3_texts_credit(t, Z3_CREDIT_sanctuary, PICK(r, "the loyal priest", "read a book", "sits in own pew", "heal plz"));
    snprintf(buf, sizeof buf, "%s's homecoming", PICK(r,
        "sahasralah", "sabotaging", "sacahuista", "sacahuiste", "saccharase", "saccharide", "saccharify",
        "saccharine", "saccharins", "sacerdotal", "sackcloths", "salmonella", "saltarelli", "saltarello",
        "saltations", "saltbushes", "saltcellar", "saltshaker", "salubrious", "sandgrouse", "sandlotter",
        "sandstorms", "sandwiched", "sauerkraut", "schipperke", "schismatic", "schizocarp", "schmalzier",
        "schmeering", "schmoosing", "shibboleth", "shovelnose", "sahananana", "sarararara", "salamander",
        "sharshalah", "shahabadoo", "sassafrass", "saddlebags", "sandalwood", "shagadelic", "sandcastle",
        "saltpeters", "shabbiness", "shlrshlrsh", "sassyralph", "sallyacorn", "sahasrahbot", "sasharalla"));
    z3_texts_credit(t, Z3_CREDIT_kakariko, buf);
    z3_texts_credit(t, Z3_CREDIT_lumberjacks, PICK(r, "twin lumberjacks", "fresh flapjacks", "two woodchoppers",
        "double lumberman", "lumberclones", "woodfellas", "dos axes"));
    if (z3_rng_int(r, 0, 1) == 1) z3_texts_credit(t, Z3_CREDIT_smithy, "the dwarven breadsmiths");
    z3_texts_credit(t, Z3_CREDIT_bridge, PICK(r, "the lost old man", "gary the old man", "Your ad here"));
    z3_texts_credit(t, Z3_CREDIT_woods, PICK(r, "the forest thief", "dancing pickles", "flying vultures"));
    z3_texts_credit(t, Z3_CREDIT_well, PICK(r, "venus. queen of faeries", "Venus was her name", "I'm your Venus",
        "Yeah, baby, she's got it", "Venus, I'm your fire", "Venus, At your desire", "Venus Love Chain",
        "Venus Crescent Beam"));
}

/* ------------------------------------------------------------------ HintService::applyHints */

/* The item a location's texts name: Location::getItem()->getTarget(). */
static int target_of(const Z3Seed *s, int item)
{
    return item == Z3_ITEM_UncleSword && s->uncle_target != Z3_ITEM_NONE ? s->uncle_target : item;
}

/* Location::getHint(): ucfirst("$item_name $location_name"), each drawn from its array if it is one. */
static bool get_hint(const Z3Seed *s, Z3Rng *r, int loc, char *out, size_t cap)
{
    int item = s->w.item_at[loc];
    if (item == Z3_ITEM_NONE) return false;
    const char *iname = pick_name(r, &Z3_HINT_ITEM[target_of(s, item)]);
    const char *lname = pick_name(r, &Z3_HINT_LOC[loc]);
    snprintf(out, cap, "%s %s", iname, lname);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
    return true;
}

static bool hint_location_kind(int loc)   /* not Medallion, Fountain, Prize or Trade */
{
    const char *k = Z3_LOCS[loc].kind;
    return strcmp(k, "Medallion") && strcmp(k, "Fountain") && strncmp(k, "Prize", 5) && strcmp(k, "Trade");
}

/* World::getLocationsWithItem($item): every location holding that Item object. */
static int locations_with(const Z3Seed *s, int item, int *out)
{
    int n = 0;
    for (int l = 0; l < Z3_LOC_COUNT; l++)
        if (z3_loc_exists(&s->w, l) && s->w.item_at[l] == item && l != s->bow_alt_loc) out[n++] = l;
    return n;
}

static void apply_hints(Z3Seed *s, Z3Rng *r)
{
    const Z3Cfg *c = &s->w.cfg;
    static const int16_t tiles0[15] = {
        Z3_TXT_telepathic_tile_eastern_palace, Z3_TXT_telepathic_tile_tower_of_hera_floor_4,
        Z3_TXT_telepathic_tile_spectacle_rock, Z3_TXT_telepathic_tile_swamp_entrance,
        Z3_TXT_telepathic_tile_thieves_town_upstairs, Z3_TXT_telepathic_tile_misery_mire,
        Z3_TXT_telepathic_tile_palace_of_darkness, Z3_TXT_telepathic_tile_desert_bonk_torch_room,
        Z3_TXT_telepathic_tile_castle_tower, Z3_TXT_telepathic_tile_ice_large_room, Z3_TXT_telepathic_tile_turtle_rock,
        Z3_TXT_telepathic_tile_ice_entrace, Z3_TXT_telepathic_tile_ice_stalfos_knights_room,
        Z3_TXT_telepathic_tile_tower_of_hera_entrance, Z3_TXT_telepathic_tile_south_east_darkworld_cave,
    };
    /* -1 stands for the three pairs: World::getLocations() filtered by getName() ("Name:1") in an array of bare
       names finds nothing, so LocationCollection::getHint() is null and the pick is skipped. */
    static const int16_t picks0[10] = { Z3_LOC_Sahasrahla, Z3_LOC_Mimic_Cave, Z3_LOC_Catfish, Z3_LOC_Graveyard_Ledge,
        Z3_LOC_Purple_Chest, Z3_LOC_Tower_of_Hera_Big_Key_Chest, Z3_LOC_Swamp_Palace_Big_Chest, -1, -1, -1 };
    int tiles[15], locs[10], nt = 15;
    char hint[512];
    for (int i = 0; i < 15; i++) tiles[i] = tiles0[i];
    for (int i = 0; i < 10; i++) locs[i] = picks0[i];
    z3_rng_shuffle(r, tiles, 15);
    z3_rng_shuffle(r, locs, 10);

    int found[Z3_LOC_COUNT];
    if (c->region_wildBigKeys && locations_with(s, Z3_ITEM_BigKeyA2, found) && get_hint(s, r, found[0], hint, sizeof hint))
        set_text(s, tiles[--nt], hint);
    if (locations_with(s, Z3_ITEM_PegasusBoots, found) && get_hint(s, r, found[0], hint, sizeof hint))
        set_text(s, tiles[--nt], hint);

    int picks[10], np = 10;
    for (int i = 0; i < 10; i++) picks[i] = i;
    for (int i = 0; i < 5; ++i) {
        z3_rng_shuffle(r, picks, np);
        int pick = locs[picks[--np]];
        if (pick < 0 || !get_hint(s, r, pick, hint, sizeof hint)) continue;
        set_text(s, tiles[--nt], hint);
    }

    int hintables[256], nh = 0;
    for (int i = 0; i < s->n_adv; i++) {
        int it = s->adv[i], ic = Z3_ITEMS[it].cls;
        if (ic == Z3_IC_Shield || ic == Z3_IC_Key || ic == Z3_IC_Map || ic == Z3_IC_Compass || ic == Z3_IC_Bottle
            || ic == Z3_IC_Sword || (ic == Z3_IC_BigKey && !c->region_wildBigKeys) || it == Z3_ITEM_TenBombs
            || it == Z3_ITEM_HalfMagic || it == Z3_ITEM_BugCatchingNet || it == Z3_ITEM_Powder || it == Z3_ITEM_Mushroom)
            continue;
        hintables[nh++] = it;
    }
    z3_rng_shuffle(r, hintables, nh);
    int hints[4], nhint = 0;
    for (int i = 0; i < nh && i < (nt < 4 ? nt : 4); i++) {   /* array_slice, then random() among each item's places */
        int n = 0, all = locations_with(s, hintables[i], found);
        for (int k = 0; k < all; k++)
            if (hint_location_kind(found[k])) found[n++] = found[k];
        if (n) hints[nhint++] = found[z3_rng_int(r, 0, n - 1)];
    }

    int with[Z3_LOC_COUNT], nw = 0;   /* getLocationsWithItem() minus the kinds and dungeon items */
    for (int l = 0; l < Z3_LOC_COUNT; l++) {
        int it = s->w.item_at[l];
        if (it == Z3_ITEM_NONE || !z3_loc_exists(&s->w, l) || !hint_location_kind(l)) continue;
        int ic = Z3_ITEMS[it].cls;
        if (ic == Z3_IC_Key || ic == Z3_IC_Map || ic == Z3_IC_Compass || (ic == Z3_IC_BigKey && !c->region_wildBigKeys))
            continue;
        with[nw++] = l;
    }
    int lo = (nt - nhint) / 2 - 1, hi = nt - nhint - 1;   /* floor((T - H) / 2) - 1; T >= H here */
    int count = z3_rng_int(r, lo, hi);
    int chosen[Z3_LOC_COUNT + 4], nc = 0;
    while (count-- > 0 && nw > 0) {   /* randomCollection */
        int k = z3_rng_int(r, 0, nw - 1);
        chosen[nc++] = with[k];
        memmove(with + k, with + k + 1, (size_t)(--nw - k) * sizeof *with);
    }
    for (int i = 0; i < nhint; i++) {   /* ->merge($hints): keyed by location name, a repeat keeps its first place */
        bool dup = false;
        for (int k = 0; k < nc; k++) dup |= chosen[k] == hints[i];
        if (!dup) chosen[nc++] = hints[i];
    }
    for (int i = 0; i < nt; i++) {
        bool ok = nc > 0 && get_hint(s, r, chosen[--nc], hint, sizeof hint);
        set_text(s, tiles[i], ok ? hint : pick_str(r, Z3_STRS_HINT));
    }
}

void z3_texts_randomize(Z3Seed *s, Z3Rng *r, bool hints)
{
    set_texts(s, r);
    randomize_credits(s, r);
    if (hints) apply_hints(s, r);
    else set_text(s, Z3_TXT_sign_north_of_links_house, "Randomizer v31\n\n>    -veetorp");
}
