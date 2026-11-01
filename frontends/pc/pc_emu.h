/* The emulator (snes9x, linked in through its libretro interface) as a small C API for the PC frontend. */
#ifndef PC_EMU_H
#define PC_EMU_H

#include <stddef.h>
#include <stdint.h>

/* Joypad bits, the order of the libretro joypad ids. */
enum { PAD_B = 1 << 0, PAD_Y = 1 << 1, PAD_SELECT = 1 << 2, PAD_START = 1 << 3, PAD_UP = 1 << 4, PAD_DOWN = 1 << 5,
       PAD_LEFT = 1 << 6, PAD_RIGHT = 1 << 7, PAD_A = 1 << 8, PAD_X = 1 << 9, PAD_L = 1 << 10, PAD_R = 1 << 11 };

/* Loads a ROM (the buffer is copied). Returns 0 or -1. */
/* path is where the ROM lives (so MSU-1 music files next to it are found); NULL when it has none. */
int emu_open(const uint8_t *rom, size_t len, const char *path);
void emu_close(void);
int emu_sample_rate(void);
void emu_set_pad(int bits);
void emu_run_frame(void);
void emu_reset(void);

/* The last picture: XRGB8888, valid until the next frame. */
const uint32_t *emu_picture(int *w, int *h, int *pitch_px);
/* Audio produced since the last call: interleaved stereo s16 samples (count of frames). */
const int16_t *emu_take_audio(size_t *frames);

/* Work RAM (128 KB) and the cartridge save RAM; either may be NULL when not available. */
const uint8_t *emu_wram(size_t *len);
uint8_t *emu_sram(size_t *len);

size_t emu_state_size(void);
int emu_state_save(void *buf, size_t len);
int emu_state_load(const void *buf, size_t len);

#endif
