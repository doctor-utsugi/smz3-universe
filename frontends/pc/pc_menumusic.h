/* The launcher's music (assets/music/smz3_intro.mp3, built into the binary): loops at half volume on a device of its own. */
#ifndef PC_MENUMUSIC_H
#define PC_MENUMUSIC_H

/* 1 plays from the start, 0 fades it out over half a second (waiting for the fade) and stops; the first 1 opens the device. */
void menumusic_set(int on);
/* Starts the half-second fade out without waiting, for a game about to start; menumusic_set(0) then stops at once. */
void menumusic_fade(void);

#endif
