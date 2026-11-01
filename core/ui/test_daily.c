/* The daily challenge on the client: the server's answers parse, the attempt survives as text, and the menu set from a
 * challenge's settings builds the very seed the server made (arguments: challenge files written by smz3daily). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smz3_daily.h"
#include "smz3_tracker.h"
#include "smz3_ui_gen.h"
#include "smz3_runlogic.h"

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)n + 1);
    if (fread(t, 1, (size_t)n, f) != (size_t)n) n = 0;
    t[n] = 0;
    fclose(f);
    return t;
}

/* The value of "key":{...} or "key":[...] in a challenge file, copied out. */
static void sub(const char *json, const char *key, char *out, size_t cap)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat) + strlen(pat);
    while (strcmp(key, "settings") && *p != '{' && *p != '[') p = strstr(p, pat) + strlen(pat);   /* "goal" is also a string inside the settings */
    const char *q = p;
    if (*p == '"') { for (q++; *q && *q != '"'; q++) if (*q == '\\') q++; snprintf(out, cap, "%.*s", (int)(q + 1 - p), p); return; }
    int depth = 0, in = 0;
    for (; *q; q++) {
        if (in) { if (*q == '\\') q++; else if (*q == '"') in = 0; continue; }
        if (*q == '"') in = 1;
        else if (*q == '{' || *q == '[') depth++;
        else if ((*q == '}' || *q == ']') && --depth == 0) { q++; break; }
    }
    snprintf(out, cap, "%.*s", (int)(q - p), p);
}

static SmzSeed a, b;

