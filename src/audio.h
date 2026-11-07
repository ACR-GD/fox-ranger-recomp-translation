/* audio.h - mixer for the two sound sources Fox Ranger drives:
 *   - OPL2 music: register writes from the AdLib driver (ports 388h/389h,
 *     or the SB mirror 228h/229h) feed Nuked-OPL3 (third_party/, LGPL-2.1)
 *   - digitised voice: PCM from VOICE.DAT handed over by the native CT-VOICE
 *     replacement (ctvoice.c)
 *
 * Threading: the game (main thread) only *queues* work: OPL writes go
 * through a lock-free single-producer/single-consumer ring, voice clips are
 * handed over by atomic pointer exchange. The audio thread (SDL callback),
 * or the caller of audio_render() when running headless, consumes them.
 */
#ifndef FOX_AUDIO_H
#define FOX_AUDIO_H
#include <stdint.h>

#define AUDIO_RATE_DEFAULT 44100

int     audio_init(int sample_rate, int open_device);   /* 0 = ok */
void    audio_shutdown(void);
int     audio_sample_rate(void);

/* OPL (main thread) */
void    audio_opl_write(uint16_t reg, uint8_t val);
uint8_t audio_opl_status(void);                 /* timer flags for AdLib detection */

/* voice channel (main thread). pcm is 8-bit unsigned, copied. */
void    audio_voice_play(const uint8_t *pcm, uint32_t n, uint32_t rate);
void    audio_voice_stop(void);
int     audio_voice_busy(void);                 /* clip queued or playing */

/* mix interleaved stereo S16 frames (audio thread, or caller when headless) */
void    audio_render(int16_t *out, uint32_t frames);

/* Volume controls (0 to 100) */
void    audio_set_master_volume(int vol);
void    audio_set_music_volume(int vol);
void    audio_set_sfx_volume(int vol);
void    audio_set_voice_volume(int vol);

int     audio_get_master_volume(void);
int     audio_get_music_volume(void);
int     audio_get_sfx_volume(void);
int     audio_get_voice_volume(void);

/* counters for tests / debugging */
typedef struct { uint32_t opl_writes, opl_dropped, voice_clips, voice_frames_out; } AudioStats;
AudioStats audio_stats(void);

#endif
