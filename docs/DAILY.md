# Daily timed challenge (plan; phases 1 to 7 done, 8 started)

Short online runs with one goal. Three windows a day, the same challenge for everyone in a window, one attempt each.

## Decisions

| Topic | Decision |
|---|---|
| Windows | Three fixed UTC windows of 4 h: 06:00-10:00, 14:00-18:00, 22:00-02:00. Each window has its own challenge and its own table. |
| Start | Any setup works as long as the challenges vary. Phase 1: a fresh file (it starts at Samus's ship, in Super Metroid) with a set of starting items. Phase 3: a prepared mid-game save (progress, bosses already dead, a different spot). |
| Goals | Kill a given boss; get a given item; collect N crystals or N bosses; reach a place (room or area). |
| Length | The minimal route takes about 30-90 min for a good player. With every hint read it is solvable in 2-3 h, always inside the 4 h window. |
| Hints | The server holds a step-by-step plan for the goal and releases it on a schedule set per challenge (for example the first hint at 10 min, then one every 20 min). Hints go from vague (area) to exact (room, item). Free, the same for everyone. |
| Clock | Real time from the moment the player presses Start, checked by the server against the token time. Pausing or closing the app does not stop it. |
| Ranking | Your own time from your start. The run counts if the goal is reached before the window closes. |
| Attempts | One per window. Closing and coming back resumes the same run while the window is open. |
| Late start | Allowed. The entry shows "Open, 0:42 left"; under 1 h left it warns that the time may not be enough. |
| Past the window | The run is marked FAILED and the player is asked whether to keep playing. The clock and the hints go on. A finish is stored as "late" in the history and on a separate list, with no rupeergies and no rank. |
| Rules | No fast forward, no save states (forced off, not offered). Default timing as the ladder: the run ends where control is lost (see "Where the run ends" in PLAN.md) or at the goal. |
| Rewards | Rupeergies kept on the server, per install: for completing, for the rank in the window, for streaks. Shown in the main menu, not spent yet. |
| Platforms | PC, Android and 3DS. |
| In app | Its own row at the top of the run list ("Daily challenge": current window, time left, your state); it does not use one of the four slots. Rupeergy counter in the header next to Ladder. |
| In game | Window countdown (red in the last 15 min), your time, the goal, the hints (a note when one arrives, plus a Challenge tab in the tracker) and "next hint in". Each one can be hidden from the options. |
| Calibration | The server generates candidates and estimates their length from the minimal route, then keeps the ones in range. The formula is adjusted with real times. |

## Ideas to add (proposed)

- **Recovery code.** Rupeergies belong to `install_id`, so a reinstall would lose them. A short code shown in the menu, which another install can enter, moves the history and the rupeergies over.
- **Practice.** Past challenges can be replayed offline (same seed, same hints unlocked at once), with no rupeergies and no table.
- **Window table on the web.** `smz3.indieviral.com` gets a Daily tab: per window the times, the completion rate and the median, so the length estimate can be checked in public.
- **Difficulty tag.** Each challenge shows Easy / Normal / Hard from the estimate, plus which game it starts in.
- **Spoiler safety.** The client never receives hints that are not unlocked yet; the server answers with the hints released so far for that token.
- **Streak shield.** Missing one window does not break the streak as long as one window a day is completed.
- **Ending card.** On completion: time, rank so far in the window, rupeergies earned, streak.

## Architecture

### Server (`server/ladder/`)

- `daily.js`: computes the window from UTC time, picks or loads that window's challenge (`data/daily/<date>-<n>.json`), and runs the generator in advance so the challenge exists before it opens.
- New endpoints, same style as the others (strict keys, rate limits):
  - `GET /api/daily` returns the current and next windows: id, opens and closes at, goal text, start game, difficulty and whether this install already started it.
  - `POST /api/daily/start {install, window}` returns a token, the seed, the settings (with the starting items and the goal) and the start time. One per install and window.
  - `GET /api/daily/hints?token=` returns the hints released so far and when the next one comes out.
  - `POST /api/daily/finish {token, timeMs, proof}` checks the token, the window and that `timeMs` is at most the real time since the token was issued, then stores the result and grants rupeergies. A finish after the window closes is stored as late.
  - `GET /api/daily/table?window=` and `GET /api/tokens?install=`.
- **Generator and planner in C.** A server-side tool (`smz3daily`, built from the same core as `smz3check`) generates a seed with given settings and starting items, then runs the logic (`smz_logic_reachable`) sphere by sphere to get the minimal route to the goal. That route gives the list of steps (which item, where, needs which earlier item) used for the hints, and the estimate. It writes the challenge JSON. The ROM itself is still built on the device from the seed, as for Monthly and Weekly.
- **Estimate.** Sum per step: base time per location by area plus travel between areas, plus a game-switch cost. Seeded from the times of the ladder runs and then adjusted from the daily results (median versus estimate per window).
- **Rupeergies.** `data/tokens.json`: balance and history per install. Initial table: completing = 10, top 1/2/3 in the window = +15/+10/+5, a streak of 3/7/30 days = +5/+15/+50.

### Core (`core/`)

- **Goals in memory** (`smz3_goal.c`): boss killed (`smz_boss_mask`, plus the ALttP dungeon bosses), item owned (inventory), N crystals or bosses, area reached (current room or area from the map data). Each goal returns done or not done; the tracker reports the run as over when it is done.
- **Challenge settings**: starting items (`start_items`, already in the fill), `saveStates` false, fast forward off. A new `challenge` flag in the run JSON makes the hosts block both.
- **Shared UI** (`smz3_ui`): the Daily row in the run list, the challenge start screen (goal, window, time left, warning), the FAILED prompt (keep playing / leave), the ending card, the rupeergy counter, and a Challenge tab for the tracker. Options for what shows in game.

### Hosts

- **PC** (`pc_online.c`, `pc_main.c`): the calls above, the overlay (window countdown, your time, goal, next hint), hint polling every minute while playing, FF and states off.
- **Android** (`OnlineLadder.kt`, `Smz3Tracker.kt`, `NativeMenuScreen.kt`): the same, drawn on the bottom screen and in the tracker.
- **3DS** (`smz3online.cpp`, `smz3run.cpp`): the 3DS has a network stack but no online rows today. It needs the Daily row and the start screen on the bottom screen, the rupeergy balance call, and the timers on the bottom screen while playing.

## Phase 1: the generator (done)

`server/ladder/smz3daily.c`, built by `server/ladder/build-daily.sh [out]` from the same core as the apps.

- `smz3daily <window-id>` prints the challenge of that window as one JSON object. The output is deterministic: the window id seeds every random choice. It has these fields:
  - `settings`: the run settings JSON in the apps' format. The tool checks that `smz_logic_settings` on it rebuilds the same placement.
  - `seed`, `start` (the starting item options), `goal` (`kind`, `target`, `count`, `text`).
  - `estimate` (`searchMin`, `routeMin`), `steps` (item, location, region, game) and `hints` (`atMin`, `text`).
- `smz3daily --stats <prefix> <n>` prints one summary line per window, for tuning.
- **Candidate.** The starting items come from 2 to 6 random item groups, each with a random "Start with" option. Then come a random seed, the fill (`smz_fill_valid`, the same call as the apps) and a goal:
  - Get a unique progression item (30 %).
  - Defeat one of 15 bosses: the four Super Metroid ones, the ten dungeon bosses or Agahnim (35 %).
  - Collect 2 or 3 crystals (10 %).
  - Defeat 2 of the four Super Metroid bosses (10 %).
  - Reach a late region (15 %).
- **Route.** The tool collects everything sphere by sphere until the goal is met. Then it drops whatever is not needed:
  - First, in one pass, every item the logic never counts.
  - Then each remaining item, latest first, if the goal is still met without it. Energy tanks and the like are kept when the logic needs them.
  - The result is checked again at the end. Keys are left out, because keysanity is off.
- **Estimate.** For a player who does not know the seed, the tool goes through the stages of the route. At each stage the pool of reachable unchecked locations is searched until the needed ones turn up: r needed out of P take r(P+1)/(r+1) checks on average.
  - A check costs 0.8 min in the overworld, 1.5 in a dungeon and 1.6 in Super Metroid.
  - Each change of game adds 2.5 min, and the goal adds a cost of its own: a fight, or the rest of a dungeon.
  - `routeMin` is the time when you know the way: the needed checks only, plus 2.5 min of walking each.
  - A candidate is accepted with `searchMin` between 30 and 90.
- **Hints.** Each step gets two hints, first the item and the area ("Look in the Light World"), then the exact place, plus a last hint about the goal. Repeated items get an ordinal ("the second Gloves"). For N crystals or N bosses, the last hint lists only the places the route opens. The first hint comes at 10 min and the rest are spread so that the last one comes out by 150 min, one every 5 to 25 min. A candidate whose last hint plus its last step goes past 180 min is rejected.
- **Checked.**
  - 200 windows: all accepted, after 2 tries at the median (23 at most), about 6 ms per window.
  - `searchMin` from 30 to 90, median 59. `routeMin` from 9 to 66, median 28. 2 to 16 steps, median 5.
  - Last hint at 142 min (median), 170 at most.
  - Goals: 64 items, 60 bosses, 39 Super Metroid boss pairs, 19 regions, 18 crystal counts. In a run of 200, 45 different goal texts.
  - No count goal lists fewer places than it needs.
  - A challenge ROM made with `gen_rom` and the same options was run in the PC emulator (`smz3 selftest` with the new `SMZ3_SELFTEST_PAD` and `SMZ3_SELFTEST_DUMP` hooks): the new file starts at the ship with the Plasma Beam, and the save RAM holds the Blue Boomerang, the Ice Rod and the Shovel, the challenge's four starting items.
- **Not checked.**
  - No challenge has been played to its goal, so the estimate is untested against real times.
  - The route is the smallest set of items found, not necessarily the shortest path. When the logic allows two ways, the greedy pass keeps one of them.
  - Item names come from the game's hint texts (except "Boh", which becomes Bow). Progressive items show as "Gloves" and "Sword" with an ordinal.

## Phase 2: server endpoints and rupeergies (done, not deployed)

`server/ladder/daily.js`, mounted by `server.js` for `/api/daily*` and `/api/tokens`. Its state is kept in `data/daily.json` (starts, results, paid windows, rupeergies) and the challenges in `data/daily/<window>.json`.

- **Windows.** The ids are `YYYY-MM-DD-n` (n = 0, 1, 2 for 06:00, 14:00, 22:00 UTC, 4 h each); the 22:00 window runs past midnight. The challenge of the current and the next window is made ahead of time, once a minute, by running `smz3daily`. A window that has none yet gets it on first request, in about 10 ms.
- **`GET /api/daily?install=`.** Returns the current window (or `null` between windows) with the goal, the starting items, the difficulty (Easy under 45 min, Normal under 70, Hard above) and the estimate. It adds this install's state (`open`, `started`, `done`, `late`, `failed`), the next window, and the rupeergies (balance and streak). No seed, settings or hints before the start.
- **`POST /api/daily/start {install, window}`.** Only for the window that is open. It returns a token signed like the ladder ones (kind `daily`), with the seed, the run settings, the goal, `startedAt`, `closesAt` and the hints released so far. One attempt per install: starting again returns the same start, and after a finish it is refused.
- **`GET /api/daily/hints?token=`.** Returns the hints released on the player's own clock (`startedAt` + `atMin`) and when the next one comes. Hints not released yet are never sent.
- **`POST /api/daily/finish {token, timeMs, player, country, proof}`.** The time must be at least 3 min and no longer than the real time since the start. The proof goes through `smz3check` with the window's settings.
  - For challenges the proof needs one check instead of 12 (`"challenge":true` in the settings, `smz3_proof.c`). A daily may need only a few checks.
  - A finish after `closesAt` is stored as late: no rank, no rupeergies. A held proof gets no rank and no rupeergies either.
  - The first finish of an install counts; later ones get `duplicate`.
- **`GET /api/daily/table?window=`.** Returns the ranked runs, the late ones, how many started and finished, and the median time.
- **`GET /api/tokens?install=`.** Returns the balance, the current streak and the last 50 grants.
- **Rupeergies.**
  - 10 for a finish in time.
  - 15, 10 and 5 for the top three of a window, paid once after it closes.
  - 5, 15 and 50 when a streak reaches 3, 7 and 30 days, then 50 every 30 days. A streak counts days with at least one window finished in time, and is paid once a day.
  - The streak shown stays alive through today, until the day ends without a run.
- **Deploying a new `smz3daily`.** The server makes each window an hour before it opens and keeps it in `data/daily/<id>.json` and in memory. A new binary only reaches windows made after the deploy, so a text change also needs the files of the open and the next window edited and the server restarted (`pm2 restart smz3-ladder`). Change only texts there: the settings and the goal check are what the players already generated.
- **Local tests.** `DAILY_CLOCK_SHIFT_MS` moves the challenge clock of a local server; do not set it on the live server.
- **Checked.**
  - `node server/ladder/test_daily.js` (with `SMZ3DAILY=` the built tool) runs on a moving clock with a stub proof checker. It covers the time between windows, opening, a start for the wrong window, resume, hints at 9:59 and at 10:00, a forged token, too short and too long times, the duplicate finish, no attempt after finishing, the rank order and a held proof. It also covers a late finish with no rupeergies, the table, rank prizes paid once, a streak over three days (paid once per day), a streak broken by a missed day, the window past midnight, and a second instance reading the same data.
  - The real `server.js` on a local port with the real `smz3daily` and `smz3check`, and the clock moved into a window: `/api/daily`, start, hints and a refused finish answer as expected, and the old `/api/ladder` still works.
  - `smz3check` accepts a 3 check proof with the challenge settings (it rebuilds the seed with the starting items) and refuses it for a normal run. `test_proof` passes.
- **Not done.**
  - Deploying it to the live server: build `smz3daily` and `smz3check` there, then restart `smz3-ladder`. It needs your OK.
  - An admin command to approve a held daily run, as `admin.js` does for the ladder.

## Phase 3: goals in memory and challenge rules (done)

- **Goal check.** `core/ui/smz3_goal.{h,c}` reads the goal from memory. `smz3daily` writes the goal as a short text, in `goal.check` and as `"goal"` in the run settings:
  - `loc:<n>`: the location of the goal item is cleared.
  - `zboss:<room>`: the dungeon boss is beaten. This is bit 11 of the boss room's word, which is how the SMZ3 tracker reads it. The rooms are Eastern `c8`, Desert `33`, Hera `7`, Palace of Darkness `5a`, Swamp `6`, Skull Woods `29`, Thieves' Town `ac`, Ice `de`, Mire `90` and Turtle Rock `a4`.
  - `aga`: Agahnim is beaten (`$7EF3C5 >= 3`).
  - `smboss:<0-3>` and `smbosses:<n>`: one Super Metroid boss, or a count of them, from the boss flags.
  - `crystals:<n>`: the crystal byte.
  - `zdungeon:<id>`: inside that dungeon (`$040C`, indoors, module 7).
  - `smarea:<n>`: in that Super Metroid area (`$079F`, with the game state in play).

  Each check reads the live block of the current game and the save RAM copy of the other one, as the tracker does. Reach goals are limited to places the memory tells apart: Ganon's Tower, Turtle Rock, Misery Mire and Ice Palace, plus Wrecked Ship, Maridia and Norfair.
- **End of the run.** `SmzTracker.goal` is set by the host from the run settings. When the goal is met, `goal_met` is set and stays set, and the tracker reports game 3, as at the end of a normal run. The read that sees it still counts its time; nothing after it counts. On Android the JNI gets the same check through `setGoal(settings)`, which `Smz3Tracker` calls when the run changes, and `summarize` reports game 3.
- **Rules.**
  - Save states: challenge settings have `saveStates:false`, so they are already off on the three platforms.
  - Fast forward is off when the settings have `"challenge":true`:
    - PC: the key and the pad are ignored.
    - Android: the hotkey shows "No fast forward in a daily challenge", and the game menu hides fast forward.
    - 3DS: `smz3NoFastForward`. The hold and toggle hotkeys are ignored, and a toggle left on from another game is cleared.
- **Build.** `smz3_goal.c` is in the PC Makefile, `build-3ds.sh` and the Android `CMakeLists.txt`. Android also builds `smz3_tracker.c` now, for the boss and crystal masks. The 3DS patch now includes `source/3dsinput.cpp`.
- **Checked.**
  - `test_goal`: parsing and bad input, and every goal kind in both games, live and from the save RAM. It also checks that other room bits do not count, that outdoors or a door transition does not count, that nothing counts before the file has started, and that the tracker ends on the goal and stops counting.
  - `test_tracker`, `test_ui`, `test_discovery`, `test_proof` and `test_preview` pass.
  - PC, Windows, 3DS and the APK build.
- **Not checked.** A goal reached in a real game: that needs phase 4, where the PC can start a challenge run. No challenge run has been played, so the fast-forward block has not been tried on any platform.

## Phase 4: the PC (done)

- **Shared client code** (`core/ui/smz3_daily.{h,c}`, for the three platforms).
  - It parses the answers of `/api/daily`, `/api/daily/start` and `/api/daily/hints` into plain C structs. Server times are moved to the device clock with the answer's `now`, so a wrong device clock does not move the window. The start and hints answers now carry `now` too.
  - It keeps the attempt as text beside the run: window, token, goal, goal check, start, close, next hint and the hints out so far.
  - `ui_apply_run_settings` sets the menu from the challenge's settings JSON: seed, rules, logic and start items. The host generates the run from that, so the ROM has the server's placement. `smz_daily_mark_settings` adds `"challenge":true` and the goal to the stored settings.
- **Menu** (`smz3_ui.c`).
  - The Daily row sits on top of the run list and the slots below it scroll as before. Between windows it says when the next one opens; offline it says the challenge needs the server.
  - The row shows the time left in the window (red in the last 15 minutes), the goal and the state: difficulty and estimate, started, finished with the time, finished late, or failed.
  - The rupeergy counter sits by the Ladder button.
  - The Daily screen shows only the goal, the time left, the state (or the difficulty and estimate), a warning when under an hour is left, and Start (or Continue), Info and Back. Info lists the start items and the rules.
- **PC host.**
  - Start asks the server for the attempt and generates the run into its own slot (`slot33`, never listed), keeping the player's sprites. A second Start in the same window resumes the run already made.
  - In game, a box over the top-right corner shows the window clock, the own time from Start, the goal and the time to the next hint. The left panel shows the own time instead of the play time.
  - When a hint is due the app asks the server for it and shows it at the bottom for 15 seconds. The Daily tab of the tracker lists every hint with its minute.
  - When the window closes before the goal, a TIME IS UP card holds the game. The player can keep playing (a finish then goes on the late list) or leave.
  - At the goal, the CHALLENGE COMPLETE card shows the real time from Start. The finish is queued with the proof, sent at once (or at the next launch when offline), and the card shows the server's answer: rank and rupeergies, late, held or refused.
  - The video options have four rows to hide the window clock, the own time, the goal and new hints, and the next-hint countdown. The video list now scrolls.
- **Fixed on the way.** `store_path` returned one static buffer shared by every thread. The background requests started at launch could read each other's files; once the install id was read as garbage and replaced with a new one. The buffer is now one per thread.
- **Test hooks.** `SMZ3_DAILY_GO=1` presses Start (or Continue) once the server answers. `SMZ3_SELFTEST_GOAL=<check>` makes `smz3 selftest` say when a goal reads as met.
- **Checked.**
  - `test_daily` covers parsing (escapes, between windows, bad answers), the attempt text round trip, the Daily row navigation, and the rebuild of 11 challenges: the menu set from each challenge's settings gives the same placement as the server.
  - Screenshots against a local server with its clock moved into a window:
    - the row between windows and inside one;
    - the Daily screen;
    - the run started, with the in-game box;
    - a hint arriving;
    - the Daily tab;
    - TIME IS UP;
    - the complete card;
    - the hidden rows.
  - A finish with an honest proof reached the server and was ranked 1st, worth 10 rupeergies. The menu then showed "Finished in 0:04:00" and 10 rupeergies. An empty proof was refused with the reason on the card.
- **Not checked.** A goal reached by playing (the finish above was sent by hand), the pad on the new screens, and the Windows build running.

## Phase 5: Android (done)

- **Native side** (`frontends/android/jni/smz3_jni.c`). `uiDailyEnable`, `uiDailySet` (an `/api/daily` answer into the shared menu) and `uiDailyWindow`. `dailySettingsJson` turns the server's settings into the run JSON the generator takes, the same way as the PC: preset 1, the challenge's settings, the player's sprites, no plando, then `"challenge":true` and the goal.
- **Menu** (`NativeMenuScreen.kt`). The Daily row and screen are the shared ones. The app asks for `/api/daily` every 60 seconds and on resume. Start asks the server for the attempt, generates the run into its own slot (`RunStore.SLOTS`, 32, never listed) as "Daily <window>" and plays it; in the same window it resumes the run already made.
- **The attempt** (`DailyChallenge.kt`) lives in `files/runs/daily.json`; the finish waiting in `daily.finish`, the server's answer in `daily.result`, the proof in `slot32.proof`. A finish not sent goes out at the next launch.
- **In game.**
  - A box in the top-right corner of the game screen (`Smz3DailyOverlay.kt`) shows the window clock (red in its last 15 minutes), the own time from Start, the goal and the time to the next hint.
  - When a hint is due the app asks for it every 15 seconds until the server releases it, and shows it at the bottom for 15 seconds.
  - When the window closes before the goal a TIME IS UP dialog offers Keep playing (a finish then goes on the late list) or Leave the run.
  - On the Thor's second screen the tracker gets a DAILY tab: goal, window clock, own time, every hint with its minute and the next one. The RUN tab shows the real time from Start instead of the play time.
  - At the goal the tracker sends the real time from Start with the proof; the run complete card says CHALLENGE COMPLETE and then the server's answer (rank and rupeergies, late, held or refused).
  - The tracker's CONFIG tab has a DAILY CHALLENGE section with the same four Shown / Hidden rows as the PC.
- **Texts.** The Android menu font is wider than the PC one, so the menu's daily texts were shortened for every host: the title says "Runs" when the rupeergy counter is there, and the Daily screen's rules are three short lines.
- **Test hook.** `/sdcard/SMZ3/server.txt` names another server (for example `http://127.0.0.1:3391` with `adb reverse tcp:3391 tcp:3391`).
- **Checked on the Thor** against a local server with its clock moved near a window's close: the row and the Daily screen (every text fits), Start, the box over the game, the first hint arriving at 10 minutes (box, note at the bottom and DAILY tab), TIME IS UP, Leave the run, and the row afterwards.
- **Not checked.** A finish on Android (it needs the goal reached by playing; the code path is the PC's, which was checked with a finish sent by hand) and the CONFIG rows.

## Phase 6: the 3DS (done)

- **Network** (`smz3online.cpp`). The `/api/daily` answer comes on a thread every 60 seconds while the menu is open; Start blocks on `/api/daily/start` behind a busy screen. The attempt lives in `sdmc:/SMZ3/online/daily.txt`, the finish waiting in `daily.finish`, the answer in `daily.result`, the proof in `slot32.proof`. A finish not sent goes out at the next start. Times use the console clock moved by the server's `now`, so a console set to local time is fine.
- **Menu** (`smz3run.cpp`). The shared Daily row and screen. Start generates the run into slot 32 (never listed) with the player's sprites, and plays it; in the same window it resumes it.
- **In game.** The tracker on the second screen becomes "SMZ3 daily challenge": the goal, the window clock (red in the last 15 minutes, then TIME IS UP with the late-list note), the own time from Start, the time to the next hint and the hints, the newest first and in amber for 15 seconds. At the goal it shows CHALLENGE COMPLETE with the real time and then the server's answer. There is no box over the game on the 3DS, so there are no rows to hide it.
- **Rules.** No fast forward (as before) and now no save states of any kind in a challenge run.
- **Test hook.** `sdmc:/SMZ3/server.txt` names another server.
- **Checked on Azahar** against a local server near a window's close: the row, the Daily screen, Start, the panel's clocks and the first hint arriving at 10 minutes.
- **Not checked.** The TIME IS UP line and a finish (as on Android), and a real 3DS.

## Phase 7: the web page (done, deployed)

- **Daily challenge section** on top of `index.html`, across the page width: the window picker (every window opened in the last four days), and for the open window its goal, start items, difficulty and estimate and a live clock to its close (red in the last 15 minutes). Between windows it counts down to the next one. Below: how many started and finished, the median time, the table and the late list.
- **Deployed** on 2026-10-30 to the live server: `server.js`, `daily.js`, `index.html` and `admin.js` copied, `smz3daily` and a fresh `smz3check` built there from the repository's core (`build-src/`). The files replaced are kept in `backup-20261030-2047/`. The first real window is 2026-10-30-2, from 22:00 UTC.

## Phase 8: mid-game starts (first step done, deployed)

A full prepared save (chests opened, rewards taken, another starting spot) would mean writing both games' save formats by hand. The first step instead gives a challenge the most visible mark of a game already in progress: some Super Metroid bosses are already dead.

- **Planner** (`smz3daily.c`). 40 % of the candidates start with 1 to 3 of Kraid, Phantoon, Draygon and Ridley dead, drawn from a stream of their own so the rest of a candidate does not change. A goal on one of those bosses, or on "2 of the four", is rejected. The settings carry `"smBossesDead":<mask>` (bit 0 Kraid, 1 Phantoon, 2 Draygon, 3 Ridley), and the start list shows "Kraid already defeated" and so on, first so the devices' short list keeps them.
- **Estimate.** Unchanged: the logic still asks to beat a dead boss for what lies behind it (the Wrecked Ship's power, for one), so the estimate errs long.
- **Core.** `smz_daily_mark_settings` copies the mask from the server's settings into the run's. `smz_prepared_apply` keeps those bosses' flags set: in work RAM (`$7ED828` + area) once Super Metroid is in game (state 8 on, so the file load does not wipe them), and in Super Metroid's save copy (`$2078` + area) while Zelda is played.
- **Hosts.** PC and 3DS call it on every tracker tick. Android reads memory as copies, so LibretroDroid got a `writeMemory` beside `readMemory` (in `patches/libretrodroid-readmemory.patch`), and the tracker writes the bytes `Smz3Native.preparedPokes` returns.
- **Checked.** `test_daily`: the mask survives the marking, and the flags are set only in game and then left alone. In the PC emulator, a challenge ROM with Kraid, Phantoon and Ridley dead (`SMZ3_SELFTEST_DEAD`, a new test hook) reaches the landing site with those three flags set; without the hook they stay clear. 60 windows from the new planner were all accepted.
- **Not checked.** Playing into a dead boss's room. Android and 3DS only build.
- **Next steps.** More of a mid-game start once phase 9 says how long real runs take: a bigger start kit, chests already opened (location flags), Zelda dungeon rewards, and starting in Zelda.

## After the first play test (done; Android not yet checked on the Thor)

- **Clock start.** The own clock starts when the game does (the tracker sees a file in play), the way the ladder times its runs, not at Start on the menu. ROM generation, the boot and the file select no longer count. The hosts keep the moment as `began` in the attempt file (`smz_daily_note_began`, `DailyChallenge.noteBegan`). A game that starts more than 10 minutes after Start (an attempt resumed on a new install) keeps Start, so losing the files never resets the clock. The server still checks the time against its own `startedAt` and still releases hints from it.
- **Off the game screen.** On PC the window clock, the goal and the next hint go into the left panel under the own time; the box over the game only comes back when the window is too narrow for side panels. On a device with a second screen (the Thor) they go to a strip over the tracker's tabs, with each new hint for 15 seconds. A phone without one keeps the box. The 3DS already had them on the bottom screen.
- **Plain map.** A challenge never marks the reachable checks, whatever the player's option says: `smz_daily_mark_settings` turns `showReachable` off, and the hosts ignore it in any run marked as a challenge.
- **Menu.** Select (Tab on the keyboard, View/Share on a pad) opens the ladder and goes back; Left and Right no longer do. The top buttons read "Ladder >" and "< Save list". With the Daily row selected the title says "Special run". On Android the shared hint line ("A Open  Select Ladder") is drawn as a bar with the Thor's buttons, the menu uses the PC's font (Chakra Petch, copied by `prepare_assets.py`), and the wipe covers the new screen on its first frame. Start on the Daily screen goes to the game without a wipe into the busy screen or back to the list.
- **Texts.** The reward is called rupeergies everywhere the player reads it (the API keeps `/api/tokens`). The boss goal reads "Defeat two Super Metroid bosses".
- **Fixes.** The progress of the game save ("Which to load") was always 0: it was counted without WRAM, and without WRAM the core took the game as not started. `smz3_summarize` with no WRAM now counts both games from the save RAM. Saves kept before the fix show their numbers after the next in-game save. On Android the recent lists now carry the run they belong to, and a new run clears the slot's map, marks and recent lists, so a new run no longer shows the items of an earlier one.

## Phase 9: calibration (waiting for data)

It needs real times: the first public window is 2026-10-30-2. After a week, compare each window's median (`/api/daily/table`) with its `searchMin` and adjust the costs per check, per game switch and per goal in `smz3daily.c`.

## Phases

1. **Done.** Planner and generator on the server. `smz3daily` produces challenges with starting items and the four goal kinds, the route steps and an estimate. Check: generate 200 candidates, look at the distribution of estimates and the hint text for a few, and play one on PC to its goal.
2. **Done (deployed with phase 7).** Server endpoints and rupeergies, with tests against a local server (start twice, finish too fast, late finish, hints not released early, streaks across days).
3. **Done.** Core goals and challenge rules, with `test_goal` (made-up memory for each goal kind) and the tracker reporting the end.
4. **Done.** PC: menu row, start screen, overlay, FAILED flow, ending card, rupeergies. Checked with screenshots and against the local server.
5. **Done.** Android: the same, checked on the Thor.
6. **Done.** 3DS: online row, start screen, timers, rupeergies. Checked on Azahar.
7. **Done (deployed).** Web: Daily section on `smz3.indieviral.com`.
8. **Started (deployed).** Mid-game starts: Super Metroid bosses already dead. The rest of a prepared save waits for phase 9.
9. **Calibration**: after a week of windows, compare the real times with the estimates and adjust the formula.

## Open questions

- Prize table: the numbers above are a first guess.
- Mid-game starts: how far in, and whether a challenge may start in Super Metroid.
- Recovery code: keep it as proposed or leave rupeergies tied to the install only.
