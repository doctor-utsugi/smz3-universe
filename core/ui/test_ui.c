/* Drives the menu with scripted buttons and checks the settings it produces. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "smz3_ui_gen.h"
#include "smz3_run.h"

int main(void)
{
    static SmzUi u;
    ui_init(&u);
    const char *links[] = {"Zelda", "Pink Link"};
    ui_set_sprites(&u, 0, links, 2);
    ui_set_slot(&u, 1, 1, "Run 2", "Zelda 3/216");
    ui_draw(&u);
    assert(u.cmd_count > 4);

    ui_input(&u, BTN_A);                       /* empty slot 1 -> the mode picker */
    assert(u.screen == UI_MODE && u.active_slot == 0 && u.saved_count >= 5);
    ui_input(&u, BTN_DOWN); ui_input(&u, BTN_DOWN); ui_input(&u, BTN_A);   /* Custom */
    assert(u.screen == UI_CREATE);
    ui_input(&u, BTN_DOWN); ui_input(&u, BTN_DOWN);   /* keysanity (after Game mode) */
    ui_input(&u, BTN_RIGHT);
    ui_input(&u, BTN_RIGHT);                   /* Both -> Zelda only */
    ui_input(&u, BTN_R);                       /* Zelda tab: the sprites are not here any more */
    ui_input(&u, BTN_R);                       /* Metroid tab */
    for (int i = 0; i < 3; i++) ui_input(&u, BTN_DOWN);   /* wall jump (after the jingle) */
    ui_input(&u, BTN_RIGHT);                   /* Hard */
    ui_draw(&u);
    for (int t = 0; t < LEVEL_ITEMS; t++)
        for (int k = 0; UI_TABS[t][k]; k++) assert(strcmp(UI_TABS[t][k], "LinkSprite") && strcmp(UI_TABS[t][k], "SamusSprite"));

    u.level = LEVEL_RUN; u.cursor[0] = 8;      /* "Next" row (the "Helps available" row is hidden unless the mode is Limited) */
    ui_input(&u, BTN_A);
    assert(u.screen == UI_START && u.start_cursor == 3 && u.action == ACT_NONE);
    ui_input(&u, BTN_UP); ui_input(&u, BTN_UP);          /* Link row */
    ui_input(&u, BTN_A);
    assert(u.screen == UI_SPRITES && u.back == UI_START);
    ui_input(&u, BTN_RIGHT);      /* Pink Link (grid, names are sorted) */
    ui_input(&u, BTN_A);
    assert(u.screen == UI_START);
    ui_input(&u, BTN_DOWN); ui_input(&u, BTN_RIGHT); ui_input(&u, BTN_A);   /* Samus: Random (no Samus sprites installed) */
    assert(u.value[UIO_SAMUS] == 0);
    ui_input(&u, BTN_UP); ui_input(&u, BTN_RIGHT); ui_input(&u, BTN_A);     /* Link: Random picks one of the two */
    assert(u.value[UIO_LINK] >= 1 && u.value[UIO_LINK] <= 2);
    u.value[UIO_LINK] = 1;
    {
        SmzSettings s; SmzPatchCfg pc; char link[32], samus[32];
        ui_settings(&u, 1234, &s, &pc, link, samus);
        assert(s.seed == 1234 && s.keysanity == KEYS_Zelda);
        assert(!strcmp(link, "Pink Link") && !samus[0]);
        assert(s.cfg.WallJumpDifficulty == 3);
    }
    ui_input(&u, BTN_DOWN); ui_input(&u, BTN_DOWN); ui_input(&u, BTN_A);
    assert(u.action == ACT_GENERATE && u.action_slot == 0);
    ui_input(&u, BTN_B);
    assert(u.screen == UI_CREATE);                         /* back to the options of the Custom run */
    {   /* Chill run and Original start from their own options and go straight to the sprites */
        u.action = ACT_NONE; u.screen = UI_MODE; u.mode_cursor = 0;
        ui_input(&u, BTN_A);
        assert(u.screen == UI_START && u.run_mode == 0 && u.value[UIO_LINK] == 0);
        assert(u.value[UIO_MAP] == 0 && u.value[UIO_HELP] == 0 && u.value[UIO_SAVES] == 1 && !strcmp(ui_run_label(&u), "Cozy run"));
        { SmzSettings s; SmzPatchCfg pc; char l[32], sm[32]; ui_settings(&u, 7, &s, &pc, l, sm); assert(s.keysanity == KEYS_Zelda && s.cfg.WallJumpDifficulty == 0); }
        ui_input(&u, BTN_B);
        assert(u.screen == UI_MODE);
        ui_input(&u, BTN_DOWN); ui_input(&u, BTN_A);       /* Original */
        { SmzSettings s; SmzPatchCfg pc; char l[32], sm[32]; ui_settings(&u, 7, &s, &pc, l, sm); assert(s.keysanity == KEYS_None && s.cfg.WallJumpDifficulty == 2 && u.value[UIO_MAP] == 0); }
        ui_input(&u, BTN_A);                               /* Start is under the cursor */
        assert(u.action == ACT_GENERATE);
        u.action = ACT_NONE;
    }
    {   /* Save as custom: type a name with the d-pad, then it shows up in Saved customs and survives a reload */
        SmzUi v; ui_init(&v);
        int builtin = v.saved_count;
        assert(builtin >= 5 && ui_saved_dirty(&v) && !ui_saved_dirty(&v));
        v.screen = UI_MODE; v.mode_cursor = 2; ui_input(&v, BTN_A);
        assert(v.screen == UI_CREATE);
        v.value[UIO_HELP] = 2;                                         /* No helps */
        v.cursor[LEVEL_RUN] = 7; ui_input(&v, BTN_A);                  /* Save as custom... */
        assert(v.screen == UI_NAME && !strcmp(v.name, "Custom 10"));
        ui_input(&v, BTN_L); ui_input(&v, BTN_L); ui_input(&v, BTN_L); ui_input(&v, BTN_L); ui_input(&v, BTN_L); ui_input(&v, BTN_L);
        assert(v.name_len == 3 && !strcmp(v.name, "Cus"));
        v.key_cursor = 0; ui_input(&v, BTN_A);                         /* A */
        ui_input(&v, BTN_RIGHT); ui_input(&v, BTN_A);                  /* B */
        ui_input(&v, BTN_R);                                           /* space */
        ui_input(&v, BTN_UP);                                          /* wraps up to the buttons */
        assert(v.key_cursor >= 40);
        v.key_cursor = 40 + 2; ui_input(&v, BTN_A);    /* Save */
        assert(v.screen == UI_CREATE && v.saved_count == builtin + 1 && !strcmp(v.saved[builtin].name, "CusAB"));
        assert(strstr(v.saved[builtin].spec, "HelpMode=2") && ui_saved_dirty(&v));
        char file[8192];
        assert(ui_saved_text(&v, file, sizeof file) > 0);
        SmzUi w; ui_init(&w);
        ui_saved_load(&w, file);
        assert(w.saved_count == v.saved_count && !strcmp(w.saved[builtin].name, "CusAB") && !ui_saved_dirty(&w));
        w.screen = UI_MODE; w.mode_cursor = 3; ui_input(&w, BTN_A);
        assert(w.screen == UI_SAVED);
        w.saved_cursor = builtin; ui_input(&w, BTN_A);                 /* choose it */
        assert(w.screen == UI_START && w.run_mode == 3 && w.value[UIO_HELP] == 2);
        ui_input(&w, BTN_B); assert(w.screen == UI_SAVED);
        ui_draw(&w);
        ui_input(&w, BTN_RIGHT); ui_input(&w, BTN_A);                  /* delete asks first */
        assert(w.saved_count == builtin + 1 && w.saved_confirm);
        ui_input(&w, BTN_A);
        assert(w.saved_count == builtin && ui_saved_dirty(&w));
        while (w.saved_count) { w.saved_col = 1; w.saved_cursor = 0; ui_input(&w, BTN_A); ui_input(&w, BTN_A); }
        assert(w.screen == UI_MODE && w.saved_count == 0);          /* nothing saved: the entry disappears */
        ui_draw(&w);
        int rows = 0;
        for (int k = 0; k < w.hit_count; k++) rows += w.hit[k].kind == H_MODE;
        assert(rows == 3);
        ui_saved_load(&w, "");                                         /* an existing empty file stays empty */
        assert(w.saved_count == 0);
        v.screen = UI_NAME; ui_draw(&v);
        assert(v.hit_count == 44);
    }
    {   /* "Helps available" only shows in the Limited mode */
        int shown[2];
        for (int m = 0; m < 3; m++) {
            u.value[UIO_HELP] = m; u.level = LEVEL_RUN; u.screen = UI_CREATE; u.scroll[0] = 0; u.cursor[0] = 0;
            int found = 0;
            for (int pass = 0; pass < 3; pass++) {
                u.scroll[0] = pass * 6; ui_draw(&u);
                for (int i = 0; i < u.cmd_count; i++) if (u.cmd[i].kind == 1 && strstr(u.cmd[i].text, "Helps available")) found = 1;
            }
            shown[m == 1] = found;
            assert(found == (m == 1));
        }
        (void)shown;
        u.value[UIO_HELP] = 0;
    }
    /* touch: tapping an empty slot opens the mode picker, Custom opens Create, tapping a tab switches level */
    u.action = ACT_NONE; u.screen = UI_SLOTS_SCREEN;
    ui_draw(&u);
    ui_touch(&u, 100, 40 + 2 * 46 + 10);
    assert(u.screen == UI_MODE && u.active_slot == 2);
    ui_draw(&u);
    ui_touch(&u, 100, 32 + 2 * 47 + 20);
    assert(u.screen == UI_CREATE);
    ui_draw(&u);
    ui_touch(&u, 137, 10);
    assert(u.level == LEVEL_ZELDA);
    ui_touch(&u, 188, 10);
    assert(u.level == LEVEL_METROID);
    ui_touch(&u, 239, 10);
    assert(u.level == LEVEL_PLAY);
    ui_touch(&u, 290, 10);
    assert(u.level == LEVEL_ITEMS);
    ui_touch(&u, 10, 10);
    assert(u.screen == UI_MODE);
    ui_touch(&u, 10, 10);
    assert(u.screen == UI_SLOTS_SCREEN);
    ui_draw(&u);
    ui_touch(&u, 100, 40 + 2 * 46 + 10);               /* open Create again, then scroll and press the button by touch */
    ui_draw(&u);
    ui_touch(&u, 100, 32 + 2 * 47 + 20);
    ui_draw(&u);
    ui_touch(&u, UI_W - 18, 44 + 100);
    assert(u.cursor[u.level] == 3);
    ui_draw(&u);
    ui_touch(&u, UI_W - 40, UI_H - 40);   /* the Next button, above the help bar */
    assert(u.screen == UI_START);
    ui_draw(&u);
    ui_touch(&u, UI_W / 2, 180);
    assert(u.action == ACT_GENERATE && u.action_slot == 2);
    u.action = ACT_NONE; u.screen = UI_SLOTS_SCREEN;
    {   /* reachable checks: off by default, on in the Cozy preset, and not a rule change */
        SmzUi c; ui_init(&c); char js[4096];
        ui_settings_json(&c, 77, js, sizeof js);
        assert(strstr(js, "\"showReachable\":false") && strstr(js, "\"defaultRules\":true"));
        c.screen = UI_MODE; c.mode_cursor = 0; ui_draw(&c); ui_input(&c, BTN_A);
        ui_settings_json(&c, 77, js, sizeof js);
        assert(strstr(js, "\"showReachable\":true"));
    }
    {   /* monthly seed row appears once the host knows the seed */
        SmzUi m; ui_init(&m); m.screen = UI_MODE;
        ui_set_monthly(&m, "2026-10", 123456789, 5);
        m.mode_cursor = 3 + (m.saved_count > 0); ui_draw(&m); ui_input(&m, BTN_A);
        assert(m.screen == UI_START && m.run_mode == 4 && ui_run_monthly(&m) && !strcmp(ui_run_label(&m), "Monthly 2026-10"));
        char js[2048]; ui_settings_json(&m, 77, js, sizeof js);
        assert(strstr(js, "\"seed\":\"123456789\"") && strstr(js, "\"defaultRules\":true"));
    }
    {   /* weekly run: limited goals, the week's seed, rules count as unchanged */
        SmzUi w; ui_init(&w); w.screen = UI_MODE;
        ui_set_monthly(&w, "2026-10", 123456789, 5);
        ui_set_weekly(&w, "2026-W44", 359721838, 6);
        w.mode_cursor = 4 + (w.saved_count > 0); ui_draw(&w); ui_input(&w, BTN_A);
        assert(w.screen == UI_START && w.run_mode == 5 && ui_run_weekly(&w) && !strcmp(ui_run_label(&w), "Weekly 2026-W44"));
        char js[2048]; ui_settings_json(&w, 77, js, sizeof js);
        assert(strstr(js, "\"seed\":\"359721838\"") && strstr(js, "\"defaultRules\":true"));
        assert(strstr(js, "\"ganonCrystals\":2") && strstr(js, "\"ganonsTowerCrystals\":2") && strstr(js, "\"tourianBosses\":1"));
        w.value[3] = 7; ui_settings_json(&w, 77, js, sizeof js);   /* changing a goal leaves the weekly rules */
        assert(strstr(js, "\"defaultRules\":false"));
    }
    {   /* load choice: save state ahead of the game save */
        SmzUi l; ui_init(&l);
        ui_load_choice(&l, 1, 83 * 60 * 1000 + 45000, 78, 30, 91 * 60 * 1000 + 2000, 84, 36, 216, 100);
        assert(l.screen == UI_LOADCHOICE && l.lc.cursor == 1);
        ui_draw(&l);
        ui_input(&l, BTN_LEFT); ui_input(&l, BTN_A);
        assert(l.action == ACT_LOAD_SAVE && l.action_slot == 1 && l.screen == UI_SLOTS_SCREEN);
        l.action = ACT_NONE; ui_load_choice(&l, 2, 0, 0, 0, 5000, 1, 0, 216, 100); ui_draw(&l);
        ui_touch(&l, 240, 150); assert(l.action == ACT_LOAD_STATE && l.action_slot == 2);
        l.action = ACT_NONE; ui_load_choice(&l, 3, 1000, 1, 0, 2000, 2, 0, 216, 100);
        ui_draw(&l); ui_input(&l, BTN_B); assert(l.screen == UI_SLOTS_SCREEN && l.action == ACT_NONE);
    }
    {   /* player profile: name keyboard and flag list from the start screen */
        SmzUi p; ui_init(&p); ui_player_enable(&p, 1); ui_player_load(&p, "Ana\tES\t1");
        assert(!strcmp(p.player, "Ana") && !strcmp(p.country, "ES"));
        p.screen = UI_MODE; p.mode_cursor = 1; ui_input(&p, BTN_A);
        assert(p.screen == UI_START && p.start_cursor == 3);
        ui_input(&p, BTN_UP); assert(p.start_cursor == 2);
        ui_draw(&p);
        ui_input(&p, BTN_RIGHT); ui_input(&p, BTN_A); assert(p.screen == UI_COUNTRY);
        ui_input(&p, BTN_DOWN); ui_draw(&p); ui_input(&p, BTN_A);
        assert(p.screen == UI_START && p.country[0] && strcmp(p.country, "ES") && ui_player_dirty(&p));
        ui_input(&p, BTN_LEFT); ui_input(&p, BTN_A); assert(p.screen == UI_NAME && p.name_for == 1);
        ui_input(&p, BTN_L); ui_input(&p, BTN_L); ui_input(&p, BTN_L);
        ui_input(&p, BTN_A);     /* letter A */
        p.key_cursor = 10 * 4 + 2; ui_input(&p, BTN_A);   /* Save */
        assert(p.screen == UI_START && !strcmp(p.player, "A"));
        { /* first run: Start asks about the online ladder */
          SmzUi q; ui_init(&q); ui_player_enable(&q, 1); ui_player_load(&q, NULL);
          q.screen = UI_MODE; q.mode_cursor = 1; ui_input(&q, BTN_A);
          ui_input(&q, BTN_A); assert(q.screen == UI_ONLINE && q.action == ACT_NONE); ui_draw(&q);
          ui_input(&q, BTN_A); assert(q.online_mode == 1 && q.screen == UI_NAME);   /* Yes: asks the name */
          ui_input(&q, BTN_A); q.key_cursor = 42; ui_input(&q, BTN_A);               /* A, Save */
          assert(q.screen == UI_COUNTRY); ui_input(&q, BTN_DOWN); ui_input(&q, BTN_A);
          assert(q.screen == UI_START && q.country[0] && ui_online_now(&q));
          q.start_cursor = 3; ui_input(&q, BTN_A); assert(q.action == ACT_GENERATE);
          SmzUi n; ui_init(&n); ui_player_enable(&n, 1); n.screen = UI_MODE; n.mode_cursor = 1; ui_input(&n, BTN_A);
          ui_input(&n, BTN_A); ui_input(&n, BTN_DOWN); ui_input(&n, BTN_DOWN); ui_input(&n, BTN_A);   /* never ask again */
          assert(n.online_mode == 2 && n.action == ACT_GENERATE && !ui_online_now(&n));
          char o[64]; ui_player_text(&n, o, sizeof o); assert(strstr(o, "\t2"));
        }
        char out[64]; assert(ui_player_text(&p, out, sizeof out) > 0 && out[0] == 'A' && out[1] == '\t');
    }
    char js[2048];
    for (int i = 0; i < UI_OPT_COUNT; i++) {          /* the Custom run's options again */
        if (!strcmp(UI_OPTS[i].key, "Keysanity")) u.value[i] = 2;
        if (!strcmp(UI_OPTS[i].key, "WallJumpDifficulty")) u.value[i] = 3;
    }
    u.value[UIO_LINK] = 1;
    assert(ui_settings_json(&u, 5, js, sizeof js) > 0);
    assert(strstr(js, "\"keysanity\":\"Zelda\"") && strstr(js, "\"linkSprite\":\"Pink Link\"") && strstr(js, "\"WallJumpDifficulty\":3"));
    printf("%s\n", js);
    SmzRun run = {0}, back;
    snprintf(run.name, sizeof run.name, "Run 1");
    snprintf(run.seed, sizeof run.seed, "5");
    snprintf(run.rom_path, sizeof run.rom_path, "/tmp/x.sfc");
    snprintf(run.settings, sizeof run.settings, "%s", js);
    run.zelda_cleared = 3; run.zelda_total = 216; run.play_ms = 3723000; run.created = 1700000000000LL;
    assert(smz_run_save("/tmp/test_slot.run", &run) == 0 && smz_run_load("/tmp/test_slot.run", &back) == 0);
    assert(!strcmp(back.name, "Run 1") && !strcmp(back.settings, js) && back.play_ms == 3723000 && back.created == run.created);
    char sum[64]; smz_run_summary(&back, sum, sizeof sum);
    assert(strstr(sum, "Zelda 3/216") && strstr(sum, "1:02:03"));
    assert(smz_run_load("/tmp/does_not_exist.run", &back) < 0);
    char n1[96], n2[96];
    smz_run_rom_name(js, "5", n1, sizeof n1);
    ui_settings_json(&u, 5, js, sizeof js);
    u.value[UIO_MAP] = 1; u.value[UIO_FILTER] = 2; u.value[UIO_SHAPE] = 1;      /* tracker-only options must not change the name */
    ui_settings_json(&u, 5, js, sizeof js);
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(!strcmp(n1, n2) && !strncmp(n1, "smz3_5_", 7));
    u.value[UIO_HELPS] = 7; ui_settings_json(&u, 5, js, sizeof js);       /* how many helps may be asked is not in the ROM */
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(!strcmp(n1, n2));
    u.value[UIO_HELP] = 1; ui_settings_json(&u, 5, js, sizeof js);          /* limited helps: no hint tiles in the ROM */
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(strcmp(n1, n2) && strstr(js, "\"helpMode\":\"limited\"") && strstr(js, "\"helpCount\":7"));
    { SmzSettings hs; SmzPatchCfg hp; char l[32], sm[32]; ui_settings(&u, 5, &hs, &hp, l, sm); assert(hp.hint_tiles == 0); }
    u.value[UIO_HELP] = 0;
    { SmzSettings hs; SmzPatchCfg hp; char l[32], sm[32]; ui_settings(&u, 5, &hs, &hp, l, sm); assert(hp.hint_tiles == 15); }
    ui_settings_json(&u, 5, js, sizeof js);
    u.value[UIO_DARK] = 1; ui_settings_json(&u, 5, js, sizeof js);
    smz_run_rom_name(js, "5", n2, sizeof n2);
    assert(strcmp(n1, n2));
    /* plando: the chosen file's config replaces the seed and the settings */
    {
        const char *plandos[] = {"first.yml", "second"};
        ui_set_sprites(&u, 2, plandos, 2);
        u.value[UIO_PLANDO] = 2;
        char name[32], err[256];
        ui_plando_name(&u, name);
        assert(!strcmp(name, "second"));
        const char *yaml = "Seed: \"777\"\nGameModeType: AllDungeons\nGanonCrystalCount: 3\nKeysanityMode: Zelda\nOpenPyramid: true\nLogic:\n  MockBall: true\n  WallJumpDifficulty: Hard\n";
        assert(ui_set_plando(yaml, strlen(yaml), err, sizeof err) == 0 && ui_plando());
        ui_settings_json(&u, 5, js, sizeof js);
        assert(strstr(js, "\"seed\":\"777\"") && strstr(js, "\"gameMode\":\"AllDungeons\"") && strstr(js, "\"plando\":\"second\"") &&
               strstr(js, "\"MockBall\":true") && strstr(js, "\"WallJumpDifficulty\":3") && strstr(js, "\"keysanity\":\"Zelda\""));
        assert(ui_set_plando("Items:\n  Nowhere - Nothing: Missile\n", 36, err, sizeof err) < 0 && !ui_plando() && strstr(err, "Nowhere"));
        ui_set_plando(NULL, 0, err, sizeof err);
        ui_settings_json(&u, 5, js, sizeof js);
        assert(strstr(js, "\"plando\":\"\""));
    }
    {   /* 32 slots, 4 shown, chevrons; deleting asks first */
        SmzUi v; ui_init(&v);
        ui_set_slot_count(&v, 32);
        ui_set_slot(&v, 5, 1, "Run 6", "Zelda 1/216");
        for (int k = 0; k < 6; k++) ui_input(&v, BTN_DOWN);
        assert(v.slot_cursor == 6 && v.slot_scroll == 3);
        ui_draw(&v);
        int chev = 0;
        for (int k = 0; k < v.hit_count; k++) chev += v.hit[k].kind == H_CHEVRON;
        assert(chev == 2);
        v.slot_cursor = 5; ui_input(&v, BTN_A);                       /* the used slot opens its menu */
        assert(v.screen == UI_SLOT_MENU);
        ui_input(&v, BTN_DOWN); ui_input(&v, BTN_A);
        assert(v.screen == UI_CONFIRM && v.action == ACT_NONE);       /* nothing is deleted yet */
        ui_input(&v, BTN_A);                                          /* Cancel is the default */
        assert(v.screen == UI_SLOT_MENU && v.action == ACT_NONE);
        ui_input(&v, BTN_A);                                          /* Continue */
        ui_input(&v, BTN_A);
        v.action = ACT_NONE; v.screen = UI_SLOT_MENU; v.menu_cursor = 1; ui_input(&v, BTN_A);
        ui_input(&v, BTN_DOWN); ui_input(&v, BTN_A);
        assert(v.action == ACT_DELETE && v.action_slot == 5);
    }
    {   /* run list rows with numbers: draw within the command budget, big percent from the totals */
        SmzUi v; ui_init(&v);
        for (int k = 0; k < 4; k++) { ui_set_slot(&v, k, 1, "Run name long enough", "x"); ui_set_slot_stats(&v, k, 216, 216, 100, 100, 99999999LL, 3, 15, 127); }
        ui_draw(&v);
        assert(v.cmd_count < UI_MAX_CMDS - 40);
        int zero = 0;
        ui_set_slot(&v, 0, 1, "a", "x"); ui_set_slot_stats(&v, 0, 0, 0, 0, 0, 0, 0, 0, 0);   /* no totals: 0 % without dividing */
        ui_draw(&v);
        (void)zero;
    }
    {   /* online tables: Select opens them, the host answers ACT_LADDER, tabs and scrolling work */
        SmzUi v; ui_init(&v);
        ui_input(&v, BTN_SELECT);
        assert(v.screen == UI_SLOTS_SCREEN && v.action == ACT_NONE);   /* the host has no ladder */
        ui_ladder_enable(&v, 1);
        ui_input(&v, BTN_RIGHT);
        assert(v.screen == UI_SLOTS_SCREEN);                          /* Right is no longer a shortcut */
        ui_input(&v, BTN_SELECT);
        assert(v.screen == UI_LADDER && v.action == ACT_LADDER);
        ui_draw(&v);                                                  /* "Loading ..." while nothing arrived */
        ui_ladder_begin(&v, 0, "", 1);
        for (int i = 0; i < 12; i++) ui_ladder_add(&v, 0, "Player", "ES", 3600000LL + i * 1000);
        ui_ladder_begin(&v, 1, "2026-10", 0);
        ui_draw(&v);
        ui_input(&v, BTN_DOWN); ui_input(&v, BTN_DOWN);
        assert(v.ladder_scroll == 2);
        for (int i = 0; i < 10; i++) ui_input(&v, BTN_DOWN);
        assert(v.ladder_scroll == 12 - UI_LADDER_SHOWN);
        ui_input(&v, BTN_RIGHT);
        assert(v.ladder_tab == 1 && v.ladder_scroll == 0);
        ui_draw(&v);                                                  /* "No connection" */
        assert(v.cmd_count < UI_MAX_CMDS - 40);
        ui_input(&v, BTN_SELECT);                                     /* Select goes back to the runs too, and so does the top button */
        assert(v.screen == UI_SLOTS_SCREEN);
        ui_input(&v, BTN_SELECT); ui_draw(&v);
        ui_touch(&v, UI_W - 40, 12);
        assert(v.screen == UI_SLOTS_SCREEN);
        ui_ladder_load_json(&v, 2, "{\"kind\":\"weekly\",\"week\":\"2026-W44\",\"runs\":[{\"kind\":\"weekly\",\"period\":\"2026-W44\",\"player\":\"Ana\",\"country\":\"ES\",\"timeMs\":3723000,\"finishedAt\":1},{\"player\":\"Bo\",\"country\":\"\",\"timeMs\":4000000}]}");
        assert(v.ladder[2].loaded == 1 && v.ladder[2].n == 2 && !strcmp(v.ladder[2].label, "2026-W44"));
        assert(!strcmp(v.ladder[2].row[0].name, "Ana") && !strcmp(v.ladder[2].row[0].cc, "ES") && v.ladder[2].row[0].ms == 3723000);
        assert(!strcmp(v.ladder[2].row[1].name, "Bo") && !v.ladder[2].row[1].cc[0] && v.ladder[2].row[1].ms == 4000000);
        ui_ladder_load_json(&v, 2, NULL);
        assert(v.ladder[2].loaded == -1);
    }
    printf("ui ok, %d draw commands\n", u.cmd_count);
    return 0;
}
