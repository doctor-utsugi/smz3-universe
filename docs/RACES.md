# Online races (plan, nothing built yet)

A start menu, and two online modes where 2 to 8 players race the same seed at the same moment: Draft (a short goal from a
random place in the game) and Championship (a normal run from the start). For every version: PC, Linux handhelds, Android
and the Thor, 3DS.

This file is the plan to iterate on. Open questions are at the end; each answer moves into Decisions. The mockups are
published as an artifact, and no interface gets built until they are approved.

## Decisions

| Topic | Decision |
|---|---|
| Versions | All of them, and nothing ships as a race mode until PC, Android and 3DS can play each other. |
| Start menu | On every start: the logo for a fixed 2 seconds that can't be skipped, then Start/Load game, Online draft, Championship, Options. A `play N` launch goes straight in. Offline, the online entries show greyed out with a message. |
| Start/Load game | Goes to today's run list, which stays as it is. |
| Players | 1 vs 1, or a multi race of up to 8. |
| Draft | Same seed and same place for everyone, one random goal that takes 5 to 15 minutes. |
| Draft goals | Kill N enemies, get N rupees, kill a boss in either game, get a crystal. |
| Championship | A normal run from the start. After the player count: Full game, or a Partial game of a chosen length. |
| Before the start | Each player picks a Link and a Samus skin, then confirms in a ready room that shows who has confirmed, with their skins. |
| Start | Only when the last player confirms, and at the same moment for everyone. |
| Rules | No save states, fast forward or rewind, nor anything else that changes the normal flow of a run. |
| Rival events | A small note in a corner with the player's name and what they got, and a small chime. A line per player with their % of the run shows on two screens only. |
| Ending, 1 vs 1 | A win or loss message, with the choice to keep playing or close. The loser's play after that counts for nothing. |
| Ending, multi | Positions appear as players finish, then the final table. |
| Leaving | Anyone can abandon. The last player left wins at once, with no ladder entry, because a win without a finish has no time or proof. If everyone abandons, the race has no result. |
| Ladder | The Championship winner can choose to save their time on a ladder, if they finished the run. |
| Chat | Text chat between the players of a race. On one screen it opens only from the tracker overlay, and a new message shows for 2 seconds at the bottom right of the game. |
| Server | Everything on salamando: the race server with Redis for live races, and the ladder and daily moved there too. |
| Options menu | Buttons, resolution and basic settings, nothing that configures a run. |
| Finding a race | Private rooms by code, with a field to enter one, and a list of open public rooms to search. |
| Room expiry | Redis expires rooms that never got going: 1 hour of waiting at most. |
| Draft goal | Random only. The server picks the goal and the start. |
| Draft pool | The server prepares and checks a reserve of Drafts ahead of time, each with fixed starting items, and takes one without repeating when a room closes. |
| Spectators | Not for now. |
| End of a multi race | The first finish starts a countdown set by the type of race: Draft 5 minutes, Sprint 15, Half 30, Full 60. Whoever hasn't finished by then gets no position. |
| Partial lengths | Sprint (2 crystals and 1 Super Metroid boss) and Half (4 and 2). They are the existing Weekly and Quick finish settings and end at the normal ending, so they need no new goal kind. Anything longer is the Full game. |
| Draft games | It can start in either game, and the goal can be in the other one. Starting in Zelda depends on step 9; if no way works, Draft starts in Super Metroid only. |
| Server down | The game goes on and each client keeps what happens, sending it when the server is back. Back within 5 minutes, the race continues; if not, it is cancelled. |
| Draft goal shown | At the 3, 2, 1, the same for everyone. Nobody plans a route before the start. |
| Draft limit | 30 minutes, which ends the race even if a countdown is still running. If nobody reaches the goal it is a draw; in a multi race the highest % wins, and a tie at the top shares the place. |
| Championship rules | Fixed: the Original ladder settings for Full, and the same settings with the lower goal numbers for Partial. Private rooms can also pick Custom, which never goes on a ladder. |
| Long races | The players can pause by agreement and carry on another day. Everyone must vote yes, and only the Championship Full game can pause, up to the first finish. |
| Pause limits | No limit on pauses, but the race expires 7 days after the first one, cancelled with no result. The pause vote sets a resume time that every yes accepts. If everyone confirms sooner, the race resumes sooner. From the resume time, whoever is back waits 10 minutes, then goes on, and the missing players have abandoned. If nobody is back, the race is cancelled. |
| Paused times | They go on the ladder with a "paused" mark. |
| Identity | The name and `install_id`, with no accounts. A reinstall loses the history, and two players can share a name. |
| Chat control | Each player can mute another or turn chat off. The server limits how many messages and how long. No word filter. |
| Ready room | No fixed time. The creator removes whoever doesn't confirm, and the 1 hour limit still applies. |
| Room setup | The creator picks mode and format. With 2 or more players they close the room, and everyone goes on to skins and the ready room. |
| Ranking | Ladders by time, one table per Championship length, with no player rating and no rupeergies for races. Draft has no ladder: its goals differ and never repeat, so a table of times would rank the luck of the draw. |
| Rival notes | Only major items, bosses and crystals, and the goal's progress in a Draft. Notes and chime can each be turned off in Options; the standings on two screens stay. |
| Daily challenge | Stays in the run list, under Start/Load game. |
| Moving servers | Before the races, between two daily windows, with a short outage. Only the address's target changes. |
| Creator leaves | The room passes to whoever has been in it longest, with the same code. |
| App version | Everyone in a room needs the same version, and it must match the server's, which holds one checker and one Draft pool. A mismatch can't create or join and gets a message saying to update, because a different generator would build a different seed. A paused race keeps the version it started with. |
| Abandon | On two screens it lives in CONFIG, by touch, except on the 3DS, which has no CONFIG and uses its game menu. On one screen it's in the existing exit menu. It always asks for confirmation. |
| Standings on RUN | At most four rows, the top three and you. All the players appear in the final table. |
| Finish time | Race time is the server's arrival time minus `startAt`. A client's stated time counts only for a finish queued while the server was down, capped at its arrival. A finish that arrives after a result was shown doesn't change that result. |
| Suspect finish | A proof the checker refuses voids the finish. A suspect proof stands for the race and only holds the ladder entry until reviewed. |
| Fewer players | If removing players leaves one in the ready room, the room goes back to waiting. |
| Dropped connection | The player can return to the same race for 5 minutes, with the clock running. After that it counts as abandoning. A closed app counts the same. |

