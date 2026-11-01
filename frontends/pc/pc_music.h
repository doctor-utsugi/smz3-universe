/* Custom music through MSU-1: the same package layout as the Android app (see Smz3Music.kt). A package is a folder of .pcm tracks under
 * <data>/music/<name>/; files named <anything>-N.pcm replace track N, the others form a pool dealt out at random for the run's seed. */
#ifndef PC_MUSIC_H
#define PC_MUSIC_H

/* Names of the package folders that hold tracks (at most max, each below 32 bytes); returns how many. */
int music_packages(char names[][32], int max);
/* Links the tracks for the run whose ROM is rom_path ("*.sfc") from its settings JSON and seed, or removes the links when it keeps the
 * game's music. Returns the number of tracks linked. */
int music_prepare(const char *rom_path, const char *settings_json, const char *seed);

#endif
