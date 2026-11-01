#include <math.h>
#include <SDL.h>
#include "pc_sfx.h"

#define RATE 44100

static SDL_AudioDeviceID dev;
static int tried;

/* A short note: a sine with a touch of its octave, a quick attack and an exponential tail, added into out from start. */
static void note(float *out, int len, int start, float hz, float ms, float vol)
{
    int n = (int)(RATE * ms / 1000);
    for (int i = 0; i < n && start + i < len; i++) {
        float t = (float)i / RATE, env = fminf(1, i / (RATE * 0.004f)) * expf(-t * 5000 / ms);
        out[start + i] += vol * env * (sinf(2 * (float)M_PI * hz * t) + 0.18f * sinf(4 * (float)M_PI * hz * t));
    }
}

void sfx_play(SfxKind kind)
{
    if (!dev && !tried) {
        tried = 1;
        SDL_AudioSpec want = {0};
        want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (dev) SDL_PauseAudioDevice(dev, 0);
    }
    if (!dev) return;
    static const struct { float hz[3], ms, gap, vol; } S[] = {
        [SFX_MOVE] = {{1318, 0, 0}, 45, 0, 0.10f},
        [SFX_TAB] = {{880, 1175, 0}, 70, 40, 0.11f},
        [SFX_SELECT] = {{659, 988, 0}, 90, 45, 0.12f},
        [SFX_BACK] = {{988, 659, 0}, 90, 45, 0.11f},
        [SFX_OPEN] = {{523, 659, 784}, 140, 38, 0.10f},
        [SFX_CLOSE] = {{784, 659, 523}, 140, 38, 0.10f},
        [SFX_CONFIRM] = {{523, 784, 1047}, 220, 55, 0.12f},
        [SFX_TICK] = {{1568, 2093, 0}, 60, 30, 0.08f},
    };
    enum { LEN = RATE * 13 / 10 };
    static float mix[LEN];
    static Sint16 pcm[LEN];
    SDL_memset(mix, 0, sizeof mix);
    int len = 0;
    if (kind == SFX_FANFARE) {   /* the run's end: a rising arpeggio that lands on a held major chord */
        static const float RISE[] = {523, 659, 784, 1047}, CHORD[] = {523, 659, 784, 1047, 1319};
        for (int i = 0; i < 4; i++) note(mix, LEN, RATE * 70 * i / 1000, RISE[i], 160, 0.10f);
        for (int i = 0; i < 5; i++) note(mix, LEN, RATE * 300 / 1000, CHORD[i], 900, 0.055f);
        len = LEN;
    }
    for (int i = 0; kind != SFX_FANFARE && i < 3 && S[kind].hz[i] > 0; i++) {
        int at = (int)(RATE * S[kind].gap * i / 1000);
        note(mix, LEN, at, S[kind].hz[i], S[kind].ms, S[kind].vol);
        int end = at + (int)(RATE * S[kind].ms / 1000);
        if (end > len) len = end;
    }
    if (len > LEN) len = LEN;
    for (int i = 0; i < len; i++) pcm[i] = (Sint16)(fmaxf(-1, fminf(1, mix[i])) * 32767);
    SDL_ClearQueuedAudio(dev);   /* a new sound cuts the last one, so fast scrolling stays crisp */
    SDL_QueueAudio(dev, pcm, (Uint32)(len * sizeof pcm[0]));
}