## What already exists

Most of the hard parts have a working first version in the daily challenge.

| Need | Already there | Missing |
|---|---|---|
| Same seed on every version | `smz3daily` plus `smz_daily_*`: the server hands out settings and seed, every client generates the same placement | Nothing for Championship |
| Goal read from memory | `smz3_goal.c`: bosses, crystals, items, areas | Enemy and rupee goals |
| Enemy and rupee counts | `smz3_counters.c`: rupees, enemy kinds, bosses | A goal kind that reads them, and a total across all enemy kinds: today only 12 kinds count, and the rule for what counts as an enemy has to widen |
| Rules forced off | The `challenge` flag blocks states and fast forward on every host | Check that rewind and every hotkey respect it |
| Server clock | `online_now_ms` and the signed start token | Offset good to a fraction of a second for a shared start |
| Finish check | `smz3check` replays the proof, which shows a possible order of checks and never reads the goal or the ending | `MIN_MS` is 10 minutes, so a Draft finish would be refused; Draft goals need their own check |
| Name, country, install id | Profile and `install_id` | Nothing |
| Skins | Sprite pickers on every version | A way to show a rival's skin the player does not have |
| Text entry with a pad | `UI_NAME` grid in the shared menu | Using it for chat |
| Second screen | PC dual mode, Thor, 3DS | A CHAT tab on each |

Three things in there change the order of the work:

- **Draft needs a start the game does not have yet.** `docs/DAILY.md` phase 8 only marks Super Metroid bosses as dead. Starting
  in a random place is the prepared save that was left for phase 9.
- **Save states cannot be that start.** The three emulators are different builds of Snes9x, so a state from one does not load
  on another. The proposal is to build it from SRAM, which is plain game data: a prepared file that loads at a Super Metroid
  save station or a Zelda start point, with its items and progress set. This is unproven and has to be tried first.
- **SRAM can't choose Zelda.** The boot code forces Super Metroid on every reset (`docs/PLAN.md`). A Zelda start needs another
  route, either patching the boot code or one prepared state per emulator, and it would only offer Link's House or Sanctuary.

So Championship comes first, because it needs none of that and proves all the online parts.

## How it fits together

```
 core/ui/smz3_race.c   room state, ready room, events, standings (shared C, no network code)
        |
 hosts: pc_online.c, OnlineLadder.kt, smz3online.cpp   one HTTP call each, on a background thread
        |
 salamando: race module of server.js + Redis           live rooms, with an expiry
        |
 race store, its own JSON file                         results kept for good
```