int main(int argc, char **argv)
{
    SmzDailyInfo d;
    /* a window open with this player started in it; times come in the device clock */
    const char *info = "{\"now\":1000000,\"current\":{\"id\":\"2026-10-30-1\",\"opensAt\":900000,\"closesAt\":15300000,\"goal\":\"Beat \\\"Kraid\\\"\","
                       "\"start\":[\"Ice Rod\",\"Plasma Beam\"],\"difficulty\":\"Normal\",\"estimateMin\":59,\"state\":\"started\",\"startedAt\":950000},"
                       "\"next\":{\"id\":\"2026-10-30-2\",\"opensAt\":29700000,\"closesAt\":44100000},\"tokens\":{\"balance\":35,\"streak\":2}}";
    assert(smz_daily_parse_info(info, 5000000, &d) == 0 && d.loaded == 1);
    assert(!strcmp(d.window, "2026-10-30-1") && !strcmp(d.goal, "Beat \"Kraid\"") && !strcmp(d.difficulty, "Normal"));
    assert(d.n_start == 2 && !strcmp(d.start[1], "Plasma Beam") && d.est_min == 59);
    assert(d.state == DAILY_STARTED && d.started_at == 4950000 && d.closes_at == 19300000 && d.next_opens_at == 33700000);
    assert(d.tokens == 35 && d.streak == 2);
    /* between windows, and an answer that is not the server's */
    assert(smz_daily_parse_info("{\"now\":5,\"current\":null,\"next\":{\"opensAt\":10},\"tokens\":{\"balance\":0,\"streak\":0}}", 5, &d) == 0);
    assert(!d.window[0] && d.next_opens_at == 10 && d.loaded == 1);
    assert(smz_daily_parse_info("<html>", 5, &d) < 0 && d.loaded == -1);

    char t[64];
    smz_daily_span(2 * 3600000 + 5 * 60000, t, sizeof t); assert(!strcmp(t, "2h 05m"));
    smz_daily_span(12 * 60000 + 30000, t, sizeof t); assert(!strcmp(t, "12m 30s"));
    smz_daily_span(-5, t, sizeof t); assert(!strcmp(t, "0s"));

    /* the run list's Daily row: between the last slot and the first, its screen starts the attempt */
    {
        static SmzUi u;
        ui_init(&u);
        ui_daily_enable(&u, 1);
        assert(smz_daily_parse_info(info, 5000000, &d) == 0);
        d.state = DAILY_OPEN;
        ui_daily_set(&u, &d, 0);
        ui_input(&u, BTN_UP);
        assert(u.daily_sel);
        ui_input(&u, BTN_DOWN);
        assert(!u.daily_sel && u.slot_cursor == 0);
        ui_input(&u, BTN_UP);
        ui_input(&u, BTN_UP);
        assert(!u.daily_sel && u.slot_cursor == u.slot_count - 1);
        ui_input(&u, BTN_DOWN);
        ui_input(&u, BTN_A);
        assert(u.screen == UI_DAILY && u.daily_cursor == 0);
        ui_draw(&u);
        ui_input(&u, BTN_A);
        assert(u.action == ACT_DAILY && u.screen == UI_SLOTS_SCREEN);
        u.action = ACT_NONE;
        d.state = DAILY_DONE;                 /* finished: nothing to start, Back only */
        ui_daily_set(&u, &d, 1);
        u.daily_sel = 1;
        ui_input(&u, BTN_A);
        assert(u.screen == UI_DAILY && u.daily_cursor == 2);
        ui_input(&u, BTN_LEFT);               /* Info, and back from it */
        assert(u.daily_cursor == 1);
        ui_input(&u, BTN_LEFT);
        assert(u.daily_cursor == 1);
        ui_input(&u, BTN_A);
        assert(u.daily_info);
        ui_draw(&u);
        ui_input(&u, BTN_A);
        assert(!u.daily_info && u.screen == UI_DAILY);
        ui_input(&u, BTN_RIGHT);
        ui_input(&u, BTN_A);
        assert(u.action == ACT_NONE && u.screen == UI_SLOTS_SCREEN);
        ui_draw(&u);
    }

    int checked = 0, mid = 0;
    for (int i = 1; i < argc; i++) {
        char *c = slurp(argv[i]);
        assert(c);
        static char settings[8192], start[512], goal[512], reply[16384], back[8192], text[8192];
        sub(c, "settings", settings, sizeof settings);
        sub(c, "start", start, sizeof start);
        sub(c, "goal", goal, sizeof goal);
        snprintf(reply, sizeof reply, "{\"token\":\"id.inst.daily.w.1000.sig\",\"window\":\"w\",\"seed\":1,\"settings\":%s,\"start\":%s,\"goal\":%s,"
                 "\"startedAt\":1000,\"closesAt\":14401000,\"hints\":[{\"at\":601000,\"text\":\"You will need the Hammer.\"}],\"nextHintAt\":1201000}",
                 settings, start, goal);
        SmzDailyRun r, r2;
        assert(smz_daily_parse_start(reply, 1000, &r, back, sizeof back) == 0);
        assert(back[0] == '{' && strstr(back, "\"challenge\":true") && r.n_hints == 1 && r.next_hint_at == 1201000 && r.closes_at == 14401000 && r.goal[0]);
        assert(smz_daily_clock_from(&r) == 1000);   /* before the game starts the clock counts from Start */
        {
            SmzDailyRun late = r;
            assert(smz_daily_note_began(&late, 1000 + 11 * 60000) == 1 && late.began_at == 1000);   /* resumed long after Start */
            assert(smz_daily_note_began(&r, 61000) == 1 && smz_daily_note_began(&r, 99000) == 0);
        }
        assert(smz_daily_run_text(&r, text, sizeof text) > 0 && smz_daily_run_load(text, &r2) == 0);
        assert(r2.began_at == 61000 && smz_daily_clock_from(&r2) == 61000);
        assert(!strcmp(r2.token, r.token) && r2.started_at == 1000 && r2.n_hints == 1 && !strcmp(r2.hint[0], r.hint[0]) && !strcmp(r2.goal, r.goal) && r2.check[0] && !strcmp(r2.check, r.check));
        assert(smz_daily_parse_hints("{\"hints\":[{\"at\":601000,\"text\":\"a\"},{\"at\":1201000,\"text\":\"b\"}],\"nextHintAt\":null,\"closesAt\":1}", 0, &r) == 0);
        assert(r.n_hints == 2 && r.next_hint_at == 0 && !strcmp(r.hint[1], "b"));

        /* the menu, set as the host does for a challenge, gives the server's placement */
        static SmzUi u;
        ui_init(&u);
        ui_preset(&u, 1);
        ui_apply_run_settings(&u, back);
        SmzSettings s1, s2, e1, e2;
        SmzPatchCfg pc;
        char link[32], samus[32];
        ui_settings(&u, 12345, &s1, &pc, link, samus);
        smz_logic_settings(back, &s2);
        assert(smz_fill_valid(&a, &s1, &e1) == 0 && smz_fill_valid(&b, &s2, &e2) == 0);
        assert(!memcmp(a.world.item_at, b.world.item_at, sizeof a.world.item_at));
        /* and its settings JSON, marked as a challenge, carries the goal the hosts read */
        char js[8192], check[40];
        assert(ui_settings_json(&u, 12345, js, sizeof js) > 0);
        const char *g = strstr(goal, "\"check\":\"") + 9;
        snprintf(check, sizeof check, "%.*s", (int)(strchr(g, '"') - g), g);
        {   /* a player who turned on the reachable checks still gets a plain map in the challenge */
            char *r = strstr(js, "\"showReachable\":false");
            if (r) { memmove(r + 20, r + 21, strlen(r + 21) + 1); memcpy(r + 16, "true", 4); }
        }
        assert(smz_daily_mark_settings(js, sizeof js, check, back) == 0 && strstr(js, "\"challenge\":true") && strstr(js, check));
        assert(strstr(js, "\"showReachable\":false,") && !strstr(js, "\"showReachable\":true"));
        assert(smz_daily_mark_settings(js, sizeof js, check, back) < 0);   /* only once */
        assert(smz_daily_prepared_bosses(js) == smz_daily_prepared_bosses(back));   /* a mid-game start carries its dead bosses */
        if (smz_daily_prepared_bosses(back)) mid++;
        assert(strstr(js, "\"saveStates\":false") && strlen(js) < 3500);   /* SmzRun.settings holds 4096 */
        free(c);
        checked++;
    }
    {   /* the dead bosses of a mid-game start are kept in memory, only once the game runs */
        static uint8_t wram[0x20000], sram[0x8000];
        sram[0x33FE] = 0xFF;
        wram[0x998] = 6;
        assert(smz_prepared_apply(5, wram, sizeof wram, sram, sizeof sram) == 0 && !wram[0xD829]);   /* still loading the file */
        wram[0x998] = 8;
        assert(smz_prepared_apply(5, wram, sizeof wram, sram, sizeof sram) == 1);
        assert(smz_boss_mask(wram, sizeof wram, sram, sizeof sram, 2) == 5 && !(wram[0xD82B] & 1));
        assert(smz_prepared_apply(5, wram, sizeof wram, sram, sizeof sram) == 0);   /* nothing left to change */
        sram[0x33FE] = 0x00;   /* in Zelda: the Super Metroid save copy */
        assert(smz_prepared_apply(8, wram, sizeof wram, sram, sizeof sram) == 1 && smz_boss_mask(wram, sizeof wram, sram, sizeof sram, 1) == 8);
    }
    printf("daily ok (%d challenges rebuilt, %d mid-game)\n", checked, mid);
    return 0;
}
