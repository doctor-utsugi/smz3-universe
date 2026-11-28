/* Web build: SDL's browser audio allows one output device at a time, and the app opens three (menu music, menu sounds,
 * the game). Every source file gets this header first (-include in the Makefile): the app's SDL audio device calls go to
 * web_audio.c, which mixes them into the one real device. */
#ifndef WEB_AUDIO_H
#define WEB_AUDIO_H
#include <SDL.h>
SDL_AudioDeviceID web_audio_open(const char *name, int capture, const SDL_AudioSpec *want, SDL_AudioSpec *have, int changes);
void web_audio_pause(SDL_AudioDeviceID d, int on);
int web_audio_queue(SDL_AudioDeviceID d, const void *data, Uint32 len);
void web_audio_clear(SDL_AudioDeviceID d);
Uint32 web_audio_queued(SDL_AudioDeviceID d);
void web_audio_close(SDL_AudioDeviceID d);
void web_audio_lock(SDL_AudioDeviceID d);
void web_audio_unlock(SDL_AudioDeviceID d);
#ifndef WEB_AUDIO_IMPL
#define SDL_OpenAudioDevice web_audio_open
#define SDL_PauseAudioDevice web_audio_pause
#define SDL_QueueAudio web_audio_queue
#define SDL_ClearQueuedAudio web_audio_clear
#define SDL_GetQueuedAudioSize web_audio_queued
#define SDL_CloseAudioDevice web_audio_close
#define SDL_LockAudioDevice web_audio_lock
#define SDL_UnlockAudioDevice web_audio_unlock
#endif
#endif