The menus go in `core/ui`, so they are written once, though Options calls host code for buttons and resolution. The race code is a module of `server.js`, like `daily.js`, so it shares the secret and the checker. The in-game parts (start hold, corner note, message dialog, chat) are not shared: PC is
C, Android is Kotlin, 3DS is C++. That is three versions of a small HUD, and the biggest cost of doing this everywhere.

### Messages: plain HTTP polling

No RPC system is needed. The 3DS has `httpc`, PC calls `curl`, and none of them
has a WebSocket client, while one poll carries everything:

```
POST /api/race/poll {token, since, progress, events[], chat[]}
  -> {state, players[], events[], chat[], startAt, now}
```

The same call is the heartbeat, sends what happened here and fetches what happened to the others. Every 2 seconds in a race
and every second in the ready room. A rival's note arrives 2 to 4 seconds late, which is fine for a note. Eight players are 4
requests a second per room.

| Endpoint | Purpose |
|---|---|
| `POST /api/race/create` | Mode, format, size, app version. Returns the room, its code and a token |
| `POST /api/race/join` | Enter by code, or from the public list, with the app version |
| `GET /api/race/open` | The open public rooms, by mode and format |
| `POST /api/race/pick` | The two skins |
| `POST /api/race/ready` | Confirm. The last one sets `startAt` far enough ahead for the slowest load |
| `POST /api/race/poll` | Above |
| `POST /api/race/finish` | Proof, plus a stated time for a finish queued during an outage |
| `POST /api/race/leave` | Abandon |
| `POST /api/race/ladder` | The winner keeps their time |

Redis keys: `race:<id>` (state), `race:<id>:log` (events and chat), `code:<code>` (room by code). Each state change and poll
renews their expiry, and the race stores an absolute deadline for the 7 days of a pause. Redis persistence stays on, so a reboot
doesn't lose a race paused for days. Finished and cancelled races go to the permanent store. The server code uses no packages today, so it either takes one Redis
client or speaks the protocol itself.

### The shared start

The server sends `startAt` in server time and each client works out its offset from a few polls. Everyone sees a 3, 2, 1 and
the game starts on the same second. The seed and the ROM are generated **before** the ready room closes, so a slow 3DS
doesn't lose time.

A race can't count from the menu, because every version loads the game after leaving it, and on Android the menu and the game
are separate processes. So before `startAt` each host loads the ROM, holds the first frame and shows the countdown, then lets go
at `startAt`. PC and 3DS can do this by reordering their load; Android needs the hold and the countdown inside the game process.

### Pause by agreement

The rule against save states has one exception here. When every player agrees, each client saves one state of its own game
and the server stops the clock. The state stays on that device, can be loaded once, and only through the race's token; it
isn't available from any menu. To carry on, everyone goes back through the ready room and the 3, 2, 1. A state can't move
between devices, so a player must return on the same one.

The vote carries the resume time, so nobody loses a race by being asleep when a rival opens the app. Whoever asks picks the day
and hour, within the 7 days, and each player sees it in their own time. Players can vote a new time while paused. A pause isn't offered after the first finish, because a finished player has no state to save.

### Server outages

A client can't tell a server outage from its own dropped connection, and doesn't need to: it keeps playing and queues what
happens. Time the server was down doesn't count against the 5 minutes to return, so on restart the server resets every player's
last contact. After 5 minutes of failed polls the client tells its player the race is cancelled.

### Progress line

| Race | The % is |
|---|---|
| Draft, count goals | Count over target |
| Draft, boss or crystal | Steps of the planned route, from the same planner the daily uses; with no steps, 0 then 100 |
| Championship, Full | Crystals and bosses done over the total |
| Championship, Partial | The parts of its goal done |

### Screens during a race

- **One screen:** the corner note over the picture, with no % per player. Chat opens from the tracker overlay, so it needs no new button. A new message shows at
  the bottom right of the game area, one at a time, and fades after 2 seconds.
- **Two screens:** the game screen stays clean but for the corner note. RUN keeps its layout, info on the left and map on the
  right, with the standings added to the left column. A new CHAT tab takes messages by touch. The pad never leaves the game.
- **A race can't pause**, so typing costs time on a single screen. Quick phrases, one tap or two buttons, cover that.

| Version | Typing |
|---|---|
| PC | Keyboard |
| Android, Thor | System keyboard |
| 3DS | A touch keyboard drawn in the CHAT tab, since the system keyboard stops the game |
| Handhelds | Quick phrases, and the grid of `UI_NAME` |

## Work in order

