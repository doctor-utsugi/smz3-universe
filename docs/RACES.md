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
| Leaving | Anyone can abandon; the result is settled from who is left. |
| Ladder | The winner can choose to save their time on a ladder. |
| Chat | Text chat between the players of a race. On one screen it opens only from the tracker overlay, and a new message shows for 2 seconds at the bottom right of the game. |
| Server | Everything on salamando: the race server with Redis for live races, and the ladder and daily moved there too. |
| Options menu | Buttons, resolution and basic settings, nothing that configures a run. |
| Finding a race | Private rooms by code, with a field to enter one, and a list of open public rooms to search. |
| Room expiry | Redis expires rooms that never got going: 1 hour of waiting at most. |
| Draft goal | Random only. The server picks the goal and the start. |
| Draft pool | The server prepares and checks a reserve of Drafts ahead of time, each with fixed starting items, and takes one without repeating when a room closes. |
| Spectators | Not for now. |
| End of a multi race | The first finish starts a countdown set by the type of race: Draft 5 minutes, Sprint 15, Half 30, Full 60. Whoever hasn't finished by then gets no position. |
| Partial lengths | Sprint (2 crystals and 1 Super Metroid boss) and Half (4 and 2). Anything longer is the Full game. |
| Draft games | It can start in either game, and the goal can be in the other one. |
| Server down | The game goes on and each client keeps what happens, sending it when the server is back. Back within 5 minutes, the race continues; if not, it is cancelled. |
| Draft goal shown | At the 3, 2, 1, the same for everyone. Nobody plans a route before the start. |
| Draft limit | 30 minutes. If nobody reaches the goal it is a draw; in a multi race the highest % wins. |
| Championship rules | Fixed, the Original ladder settings, for Full and Partial. Private rooms can also pick Custom, which never goes on a ladder. |
| Long races | The players can pause by agreement and carry on another day. Everyone must vote yes, and only the Championship Full game can pause. |
| Pause limits | No limit on pauses, but the race expires after 7 days. If someone is missing when it resumes, the others go on after 10 minutes and that player has abandoned. |
| Paused times | They go on the ladder with a "paused" mark. |
| Identity | The name and `install_id`, with no accounts. A reinstall loses the history, and two players can share a name. |
| Chat control | Each player can mute another or turn chat off. The server limits how many messages and how long. No word filter. |
| Ready room | No fixed time. The creator removes whoever doesn't confirm, and the 1 hour limit still applies. |
| Room setup | The creator picks mode and format. With 2 or more players they close the room, and everyone goes on to skins and the ready room. |
| Ranking | Ladders by time, one table per mode and length, with no player rating and no rupeergies for races. |
| Rival notes | Only major items, bosses and crystals, and the goal's progress in a Draft. Notes and chime can each be turned off in Options; the standings on two screens stay. |
| Daily challenge | Stays in the run list, under Start/Load game. |
| Moving servers | Before the races, between two daily windows, with a short outage. Only the address's target changes. |
| Creator leaves | The room passes to whoever has been in it longest, with the same code. |
| App version | Everyone in a room needs the same version. A mismatch can't join and gets a message saying to update, because a different generator would build a different seed. |
| Abandon | On two screens it lives in CONFIG, by touch. On one screen it's in the existing exit menu. It always asks for confirmation. |
| Standings on RUN | At most four rows, the top three and you. All the players appear in the final table. |
| Dropped connection | The player can return to the same race for 5 minutes, with the clock running. After that it counts as abandoning. A closed app counts the same. |

## What already exists

Most of the hard parts have a working first version in the daily challenge.

| Need | Already there | Missing |
|---|---|---|
| Same seed on every version | `smz3daily` plus `smz_daily_*`: the server hands out settings and seed, every client generates the same placement | Nothing for Championship |
| Goal read from memory | `smz3_goal.c`: bosses, crystals, items, areas | Enemy and rupee goals |
| Enemy and rupee counts | `smz3_counters.c`: rupees, enemy kinds, bosses | A goal kind that reads them, and a total across all enemy kinds |
| Rules forced off | The `challenge` flag blocks states and fast forward on every host | Check that rewind and every hotkey respect it |
| Server clock | `online_now_ms` and the signed start token | Offset good to a fraction of a second for a shared start |
| Finish check | `smz3check` replays the proof | `MIN_MS` is 10 minutes, so a Draft finish would be refused; Draft goals need their own check |
| Name, country, install id | Profile and `install_id` | Nothing |
| Skins | Sprite pickers on every version | A way to show a rival's skin the player does not have |
| Text entry with a pad | `UI_NAME` grid in the shared menu | Using it for chat |
| Second screen | PC dual mode, Thor, 3DS | A CHAT tab on each |

