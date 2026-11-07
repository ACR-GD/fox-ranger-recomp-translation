#include "enhanced_music.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static char        g_music_dir[512] = "enhanced_music";
static int         g_enabled = 1;
static SDL_mutex  *g_mutex = NULL;

/* Currently active track */
typedef enum {
    TRACK_NONE = 0,
    TRACK_VORBIS,
    TRACK_WAV
} TrackType;

static TrackType   g_track_type = TRACK_NONE;
static stb_vorbis *g_vorbis = NULL;
static int         g_vorbis_channels = 0;
static int         g_vorbis_rate = 0;

/* WAV state */
static uint8_t    *g_wav_buf = NULL;
static uint32_t    g_wav_len = 0;       /* in bytes */
static uint32_t    g_wav_pos = 0;
static int16_t    *g_wav_samples = NULL;
static uint32_t    g_wav_total_samples = 0; /* in stereo frames */
static uint32_t    g_wav_sample_idx = 0;

static int file_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

static const char *get_track_alias(int track_id)
{
    switch (track_id) {
    case 0:  case 17: return "intro";
    case 1:  case 12: return "title";
    case 2:  return "stage1";
    case 3:  return "boss";
    case 4:  return "stage2";
    case 5:  return "stage3";
    case 6:  return "stage4";
    case 7:  return "stage5";
    case 8:  return "stage6";
    case 9:  return "ending";
    case 10: return "gameover";
    case 13: case 14: case 15: return "stage_clear";
    default: return NULL;
    }
}

static int find_track_file(int track_id, const char *name, char *out_path, size_t max_len)
{
    const char *alias = name ? name : get_track_alias(track_id);
    const char *dirs[] = {
        g_music_dir,
        "enhanced_music",
        "fox-ranger/enhanced_music",
        NULL
    };

    for (int d = 0; dirs[d]; d++) {
        if (!dirs[d][0]) continue;

        /* 1. Try alias if available */
        if (alias) {
            snprintf(out_path, max_len, "%s/%s.ogg", dirs[d], alias);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/%s.wav", dirs[d], alias);
            if (file_readable(out_path)) return 1;
        }

        /* 2. Try track_XX.ogg / track_XX.wav */
        if (track_id >= 0) {
            snprintf(out_path, max_len, "%s/track_%02d.ogg", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/track%02d.ogg", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/track_%d.ogg", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/track%d.ogg", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/%02d.ogg", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/%d.ogg", dirs[d], track_id);
            if (file_readable(out_path)) return 1;

            snprintf(out_path, max_len, "%s/track_%02d.wav", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/track%02d.wav", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/track_%d.wav", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/track%d.wav", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/%02d.wav", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
            snprintf(out_path, max_len, "%s/%d.wav", dirs[d], track_id);
            if (file_readable(out_path)) return 1;
        }
    }
    return 0;
}

void enhanced_music_init(const char *music_dir)
{
    if (!g_mutex) {
        g_mutex = SDL_CreateMutex();
    }
    if (music_dir && music_dir[0]) {
        snprintf(g_music_dir, sizeof(g_music_dir), "%s", music_dir);
    }
}

void enhanced_music_set_enabled(int enabled)
{
    g_enabled = enabled;
    if (!enabled) {
        enhanced_music_stop();
    }
}

int enhanced_music_is_enabled(void)
{
    return g_enabled;
}

int enhanced_music_has_tracks(const char *music_dir)
{
    char tmp[512];
    /* Check for common track names */
    const char *test_names[] = {
        "title.ogg", "title.wav", "stage1.ogg", "stage1.wav",
        "intro.ogg", "intro.wav", "track_01.ogg", "track_01.wav",
        "track01.ogg", "track01.wav", "01.ogg", "01.wav", NULL
    };
    const char *base = music_dir && music_dir[0] ? music_dir : g_music_dir;
    for (int i = 0; test_names[i]; i++) {
        snprintf(tmp, sizeof(tmp), "%s/%s", base, test_names[i]);
        if (file_readable(tmp)) return 1;
    }
    return 0;
}

static void close_current_track_locked(void)
{
    if (g_vorbis) {
        stb_vorbis_close(g_vorbis);
        g_vorbis = NULL;
    }
    if (g_wav_samples) {
        free(g_wav_samples);
        g_wav_samples = NULL;
    }
    if (g_wav_buf) {
        SDL_FreeWAV(g_wav_buf);
        g_wav_buf = NULL;
    }
    g_wav_len = 0;
    g_wav_pos = 0;
    g_wav_total_samples = 0;
    g_wav_sample_idx = 0;
    g_track_type = TRACK_NONE;
}

int enhanced_music_play_track(int track_id)
{
    if (!g_enabled) return 0;

    char path[512] = "";
    if (!find_track_file(track_id, NULL, path, sizeof(path))) {
        /* No arranged file found -> stop arranged stream, fall back to OPL3 */
        enhanced_music_stop();
        return 0;
    }

    return enhanced_music_play_name(path);
}

int enhanced_music_play_name(const char *name)
{
    if (!g_enabled) return 0;
    if (!g_mutex) g_mutex = SDL_CreateMutex();

    char path[512];
    if (file_readable(name)) {
        snprintf(path, sizeof(path), "%s", name);
    } else if (!find_track_file(-1, name, path, sizeof(path))) {
        enhanced_music_stop();
        return 0;
    }

    SDL_LockMutex(g_mutex);
    close_current_track_locked();

    size_t len = strlen(path);
    if (len > 4 && (!strcasecmp(path + len - 4, ".ogg"))) {
        int err = 0;
        g_vorbis = stb_vorbis_open_filename(path, &err, NULL);
        if (g_vorbis) {
            stb_vorbis_info info = stb_vorbis_get_info(g_vorbis);
            g_vorbis_channels = info.channels;
            g_vorbis_rate = (int)info.sample_rate;
            g_track_type = TRACK_VORBIS;
            SDL_UnlockMutex(g_mutex);
            return 1;
        }
    } else if (len > 4 && (!strcasecmp(path + len - 4, ".wav"))) {
        SDL_AudioSpec spec;
        uint8_t *buf = NULL;
        uint32_t bytes = 0;
        if (SDL_LoadWAV(path, &spec, &buf, &bytes)) {
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, 44100) >= 0) {
                cvt.len = (int)bytes;
                cvt.buf = malloc((size_t)cvt.len * (size_t)cvt.len_mult);
                if (cvt.buf) {
                    memcpy(cvt.buf, buf, bytes);
                    SDL_ConvertAudio(&cvt);
                    g_wav_samples = (int16_t *)cvt.buf;
                    g_wav_total_samples = (uint32_t)cvt.len_cvt / 4;
                    g_wav_sample_idx = 0;
                    g_track_type = TRACK_WAV;
                }
            }
            SDL_FreeWAV(buf);
            if (g_track_type == TRACK_WAV) {
                SDL_UnlockMutex(g_mutex);
                return 1;
            }
        }
    }

    close_current_track_locked();
    SDL_UnlockMutex(g_mutex);
    return 0;
}