1. **Sign-off.** Questions below answered, mockups approved.
2. **Start menu and Options** on every version. No network, and useful alone. Built: the logo, the start menu and Options are
   in `core/ui`, with the online entries greyed out until step 5. Options opens Buttons and Video only on PC and the Linux
   handhelds; Android sets the buttons in the game's CONFIG tab and the 3DS in its game menu, so there Options has Player and
   Quit.
3. **Move to salamando.** Not needed: `smz3.indieviral.com` already runs on salamando, behind its Apache.
4. **Server.** Built: `server/ladder/race.js`, loaded by `server.js`, with `redis.js`, a client of its own so the server keeps
   no packages. Rooms live in Redis database 5 under `smz3:`, finished races and the race ladders in `data/races.json`.
   Salamando's Redis is shared and saves snapshots (RDB), not every write, so its config stays as it is. Pause and Draft
   are not in it yet.
5. **Core and PC.** `smz3_race.c`, skin pick, ready room, shared start with the hold on the first frame, Championship Full 1 vs 1.
   Built: `core/ui/smz3_race.c` reads the server's answers, and the race screens (size, create or join, format, length, public
   list, room, ready room) live in `smz3_ui.c`. On PC, `pc_race.c` sends the requests and polls from one thread. The game is
   built when the player presses ready, since the skins go into the ROM. Until the start the game's intro plays but takes no
   input, under a countdown. Bosses and crystals go out as news for the rivals and as progress; the finish sends the proof.
   Rivals' news shows top right for four seconds, their chat bottom right for two. The result card lets the winner save the
   time to the ladder. Nothing of this has been tried against the server yet.
6. **Android and 3DS** join the same race, each with its own hold. This is where polling on a real 3DS gets measured.
   Built, not tried yet. Android has `RaceClient.kt`; the game runs in its own process, so the token waits in
   `runs/race.token` and `Smz3RaceOverlay.kt` picks the room up there. The 3DS client lives in `smz3online.cpp` and plays the
   race in slot 33. On the 3DS the rivals' chat and times go on the touch screen's strip, and the end is read once a second,
   so its finish can arrive up to a second later than on PC. Closing the 3DS app mid-race sends nothing, so the server drops
   the player after five minutes; Android sends the leave as the game closes.
7. **Multi race,** positions, abandon rules, Partial lengths, pause by agreement, ladder.
   Built on the server and PC, tried live against the server with a scripted second player. In a multi race the card
   shows the places so far when you finish, and the first finish can go to the ladder at once, since nobody can beat it.
   A pause is asked from the abandon prompt with a resume time from 1 hour to 7 days, and the game stops for everyone
   until the vote is settled. Once paused, each PC keeps its state and the room's token, and Championship shows the paused
   race first; choosing it loads the state once and waits for the others. A finish sent while the server was down
   carries its own moment, which counts only inside an outage the server recorded. Requests that can't be lost (finish,
   ladder, vote, back) are sent again until the server answers. After 5 minutes without the server the race shows as
   cancelled. On two screens RUN lists the top three and you.
8. **Chat,** with the touch keyboard on 3DS.
   Built on PC: a CHAT tab in the tracker overlay, which no longer pauses a race. The keyboard types; a pad sends quick
   phrases. Players can be muted and chat turned off. On two screens the CHAT tab has the phrases by touch.
9. **Draft start points.** Try one spot in each game, on all three emulators: the SRAM idea for Super Metroid, and a boot patch or prepared state for Zelda. If Super Metroid fails, Draft has to be rethought; if only Zelda fails, Draft starts in Super Metroid.
   Done, and both games work as plain ROM patches, with no SRAM or state to prepare. `smz_rom_start_sm` and
   `smz_rom_start_z3` in `core/gen/smz3_rom.c`. A new game sets its load station at `$81:811D`, in the branch for a file
   with nothing to load; the patch calls a routine at `$85:FF80` that writes the area and station both to the live game
   and to the values the first save keeps. The Zelda start runs after the new game's first save (`$81:FDC7`) and jumps
   into `sm_do_teleport`, the routine the portals already use, with one of the four entries of its table. So Zelda starts
   are the exits of the portals from Super Metroid: the light world fortune teller, another light world cave, the dark
   world ice rod cave and the Misery Mire fairy. Checked on the PC core (the same libretro snes9x Android uses) with the
   headless self test, and on the 3DS build in Azahar, where the tracker showed the new file starting in Zelda and in
   Brinstar. Azahar's picture is too distorted to read, for this core and for normal runs alike, so the 3DS check went
   by the tracker.
