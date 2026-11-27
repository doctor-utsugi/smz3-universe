Changes on main since v0.15.0, for the next release's notes. Not built or tested together yet.

### Android: games start again

- v0.15.0's APK went out without the snes9x core, so no game could start ("the libretro core was not loaded"). The
  build puts it back and now stops if an APK comes out without it.
- Android: the daily challenge's game froze on a black screen from its first frame (v0.15.0 too); it plays again.
- 3DS: Link, Samus and the logo fill their cards on the game carousel, as on the other systems.
- 3DS: in an A Link to the Past run the map opens zoomed on one world and follows Link.
- Thor: the second screen's own Back button closed SMZ3's bottom screen and left the system's app drawer there for good;
  it now stays.
- 3DS: closing the app ended on a crash screen ("An exception occurred"), because the menu music's file was still
  open when the app let go of its own files; it now closes cleanly.
- 3DS: leaving a run for the main menu fades both screens to black, and the menu comes up out of it.
- 3DS: the logo on the game carousel is drawn from a larger picture instead of a blown-up small one.
- A single game's seed code (ALttP's item icons, Super Metroid's text) is hidden on the RUN tab and the side panel
  unless Options > Seed code is set to shown. On the 3DS it has its own "SEED CODE" line, after the rewards.
- 3DS: after a run's end the game no longer slows down; its file was read and written on the SD card every second.
- Android's app settings show the SMZ3 version (0.15.0 and on) instead of 1.17.0, the version of Lemuroid it is built on.

### Menus

- PC and handhelds: the second screen's ladder could stay on "No times yet" when the tables were asked for twice at
  launch; each request now has its own buffer.
- The ROMs screen moves its focus to Play once the second ROM is found, instead of leaving it on Add ROMs.
- Android: the app no longer asks for the notifications permission on its first start; the system's dialog sent it to the
  background, and games run without it.
- The STAGING badge of a test server sits at the top middle, clear of the Ladder button and the rupee counter.
- Races: a player who stops answering is out after 2 minutes (it was 5), and the last one still playing wins. While
  only one is left, the room says so every 30 seconds with the time to the win.
- Races on Android: the screen stays on in a race room, and a start that came with the app in the background opens the
  game once the app is back, instead of leaving it on the run list.
- Race server: a restart of the server no longer counts players who were already away as back.
- Android: a race's countdown and start went by 200 ms steps, so phones counted and started up to 0.2 s apart; each
  count and the start now come on their own moment, within a few ms of the other systems.
- 3DS: a race started about 300 ms before the other systems. Its estimate of the server's clock halved a trip whose
  way out (opening the connection) is slow and uneven; it now goes by when the answer arrives.
- Championship and Online draft ask for a player name first when none is set, instead of showing others a made-up
  "Player-xxxx".
- Races: a player the creator removes, or whose room expired, goes back to the race menu with "You were removed from
  the room" or "The room closed", instead of staying on the room's list with "not in this race".
- PC: leaving a race already finished or won is "Leave the run?" again, not "Abandon the race?".
- PC: the race's end card says how it ended for a player with no time, like Android: "The others left", "X won",
  "Time is up" or "No result".
- macOS: the Intel build opens on macOS 11 and later; it asked for macOS 15.
- PC in one window with no room for the side panels (a 640 x 480 handheld like the RG 40XXV): the %, the clock and a
  race's lanes show in the game's top left corner. The side-panels key hides them too.
- PC on Knulli and Batocera handhelds (the RG 40XXV): A and B, and X and Y, were swapped, and L2 / R2 did nothing where
  they are plain buttons. The system hands games a pad mapping by place, as on an Xbox pad, that SDL let nothing
  replace; the app now sets each button from the system's pad config by its printed letter.
- PC on a pad with no R3 (the RG 40XXV): the tracker opens with L3, and the menu button leaves the run.
- PC and Linux handhelds: the start menu has Quit under Options, so the app closes without going into Options. Quit
  asks "Quit SMZ3?" first.
