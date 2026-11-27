Changes on main since v0.15.0, for the next release's notes. Not built or tested together yet.

### Menus

- A ROMs screen comes first on Android and the 3DS while the Super Metroid or ALttP ROM is missing (the PC already had
  its welcome screen). Two cards show which ROM is found and which exact dump is needed. Add ROMs opens the file picker
  on Android and looks in the folders again on the 3DS. Once both are in, the screen says "All good! Start playing!" and
  Play turns green.
- Android: the carousel's Link and Samus pictures show as soon as the ROMs are added, without restarting the app.
- The game carousel's turn eases to a stop instead of ending with a small jump.
- Continue fades to black from the run's menu; the run list no longer flashes before the game.
- One button legend style on every screen, the game picker's included.

- The game carousel starts on the game entered last, and each game's run list on the slot opened last, also after
  closing the app.
- Credits open with "SMZ3 Universe made by Doctor Utsugi"; the menu music is credited to Doctor Utsugi.

### In game

- The CONFIG tab ends with Back to main menu, which asks first and saves the run as leaving does. On the PC it is also
  the last row of the in-game settings (in place of Quit), and on the 3DS the last row of the pause menu.
- Single screen on Android: the d-pad moves through the open tracker's page (up and down by rows, left and right one
  button at a time) and A presses what is outlined; a page with no buttons scrolls, and down past its end reaches
  Close. Pads whose d-pad is a hat axis work
  too.
- Single screen with a controller: a quiet "R3 Tracker" in the bottom left corner says which button opens the tracker.
  The PC's hint moved to the bottom left as well.
- Android: the tracker over the game opens at once. Its pages are built a moment after the game starts instead of on
  every open.

### Online

- Online draft in A Link to the Past and Super Metroid, from the usual start with a 5 to 15 minute goal.
- Monthly and weekly runs show how many players are on the seed right now: "Playing: N" on the New run row, and a
  green tag on a saved run of that kind (plus a line in its menu). Only runs sent to the online ladder count, and
  nothing shows while nobody plays. A run counts from its first minute (once it has its token) and the menu asks
  again every minute. The server needs the new /api/playing (not deployed yet).

### Tests

- One file sends every online call to a test server: a `server.txt` holding its address, on Android (in the app's own
  Android/data folder, which it can always read), the 3DS and the PC. Android's ladder and cloud sync and the 3DS's sync
  did not follow it before. The menu shows STAGING while it is in use.

### A Link to the Past

- Quickswap is on by default: L and R change the Y item.
- Tapping an item in the tracker equips it; on Android L3 gives the pad back to the game.

### Races

- Races and Drafts tell the rivals each major item picked up ("got the Hookshot", "got the Titan's Mitt"), as the
  design always said; until now only bosses, pendants and crystals went out. Hearts, tanks, ammo and bottles stay quiet.

- The race standings are a horse race: one lane per player with a runner at their %, on the second screen's RUN tab
  (Android, PC, 3DS) and in the side panel beside the game on one screen (Android, PC).
- A Draft whose goal is a boss, a dungeon, an area or an item moves on the standings too: the major items found give up
  to half, being inside the goal's dungeon or area 70% and the boss's room 85%. Crystal and boss counts go by how many
  are done. Before, those goals stayed at 0% until the finish.
- Fast forward in an online race or Draft says so, not "daily challenge".

### Controls

- Android: Options > Buttons and keys, and the CONFIG tab's "Change the game's buttons", open the controller
  settings, where each game button takes any key of the pad.
- Android: the touch controls can be shown with a controller connected (CONFIG > Touch controls, off by default) and
  moved and resized from there. With no controller they show on their own, as before.
- One window (PC, and Android in single-screen landscape): the side panels beside the game hide and come back with a
  shortcut, H on the PC keyboard (or a pad button set in the options) and L2 + R3 on Android.

### Updates and data

- Cloud sync has no built-in RomM server: Options > Cloud sync > RomM address takes your own (a host, or a full
  address with port).
- 3DS: a save state left in the old 5th slot (versions up to v0.14.0) moves to the first free slot of the four.
- Android: when an update is signed with another key, the app copies the runs and sign-ins to /sdcard/SMZ3/backup and
  says to reinstall; the reinstalled app takes them back on its first start.
- Android no longer asks for the microphone.

### 3DS

- On an Old 3DS the rewind history and the tracker redraws wait for a frame with room.