10. **Draft goals.** New goal kinds, a generator with its own length estimate, its own finish check. Start spots are a hand-made list, and each one names the items needed to leave it, so a Draft's starting items can't trap the player. Count goals and Super Metroid boss goals can't be proved, so the server trusts the client and only checks they are plausible.
    Built. `smz3daily --draft <id>` makes a Draft from the daily's generator and logic. It picks:
    - **A start spot** from a hand-made list. Super Metroid has the Landing Site, the Crateria save room, Green, Pink and
      Red Brinstar, Kraid's Lair and the Wrecked Ship; Zelda has the fortune teller's and Death Mountain. Each spot gives
      the items it needs, and the spot counts only when the logic, with the starting items alone, reaches its region from
      the ship. If the way in is open, so is the way back.
    - **One to three random starting items** on top, never rupees.
    - **A goal:** defeat 15 to 35 enemies, collect 50 to 130 rupees, defeat a boss (Kraid, Phantoon, or a light or dark
      world dungeon boss up to Kholdstare), or get a crystal. For a boss or a crystal, the items of its shortest route
      become starting items too, and the seed is filled again with them. Every goal is open from the start. The kind of
      goal is fixed per Draft, so the slow kinds come up as often as the quick ones.
    - **An estimate,** kept between 5 and 15 minutes: 0.3 minutes per enemy, one per 12 rupees, 6 or 8 for a boss, 9 for
      a crystal, plus 2.5 when the goal is in the other game.

    On the server, `race.js` runs it as `smz3draft`, a copy built for the app version that plays Drafts, so the daily's
    `smz3daily` stays as the released apps expect. It keeps six Drafts ready. A Draft finish is trusted: enemy and rupee
    goals open no check, so there is no proof to verify, and only the time is checked (at least 30 seconds). The race ends
    30 minutes after the start. A multi race with no finisher goes to the highest %, a 1 vs 1 one is a draw. Drafts never
    go on a ladder.

    In the apps, the run's counters gained `kills`, any enemy in either game. The goals gained `kills:<n>` and
    `rupees:<n>`, read from the counters through `SmzTracker.counts`. `smz_race_mark_settings` keeps the room's goal and
    start in the run, and `smz_race_apply_start` writes the start into the ROM. The goal shows under the 3, 2, 1 and then
    as a line at the top left with its % and the time left. The progress line is the goal's own %. Played on PC against
    a bot on the live server: the room list, the goal at the countdown and on the top line, and new games that start
    at a Brinstar save station instead of the ship.
11. **Balance.** Real times adjust the lengths.
    Ready for the data. Each Draft result keeps its goal, spot and estimate, and `node server/ladder/balance.js` on the
    server prints, per goal kind, the winners' median time and its ratio to the estimate, and the winners' times for
    Sprint, Half and Full. A ratio far from 1 for a kind means changing its numbers in `draft_estimate` (or the counts in
    `pick_draft_goal`) and rebuilding `smz3draft`. There are no real races yet to tune from.

## Risks

- **TLS on an Old 3DS.** Each poll may need a new handshake, and that may be slow. If so: a slower poll on 3DS, or plain HTTP
  for the poll alone, with a signed token. To measure in step 6.
- **`curl` per poll on PC.** A new process every 2 seconds on a background thread. Probably fine, and `curl` becomes a hard
  requirement for races, which the README doesn't mention yet.
- **Speed across versions.** If a version drops frames, its player is slower in game time. Real time is what counts here, so a
  weak device is a handicap.
- **Trust.** Events and the % come from the client, and the proof covers the checks opened, not the finish. A modified client could send fake notes or a coherent fake finish and win a race. The ladder already accepts this limit, and races put no rating or rupeergies at stake.
- **Spoilers.** All players share a seed, so "Rival got Morph Ball" gives away little, but showing the place would give away a lot.
  Notes carry the item, never where it was.
- **The move.** Live ladder and daily data have to arrive whole, and a window of the daily may be open during it.
- **Pause and trust.** A paused client holds a state and could stop to think with the game frozen, or look at the map. The seed is also known, so a player could play it in another emulator during the pause. Everyone agreed to the pause and has the same chance, so this is accepted.
- **Scouting before the start.** The ROM is on the device before the 3, 2, 1, so a player can stall in the ready room and explore it elsewhere, up to the 1 hour limit. The creator can remove a staller. Accepted for now; if it becomes a problem, the seed can go out on the last confirm, with `startAt` set once every client has its ROM, at the cost of a longer wait.
- **Lengths.** The daily's estimate was built for 30 to 90 minutes. 5 to 15 minutes needs its own numbers, and a goal in the other game has to fit the trip between games in that time.

## Open questions

None right now.
