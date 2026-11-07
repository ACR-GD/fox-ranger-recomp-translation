/* audio_opl.c - OPL2 output through Nuked-OPL3 (third_party/nuked-opl3, LGPL-2.1)
 * plus the voice mixer and modern arranged soundtrack support. See audio.h for the threading model. */
#include "audio.h"
#include "opl3.h"
#include "io.h"
#include "enhanced_music.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#define OPL_RING 8192u                    /* power of two */

static opl3_chip   g_chip;
static int         g_rate = AUDIO_RATE_DEFAULT;
static SDL_AudioDeviceID g_dev;

/* Volume controls (0 - 100) */
static SDL_atomic_t g_vol_master = { 100 };
static SDL_atomic_t g_vol_music  = { 100 };
static SDL_atomic_t g_vol_sfx    = { 100 };
static SDL_atomic_t g_vol_voice  = { 100 };

/* SPSC ring of OPL register writes: producer = game thread, consumer = mixer */
static uint32_t    g_ring[OPL_RING];
static SDL_atomic_t g_head, g_tail;      /* head: next write slot, tail: next read slot */

/* OPL timer status model (main thread only): enough for the standard AdLib
 * detection (write 04h=60h, 04h=80h, read status = 00h; start timer 1, wait,
 * read status & E0h = C0h). Timers expire immediately. */
static uint8_t     g_opl_status;

/* voice */
typedef struct { uint8_t *pcm; uint32_t n, rate; } Clip;
static void * volatile g_pending_ptr;     /* Clip* handed to the mixer (atomic exchange) */
static SDL_atomic_t g_voice_stop, g_voice_busy;
static Clip       *g_cur;                /* mixer-owned */
static uint64_t    g_pos_fp;             /* 32.32 fixed-point position in g_cur */

static AudioStats  g_stats;
static uint32_t    g_spk_phase;          /* PC speaker phase accumulator */

static void drain_opl(void)
{
    uint32_t tail = (uint32_t)SDL_AtomicGet(&g_tail), head = (uint32_t)SDL_AtomicGet(&g_head);
    while (tail != head) {
        uint32_t e = g_ring[tail & (OPL_RING - 1)];
        uint16_t reg = (uint16_t)(e >> 8);
        uint8_t val = (uint8_t)e;
        OPL3_WriteReg(&g_chip, reg, val);
        if ((reg & 0xFF) == 0xBD) {
            /* Rhythm mode: when any drum key is off, ensure slot eg_gen is in release
             * so an immediate retrigger is not dropped by Nuked-OPL3 envelope state */
            if (!(val & 0x10)) {
                g_chip.channel[6].slotz[0]->eg_gen = 3;
                g_chip.channel[6].slotz[1]->eg_gen = 3;
            }
            if (!(val & 0x08)) g_chip.channel[7].slotz[1]->eg_gen = 3;
            if (!(val & 0x04)) g_chip.channel[8].slotz[0]->eg_gen = 3;
            if (!(val & 0x02)) g_chip.channel[8].slotz[1]->eg_gen = 3;
            if (!(val & 0x01)) g_chip.channel[7].slotz[0]->eg_gen = 3;
        } else if ((reg & 0xF0) == 0xB0) {
            uint8_t ch = (uint8_t)(reg & 0x0F);
            if (ch < 9 && !(val & 0x20)) {
                g_chip.channel[ch].slotz[0]->eg_gen = 3;
                g_chip.channel[ch].slotz[1]->eg_gen = 3;
            }
        }
        tail++;
    }
    SDL_AtomicSet(&g_tail, (int)tail);
}

void audio_set_master_volume(int vol)
{
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    SDL_AtomicSet(&g_vol_master, vol);
}

void audio_set_music_volume(int vol)
{
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    SDL_AtomicSet(&g_vol_music, vol);
}

void audio_set_sfx_volume(int vol)
{
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    SDL_AtomicSet(&g_vol_sfx, vol);
}

void audio_set_voice_volume(int vol)
{
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    SDL_AtomicSet(&g_vol_voice, vol);
}

int audio_get_master_volume(void) { return SDL_AtomicGet(&g_vol_master); }
int audio_get_music_volume(void)  { return SDL_AtomicGet(&g_vol_music); }
int audio_get_sfx_volume(void)    { return SDL_AtomicGet(&g_vol_sfx); }
int audio_get_voice_volume(void)  { return SDL_AtomicGet(&g_vol_voice); }

