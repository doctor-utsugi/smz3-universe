# World goals

Every month the players work together on a few goals ("defeat 30 Kraids", "collect 50,000 rupees"). Whoever helped
with enough of a goal counts in it, and when the month ends the server pays rupeergies to everyone who counted.

## Rules

| Topic | Rule |
|---|---|
| Month | UTC calendar month. On the first request of a new month the server picks its goals; they all close when the month ends. |
| Goals per month | `perMonth` in the catalog (4): one Zelda goal, one Super Metroid goal, one shared goal, then one more from what is left. The pick is seeded by the month, so it is the same after a restart. |
| Target | The catalog's `target`, or what everyone reached in that key the month before times `growth` (1.1), rounded up to two figures, whichever is higher. |
| Counting in a goal | Your total in it reaches the goal's `min` (1 Kraid, 10 Moblins, 1,000 rupees...). |
| Rupeergies | Counting in n goals pays n(n+1)/2 (1, 3, 6, 10...), times (met + missed / 2) / n. Example: 3 goals, 2 met: 6 x 2.5 / 3 = 5 rupeergies. Paid into the same balance as the daily challenge, once, after the month ends. |
| Which runs | All of them. `onlineOnly: true` in the catalog makes only online runs (original rules, monthly, weekly, daily) count. |
| Daily challenges | The "Finish daily challenges" goal is counted by the server from its own results (`"server": true`), never from the devices. |

## The catalog

`server/ladder/goals-catalog.json`, read on every request, so it can be edited on the server without a restart. Each
goal has `key` (the counter's name, see below), `group` (zelda, metroid, shared), `title`, `sprite` (the icon),
`target`, `min` and `perRun` (the most one run can add: bosses are 1).

A month can be set by hand in `data/goals-override.json`, which wins over the pick:

```json
{"2026-12": [{"key": "kraid", "target": 40}, {"key": "moblin", "target": 300}, {"key": "items", "target": 9000}, {"key": "daily", "target": 70}]}
```

Set it before the month's first request: once a month has its goals they are kept in `data/goals.json`.

## Counting on the device

`core/ui/smz3_counters.c` reads the emulator memory every 100 ms or so, next to the tracker:

- **Zelda enemies.** A sprite slot ($0DD0) entering state 06 (dying) counts its type ($0E20): Moblin 12, soldiers
  41-4B, Octorok 08/0A, Stalfos A7/85, Hinox 11, Lynel D0, Keese 6F, Rope 6E. Only in the overworld or a dungeon
  (module $10 07 or 09).
- **Super Metroid enemies.** An enemy ($0F78 id, $0F8C hit points) that had hit points and then has none, or is gone,
  in the same room with the game in play ($0998 = 8): Metroid DD7F, Space Pirates F353-F593, Zoomer DCFF, Sidehopper D93F.
- **Bosses**, once per run, from the save data: Zelda boss rooms (0x0800 in the room's word at $7EF000 + room x 2) and
  the Super Metroid boss bits ($7ED828 + area). Bosses already dead when counting began (a prepared daily start) never count.
- **Rupees**: rises of $7EF360 of at most 300 between two reads. **Trips**: each change between the games. **Items**:
  locations cleared. **Runs**: the credits.

The enemy ids and states come from the games' known RAM maps and have not been checked in play yet.

The counters are saved with the run (`goals=` in the PC and 3DS run file, `slotN.goals` on Android) and beside every
save state (`slotN.qK.goals` on PC, `slotN.goals.stateK` on Android, the same idea on 3DS). Loading a state puts them
back to that point, so repeating a stretch with a state adds nothing.

## Server

`server/ladder/goals.js`:

- `GET /api/goals?install=` -> `{month, endsAt, onlineOnly, goals: [{key, title, sprite, target, total, min, mine}], counted, gemsIfMet, last?, prev?}`
- `POST /api/goals/report {install, run, online, counts, player?, country?}` -> `{ok}`

`prev` is the month before: `{month, counted, gems, goals: [{key, title, sprite, target, total, mine, rank, players, top}]}`,
where `top` is the goal's ten biggest contributors as `name|CC|count;...` and `rank` is the install's place (0 when it
gave nothing). The name and flag are the ones the device sent with its last report (or its last daily challenge).
The ladder's World tab switches between this month and last month; A on one of last month's goals opens its top 10
with each player's share of the total.

A report carries a run's totals so far, not what changed, and the server keeps the latest one per run (`run` is
`s<slot>-<created>`). A total that goes down after a state load takes back what was reported. Each counter is cut to
the goal's `perRun`, an install can report `runsPerInstallPerMonth` runs (60), and reports are limited per IP and hour.
A run that began the month before only adds what came since.

The devices report every 5 minutes while playing and when the run is left. The run list's World goals row and the
ladder's World tab ask for the goals when they come on screen and then once a minute.

## Icons

The core draws a goal's icon as the sprite preview `goal/<sprite>`. The hosts look for `goals/<sprite with _>.png` in
the app's assets: `tools/prepare_assets.py` copies the ones the tracker's sprite set has (the Super Metroid bosses,
rupees, chest, portal). The others are read from `goals/<name>.pv` in the data folder, cut from the player's own ROM.

## Deployed

2026-10-31 to the live server: `server.js`, `daily.js`, `goals.js` and `goals-catalog.json`.
The first month made was October 2026, three hours before it ended.