void enhanced_music_stop(void)
{
    if (!g_mutex) return;
    SDL_LockMutex(g_mutex);
    close_current_track_locked();
    SDL_UnlockMutex(g_mutex);
}

int enhanced_music_is_playing(void)
{
    return (g_track_type != TRACK_NONE);
}

void enhanced_music_render(int16_t *out, uint32_t frames, int target_rate)
{
    (void)target_rate;
    if (!g_mutex || g_track_type == TRACK_NONE) {
        memset(out, 0, frames * 4);
        return;
    }

    SDL_LockMutex(g_mutex);

    if (g_track_type == TRACK_VORBIS && g_vorbis) {
        uint32_t rendered = 0;
        while (rendered < frames) {
            uint32_t want = frames - rendered;
            int n = stb_vorbis_get_samples_short_interleaved(
                g_vorbis, 2, out + (rendered * 2), (int)(want * 2)
            );
            if (n <= 0) {
                /* Loop back to start */
                stb_vorbis_seek_start(g_vorbis);
                n = stb_vorbis_get_samples_short_interleaved(
                    g_vorbis, 2, out + (rendered * 2), (int)(want * 2)
                );
                if (n <= 0) break;
            }
            rendered += (uint32_t)n;
        }
        if (rendered < frames) {
            memset(out + (rendered * 2), 0, (frames - rendered) * 4);
        }
    } else if (g_track_type == TRACK_WAV && g_wav_samples && g_wav_total_samples > 0) {
        for (uint32_t i = 0; i < frames; i++) {
            if (g_wav_sample_idx >= g_wav_total_samples) {
                g_wav_sample_idx = 0; /* loop */
            }
            out[2 * i]     = g_wav_samples[2 * g_wav_sample_idx];
            out[2 * i + 1] = g_wav_samples[2 * g_wav_sample_idx + 1];
            g_wav_sample_idx++;
        }
    } else {
        memset(out, 0, frames * 4);
    }

    SDL_UnlockMutex(g_mutex);
}

void enhanced_music_shutdown(void)
{
    enhanced_music_stop();
    if (g_mutex) {
        SDL_DestroyMutex(g_mutex);
        g_mutex = NULL;
    }
}