void audio_render(int16_t *out, uint32_t frames)
{
    drain_opl();

    if (SDL_AtomicGet(&g_voice_stop)) {             /* stop kills the *current* clip only; */
        SDL_AtomicSet(&g_voice_stop, 0);            /* a clip queued after stop() survives */
        if (g_cur) { free(g_cur->pcm); free(g_cur); g_cur = NULL; }
        if (!g_pending_ptr) SDL_AtomicSet(&g_voice_busy, 0);
    }
    Clip *p = SDL_AtomicSetPtr((void **)&g_pending_ptr, NULL);
    if (p) {
        if (g_cur) { free(g_cur->pcm); free(g_cur); }
        g_cur = p; g_pos_fp = 0;
    }

    int vol_master = SDL_AtomicGet(&g_vol_master);
    int vol_music  = SDL_AtomicGet(&g_vol_music);
    int vol_sfx    = SDL_AtomicGet(&g_vol_sfx);
    int vol_voice  = SDL_AtomicGet(&g_vol_voice);

    int enh_active = enhanced_music_is_playing();

    #define AUDIO_CHUNK_MAX 512
    int16_t enh_buf[AUDIO_CHUNK_MAX * 2];

    uint32_t done = 0;
    uint64_t step = g_cur ? ((uint64_t)g_cur->rate << 32) / (uint32_t)g_rate : 0;

    while (done < frames) {
        uint32_t chunk = frames - done;
        if (chunk > AUDIO_CHUNK_MAX) chunk = AUDIO_CHUNK_MAX;

        if (enh_active) {
            enhanced_music_render(enh_buf, chunk, g_rate);
        }

        for (uint32_t i = 0; i < chunk; i++) {
            int32_t mus_l, mus_r;
            if (enh_active) {
                int16_t s[2];
                OPL3_GenerateResampled(&g_chip, s);
                mus_l = enh_buf[2 * i];
                mus_r = enh_buf[2 * i + 1];
            } else {
                int16_t s[2];
                OPL3_GenerateResampled(&g_chip, s);
                mus_l = s[0];
                mus_r = s[1];
            }

            if (vol_music != 100) {
                mus_l = (mus_l * vol_music) / 100;
                mus_r = (mus_r * vol_music) / 100;
            }

            int32_t v = 0;
            if (g_cur) {
                uint32_t k = (uint32_t)(g_pos_fp >> 32);
                if (k < g_cur->n) {
                    v = ((int32_t)g_cur->pcm[k] - 128) << 7;   /* u8 -> s16 at ~-6 dB */
                    if (vol_voice != 100) {
                        v = (v * vol_voice) / 100;
                    }
                    g_pos_fp += step;
                    g_stats.voice_frames_out++;
                } else {
                    free(g_cur->pcm); free(g_cur); g_cur = NULL;
                    if (!g_pending_ptr) SDL_AtomicSet(&g_voice_busy, 0);
                }
            }

            int32_t spk_v = 0;
            if ((g_io.port61 & 3) == 3 && g_io.pit_divisor[2] >= 2) {
                uint32_t freq = (uint32_t)(1193182u / g_io.pit_divisor[2]);
                if (freq >= 20 && freq <= 20000) {
                    uint32_t spk_step = (uint32_t)(((uint64_t)freq << 32) / (uint32_t)g_rate);
                    spk_v = (g_spk_phase & 0x80000000u) ? 2000 : -2000;
                    if (vol_sfx != 100) {
                        spk_v = (spk_v * vol_sfx) / 100;
                    }
                    g_spk_phase += spk_step;
                }
            } else {
                g_spk_phase = 0;
            }

            int32_t l = mus_l + v + spk_v;
            int32_t r = mus_r + v + spk_v;

            if (vol_master != 100) {
                l = (l * vol_master) / 100;
                r = (r * vol_master) / 100;
            }

            uint32_t out_idx = (done + i) * 2;
            out[out_idx]     = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
            out[out_idx + 1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
        }
        done += chunk;
    }
}

static void SDLCALL sdl_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    audio_render((int16_t *)stream, (uint32_t)len / 4);
}

int audio_init(int sample_rate, int open_device)
{
    g_rate = sample_rate > 0 ? sample_rate : AUDIO_RATE_DEFAULT;
    OPL3_Reset(&g_chip, (uint32_t)g_rate);
    SDL_AtomicSet(&g_head, 0); SDL_AtomicSet(&g_tail, 0);
    g_opl_status = 0;
    g_spk_phase = 0;
    memset(&g_stats, 0, sizeof g_stats);
    if (!open_device) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;
    SDL_AudioSpec want = { 0 }, have;
    want.freq = g_rate; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = 512; want.callback = sdl_cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) return -1;
    if (have.freq != g_rate) { g_rate = have.freq; OPL3_Reset(&g_chip, (uint32_t)g_rate); }
    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

void audio_shutdown(void)
{
    if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
}

int audio_sample_rate(void) { return g_rate; }

void audio_opl_write(uint16_t reg, uint8_t val)
{
    /* timer/IRQ register (04h) drives the status model */
    if ((reg & 0xFF) == 0x04) {
        if (val & 0x80) g_opl_status = 0;
        else {
            if (val & 0x01) g_opl_status |= 0xC0;   /* T1 expired + IRQ */
            if (val & 0x02) g_opl_status |= 0xA0;   /* T2 expired + IRQ */
        }
    }
    uint32_t head = (uint32_t)SDL_AtomicGet(&g_head), tail = (uint32_t)SDL_AtomicGet(&g_tail);
    if (head - tail >= OPL_RING) { g_stats.opl_dropped++; return; }
    g_ring[head & (OPL_RING - 1)] = ((uint32_t)reg << 8) | val;
    SDL_AtomicSet(&g_head, (int)(head + 1));
    g_stats.opl_writes++;
}

uint8_t audio_opl_status(void) { return g_opl_status; }

void audio_voice_play(const uint8_t *pcm, uint32_t n, uint32_t rate)
{
    Clip *c = malloc(sizeof *c);
    if (!c) return;
    c->pcm = malloc(n ? n : 1);
    if (!c->pcm) { free(c); return; }
    memcpy(c->pcm, pcm, n);
    c->n = n; c->rate = rate ? rate : 8000;
    SDL_AtomicSet(&g_voice_busy, 1);
    Clip *old = SDL_AtomicSetPtr((void **)&g_pending_ptr, c);
    if (old) { free(old->pcm); free(old); }
    g_stats.voice_clips++;
}

void audio_voice_stop(void)
{
    Clip *p = SDL_AtomicSetPtr((void **)&g_pending_ptr, NULL);   /* drop not-yet-started clip */
    if (p) { free(p->pcm); free(p); }
    SDL_AtomicSet(&g_voice_stop, 1);
}
int  audio_voice_busy(void)  { return SDL_AtomicGet(&g_voice_busy); }
AudioStats audio_stats(void) { return g_stats; }
