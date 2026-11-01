#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "pc_emu.h"

static uint32_t *g_pic; static int g_w, g_h, g_pitch;
static int16_t *g_audio; static size_t g_audio_frames, g_audio_cap;
static int g_pad;
static int g_rate = 32040;
static int g_open, g_rgb565;

static void log_cb(enum retro_log_level level, const char *fmt, ...) { (void)level; (void)fmt; }

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565) return false;
        g_rgb565 = f == RETRO_PIXEL_FORMAT_RGB565;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;
    default: return false;
    }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    size_t need = (size_t)w * h;
    static size_t cap;
    if (need > cap) { free(g_pic); g_pic = malloc(need * 4); cap = need; }
    for (unsigned y = 0; y < h; y++) {
        uint32_t *dst = g_pic + (size_t)y * w;
        if (!g_rgb565) { memcpy(dst, (const uint8_t *)data + y * pitch, (size_t)w * 4); continue; }
        const uint16_t *src = (const uint16_t *)((const uint8_t *)data + y * pitch);
        for (unsigned x = 0; x < w; x++) {
            uint32_t p = src[x], r = p >> 11 & 31, g = p >> 5 & 63, b = p & 31;
            dst[x] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
        }
    }
    g_w = (int)w; g_h = (int)h; g_pitch = (int)w;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    if (g_audio_frames + frames > g_audio_cap) {
        g_audio_cap = (g_audio_frames + frames) * 2;
        g_audio = realloc(g_audio, g_audio_cap * 4);
    }
    memcpy(g_audio + g_audio_frames * 2, data, frames * 4);
    g_audio_frames += frames;
    return frames;
}

static void audio_cb(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch_cb(s, 1); }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)g_pad;
    return id < 16 ? (g_pad >> id) & 1 : 0;
}

int emu_open(const uint8_t *rom, size_t len, const char *path)
{
    static int inited;
    if (g_open) emu_close();
    if (!inited) {
        retro_set_environment(env_cb);
        retro_init();
        retro_set_video_refresh(video_cb);
        retro_set_audio_sample(audio_cb);
        retro_set_audio_sample_batch(audio_batch_cb);
        retro_set_input_poll(poll_cb);
        retro_set_input_state(input_cb);
        inited = 1;
    }
    uint8_t *copy = malloc(len);
    memcpy(copy, rom, len);
    struct retro_game_info gi = {path && *path ? path : "smz3.sfc", copy, len, NULL};
    if (!retro_load_game(&gi)) { free(copy); return -1; }
    free(copy);   /* snes9x keeps its own copy */
    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    g_rate = (int)(av.timing.sample_rate + 0.5);
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    g_audio_frames = 0; g_w = g_h = 0; g_pad = 0;
    g_open = 1;
    return 0;
}

void emu_close(void) { if (g_open) { retro_unload_game(); g_open = 0; } }
int emu_sample_rate(void) { return g_rate; }
void emu_set_pad(int bits) { g_pad = bits; }
void emu_run_frame(void) { if (g_open) retro_run(); }
void emu_reset(void) { if (g_open) retro_reset(); }

const uint32_t *emu_picture(int *w, int *h, int *pitch_px) { *w = g_w; *h = g_h; *pitch_px = g_pitch; return g_pic; }

const int16_t *emu_take_audio(size_t *frames) { *frames = g_audio_frames; g_audio_frames = 0; return g_audio; }

const uint8_t *emu_wram(size_t *len)
{
    *len = g_open ? retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
    return g_open ? retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM) : NULL;
}

uint8_t *emu_sram(size_t *len)
{
    *len = g_open ? retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0;
    return g_open ? retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) : NULL;
}

size_t emu_state_size(void) { return g_open ? retro_serialize_size() : 0; }
int emu_state_save(void *buf, size_t len) { return g_open && retro_serialize(buf, len) ? 0 : -1; }
int emu_state_load(const void *buf, size_t len) { return g_open && retro_unserialize(buf, len) ? 0 : -1; }
