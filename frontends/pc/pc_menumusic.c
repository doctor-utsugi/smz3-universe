#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#include <SDL.h>
#include "pc_menumusic.h"

#define FADE_FRAMES(rate) ((rate) / 2)   /* half a second */

extern const unsigned long PC_MENU_MUSIC_SIZE;
extern const unsigned char PC_MENU_MUSIC[];

static drmp3 mp3;
static SDL_AudioDeviceID dev;
static int tried, playing;
static volatile int fade_left = -1;   /* frames left of a fade out, -1 when not fading */

static void fill(void *user, Uint8 *stream, int len)
{
    (void)user;
    drmp3_int16 *out = (drmp3_int16 *)stream;
    drmp3_uint64 want = (drmp3_uint64)len / (2 * sizeof *out), got = 0;
    while (got < want) {
        drmp3_uint64 n = drmp3_read_pcm_frames_s16(&mp3, want - got, out + got * 2);
        if (!n) { if (!drmp3_seek_to_pcm_frame(&mp3, 0)) break; continue; }
        got += n;
    }
    int fade = fade_left, total = FADE_FRAMES((int)mp3.sampleRate);
    for (drmp3_uint64 i = 0; i < got; i++) {
        int g = fade < 0 ? total : fade > 0 ? fade-- : 0;   /* half volume, scaled down while fading */
        out[i * 2] = (drmp3_int16)(out[i * 2] * g / (2 * total));
        out[i * 2 + 1] = (drmp3_int16)(out[i * 2 + 1] * g / (2 * total));
    }
    if (fade_left >= 0) fade_left = fade;
    SDL_memset(out + got * 2, 0, (size_t)(want - got) * 2 * sizeof *out);
}

static int open_dev(void)
{
    if (!dev && !tried) {
        tried = 1;
        if (!drmp3_init_memory(&mp3, PC_MENU_MUSIC, PC_MENU_MUSIC_SIZE, NULL)) return 0;
        SDL_AudioSpec want = {0};
        want.freq = (int)mp3.sampleRate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048; want.callback = fill;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (!dev) drmp3_uninit(&mp3);
    }
    return dev != 0;
}

void menumusic_fade(void)
{
    if (playing && fade_left < 0) { SDL_LockAudioDevice(dev); fade_left = FADE_FRAMES((int)mp3.sampleRate); SDL_UnlockAudioDevice(dev); }
}

void menumusic_set(int on)
{
    if (on == playing || (on && !open_dev())) return;
    if (!on) {
        menumusic_fade();
        while (fade_left > 0) SDL_Delay(10);
        SDL_PauseAudioDevice(dev, 1);
    } else {
        SDL_LockAudioDevice(dev);
        drmp3_seek_to_pcm_frame(&mp3, 0);
        fade_left = -1;
        SDL_UnlockAudioDevice(dev);
        SDL_PauseAudioDevice(dev, 0);
    }
    playing = on;
}