Two things in there change the order of the work:

- **Draft needs a start the game does not have yet.** `docs/DAILY.md` phase 8 only marks Super Metroid bosses as dead. Starting
  in a random place is the prepared save that was left for phase 9.
- **Save states cannot be that start.** The three emulators are different builds of Snes9x, so a state from one does not load
  on another. The proposal is to build it from SRAM, which is plain game data: a prepared file that loads at a Super Metroid
  save station or a Zelda start point, with its items and progress set. This is unproven and has to be tried first.

So Championship comes first, because it needs none of that and proves all the online parts.

## How it fits together

```
 core/ui/smz3_race.c   room state, ready room, events, standings (shared C, no network code)
        |
 hosts: pc_online.c, OnlineLadder.kt, smz3online.cpp   one HTTP call each, on a background thread
        |
 salamando: server/race/ (Node) + Redis                live rooms, with an expiry
        |
 ladder store                                          results kept for good
```

The menus go in `core/ui`, so they are written once. The in-game parts (corner note, message dialog, chat) are not shared: PC is
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
| `POST /api/race/create` | Mode, format, size. Returns the room, its code and a token |
| `POST /api/race/join` | Enter by code, or from the public list |
| `GET /api/race/open` | The open public rooms, by mode and format |
| `POST /api/race/pick` | The two skins |
| `POST /api/race/ready` | Confirm. The last one sets `startAt` a few seconds ahead |
| `POST /api/race/poll` | Above |
| `POST /api/race/finish` | Time and proof |
| `POST /api/race/leave` | Abandon |
| `POST /api/race/ladder` | The winner keeps their time |

Redis keys, expiring after a few hours, or 7 days for a paused race: `race:<id>` (state), `race:<id>:log` (events and chat), `code:<code>` (room by
code). Finished races go to the permanent store. The server code uses no packages today, so it either takes one Redis
client or speaks the protocol itself.

### The shared start

The server sends `startAt` in server time and each client works out its offset from a few polls. Everyone sees a 3, 2, 1 and
the game starts on the same second. The seed and the ROM are generated **before** the ready room closes, so a slow 3DS
doesn't lose time.

### Pause by agreement

The rule against save states has one exception here. When every player agrees, each client saves one state of its own game
and the server stops the clock. The state stays on that device, can be loaded once, and only through the race's token; it
isn't available from any menu. To carry on, everyone goes back through the ready room and the 3, 2, 1. A state can't move
between devices, so a player must return on the same one.

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
| 3DS | System keyboard on the bottom screen |
| Handhelds | Quick phrases, and the grid of `UI_NAME` |

## Work in order

1. **Sign-off.** Questions below answered, mockups approved.
2. **Start menu and Options** on every version. No network, and useful alone.
3. **Move to salamando.** The ladder and daily with their data, keeping `smz3.indieviral.com` as the address, because released versions have it built in.
4. **Server.** Rooms, poll, ready, finish, on salamando, with Redis.
5. **Core and PC.** `smz3_race.c`, skin pick, ready room, shared start, Championship Full 1 vs 1.
6. **Android and 3DS** join the same race. This is where polling on a real 3DS gets measured.
7. **Multi race,** positions, abandon rules, Partial lengths, ladder.
8. **Chat.**
9. **Draft start points.** Try the SRAM idea on one spot, on all three emulators. If it fails, Draft has to be rethought.
10. **Draft goals.** New goal kinds, a generator with its own length estimate, its own finish check.
11. **Balance.** Real times adjust the lengths.

## Risks

- **TLS on an Old 3DS.** Each poll may need a new handshake, and that may be slow. If so: a slower poll on 3DS, or plain HTTP
  for the poll alone, with a signed token. To measure in step 6.
- **`curl` per poll on PC.** A new process every 2 seconds on a background thread. Probably fine, and `curl` becomes a hard
  requirement for races, which the README doesn't mention yet.
- **Speed across versions.** If a version drops frames, its player is slower in game time. Real time is what counts here, so a
  weak device is a handicap.
- **Trust.** Events come from the client. The proof check covers the finish, not the notes, so a cheater could send fake ones.
- **Spoilers.** All players share a seed, so "Rival got Morph Ball" gives away little, but showing the place would give away a lot.
  Notes carry the item, never where it was.
- **The move.** Live ladder and daily data have to arrive whole, and a window of the daily may be open during it.
- **Pause and trust.** A paused client holds a state and could stop to think with the game frozen, or look at the map. Everyone agreed to the pause, so this is accepted.
- **Lengths.** The daily's estimate was built for 30 to 90 minutes. 5 to 15 minutes needs its own numbers, and a goal in the other game has to fit the trip between games in that time.

## Open questions

None right now.
