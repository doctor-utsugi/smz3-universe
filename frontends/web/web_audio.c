/* One real SDL audio device (stereo 16-bit at the browser's rate) mixing the app's devices, see web_audio.h. Each app
 * device keeps its own format; an SDL_AudioStream converts it. Queue devices (the game, menu sounds) are fed by
 * SDL_QueueAudio; callback devices (menu music) are asked for more as the mixer needs it. */
#define WEB_AUDIO_IMPL
#include "web_audio.h"
/* the Makefile's -include already defined the renames here: this file calls the real SDL functions */
#undef SDL_OpenAudioDevice
#undef SDL_PauseAudioDevice
#undef SDL_QueueAudio
#undef SDL_ClearQueuedAudio
#undef SDL_GetQueuedAudioSize
#undef SDL_CloseAudioDevice
#undef SDL_LockAudioDevice
#undef SDL_UnlockAudioDevice
#include <string.h>

enum { MAXDEV = 8 };
typedef struct {
    int used, paused;
    SDL_AudioSpec spec;        /* what the app asked for */
    SDL_AudioStream *st;       /* its format -> the real device's */
} Dev;
static Dev devs[MAXDEV];
static SDL_AudioDeviceID real;
static SDL_AudioSpec out;
static Uint8 tmp[65536], gen[65536];

static Dev *dev(SDL_AudioDeviceID d) { return d >= 1 && d <= MAXDEV && devs[d - 1].used ? &devs[d - 1] : NULL; }

static void mix(void *user, Uint8 *stream, int len)
{
    (void)user;
    memset(stream, 0, (size_t)len);
    if (len > (int)sizeof tmp) len = sizeof tmp;
    Sint16 *o = (Sint16 *)stream;
    for (int i = 0; i < MAXDEV; i++) {
        Dev *v = &devs[i];
        if (!v->used || v->paused) continue;
        if (v->spec.callback) {   /* ask the app for more until there's enough converted sound */
            int chunk = v->spec.samples * v->spec.channels * (SDL_AUDIO_BITSIZE(v->spec.format) / 8);
            if (chunk <= 0 || chunk > (int)sizeof gen) chunk = sizeof gen / 2;
            for (int guard = 0; SDL_AudioStreamAvailable(v->st) < len && guard < 16; guard++) {
                v->spec.callback(v->spec.userdata, gen, chunk);
                SDL_AudioStreamPut(v->st, gen, chunk);
            }
        }
        int got = SDL_AudioStreamGet(v->st, tmp, len);
        Sint16 *s = (Sint16 *)tmp;
        for (int k = 0; k < got / 2; k++) {
            int x = o[k] + s[k];
            o[k] = (Sint16)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
    }
}

static int real_open(void)
{
    if (real) return 1;
    SDL_AudioSpec want = {0};
    want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = mix;
    real = SDL_OpenAudioDevice(NULL, 0, &want, &out, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!real) return 0;
    SDL_PauseAudioDevice(real, 0);
    return 1;
}

SDL_AudioDeviceID web_audio_open(const char *name, int capture, const SDL_AudioSpec *want, SDL_AudioSpec *have, int changes)
{
    (void)name; (void)changes;
    if (capture || !want || !real_open()) return 0;
    SDL_LockAudioDevice(real);
    for (int i = 0; i < MAXDEV; i++) {
        Dev *v = &devs[i];
        if (v->used) continue;
        v->st = SDL_NewAudioStream(want->format, want->channels, want->freq, out.format, out.channels, out.freq);
        if (!v->st) break;
        v->spec = *want;
        if (!v->spec.samples) v->spec.samples = 1024;
        v->paused = 1;   /* as SDL: a new device starts paused */
        v->used = 1;
        SDL_UnlockAudioDevice(real);
        if (have) *have = v->spec;
        return (SDL_AudioDeviceID)(i + 1);
    }
    SDL_UnlockAudioDevice(real);
    return 0;
}

void web_audio_pause(SDL_AudioDeviceID d, int on)
{
    SDL_LockAudioDevice(real);
    Dev *v = dev(d);
    if (v) v->paused = on;
    SDL_UnlockAudioDevice(real);
}

int web_audio_queue(SDL_AudioDeviceID d, const void *data, Uint32 len)
{
    SDL_LockAudioDevice(real);
    Dev *v = dev(d);
    int r = v && !v->spec.callback ? SDL_AudioStreamPut(v->st, data, (int)len) : -1;
    SDL_UnlockAudioDevice(real);
    return r;
}

void web_audio_clear(SDL_AudioDeviceID d)
{
    SDL_LockAudioDevice(real);
    Dev *v = dev(d);
    if (v) SDL_AudioStreamClear(v->st);
    SDL_UnlockAudioDevice(real);
}

/* in the app's own format, as SDL_GetQueuedAudioSize would say */
Uint32 web_audio_queued(SDL_AudioDeviceID d)
{
    SDL_LockAudioDevice(real);
    Dev *v = dev(d);
    Uint32 n = 0;
    if (v) {
        double frames = (double)SDL_AudioStreamAvailable(v->st) / (out.channels * 2) * v->spec.freq / out.freq;
        n = (Uint32)(frames * v->spec.channels * (SDL_AUDIO_BITSIZE(v->spec.format) / 8));
    }
    SDL_UnlockAudioDevice(real);
    return n;
}

void web_audio_close(SDL_AudioDeviceID d)
{
    SDL_LockAudioDevice(real);
    Dev *v = dev(d);
    if (v) { SDL_FreeAudioStream(v->st); memset(v, 0, sizeof *v); }
    SDL_UnlockAudioDevice(real);
}

void web_audio_lock(SDL_AudioDeviceID d) { (void)d; SDL_LockAudioDevice(real); }
void web_audio_unlock(SDL_AudioDeviceID d) { (void)d; SDL_UnlockAudioDevice(real); }