- Android: a phone with less than 2 GB of RAM, or 1.1 GB free, downloads its Map Rando seed from the server instead of
  building it, which closed the app. The screen says "Downloading the Map Rando seed", or "Loading" while the phone builds
  one, instead of a count of seconds and tries.
- PC: a Map Rando seed is built on the device only when it has 2 GB of RAM and 1.1 GB free (building one peaks near
  780 MB); otherwise (a 1 GB handheld like the RG 40XXV) the server builds it and sends a small patch, as on the 3DS. Before, the system closed the app halfway.
- PC: while a Map Rando seed is made the screen says "Loading", or "Downloading the Map Rando seed" when the server
  makes it, not a count of seconds and tries. Offline, it asks to connect to the internet.
- PC and 3DS: the game comes with 32 Map Rando seeds, 8 for each preset. When a seed can be neither built nor
  downloaded (offline, or the server fails), a run with a preset and no skill change plays one of them, and the Map
  Rando row says so. Races, the weekly and the monthly never use them, so everyone there plays the same map.
- 3DS: while the server makes a Map Rando seed the screen says "Downloading the Map Rando seed", not a count of seconds.
- Linux handhelds: the Ports launcher unpacks the app on the card instead of /tmp, which is RAM there, and clears a copy
  left by a run the system closed.
- PC: a run that cannot be made shows why in a box you close with A, instead of a line that the screen cut short.
- PC: one Config for one and two screens. The tracker's CONFIG tab (two screens, by touch) and the Settings screen (one
  screen, by pad or keyboard) now show the same list: save slot, fast-forward speed, screen and daily challenge settings,
  each where it means something (the window size only in one window, the shortcut rows only on the touch screen).
  Fast-forward on one screen now follows its speed setting, not a fixed x4. Fixes the CONFIG tab's Screen shape and
  Screen mode rows, which changed the minimap and the corner info instead.
- PC: the button that opens the tracker shows for 5 seconds at the start, then fades out.
- PC: the D-pad moves the map in the tracker's MAP tab, as the stick and the arrow keys did. On a pad with no R3
  (the RG 40XXV) only the D-pad does: its stick is pressed to open the tracker.
- PC: Options > Video > Corner info picks what the game's corner shows when the side panels don't fit: the % and the
  time, the % only, or nothing.
- PC on a handheld: the prompts show the pad's buttons from the first screen, not the keyboard's keys.
- PC on handhelds whose buttons come as a virtual PlayStation pad (the RG DS with InputPlumber): the prompts show the
  console's own letters, not PlayStation's shapes.

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

- Android, one screen: the daily's window, time, goal and next hint sit in the left side panel, as a Draft's goal does,
  instead of a box over the right panel. The side panels and the daily's notes use the menus' font.

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
- Android on one screen: a Draft's goal and its time left sit in the side panel above the lanes, instead of over the
  panel's own %. With the panels hidden it stays at the top left of the game.
- A Draft that asks for one enemy reads "Defeat an enemy", not "Defeat 1 enemies".
- Android: the end of a race is the run's end card, the same celebration as a finished run: the title letter by letter,
  your time, the standings, and the rays, confetti and fanfare when you win. A keeps playing (or saves the time to the
  ladder), B goes back to the menu. Before, it was a plain system dialog.
- PC and 3DS: winning a race plays the run's end fanfare.
- Android: leaving a race from its menu now tells the server at once, so the rival wins straight away. Before, the
  game closed before the leave went out and the rival waited five minutes for the win.

### Android: no system dialogs

- Leaving the race, asking to pause, the vote, the wait for the answers, the paused race, leaving the run, a daily's
  "Time is up" and the error prompts are drawn like the menus (cut corners, the chosen row glowing, the button bar),
  not as Android dialogs. The race's notes on the game use the menu font.
- Options > Buttons and keys (and the CONFIG tab's "Change the game's buttons") is drawn like the menus and laid out
  like the PC's page: each SNES button with what it does in the two games and the controller button on it, Reset to
  defaults, and a six-second wait for the new button. L and R switch controllers when there are several.

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
