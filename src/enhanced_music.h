#ifndef FOX_ENHANCED_MUSIC_H
#define FOX_ENHANCED_MUSIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize enhanced music directory (e.g. "enhanced_music", or "<game>/enhanced_music") */
void enhanced_music_init(const char *music_dir);

/* Enable or disable arranged music streaming (1 = enabled, 0 = use OPL3) */
void enhanced_music_set_enabled(int enabled);
int  enhanced_music_is_enabled(void);

/* Check if enhanced music directory has any playable tracks */
int  enhanced_music_has_tracks(const char *music_dir);

/* Play music track by ID (called when game triggers music) */
int  enhanced_music_play_track(int track_id);

/* Play music track by name (e.g. "title", "stage1", "boss", etc.) */
int  enhanced_music_play_name(const char *name);

/* Stop current arranged music track */
void enhanced_music_stop(void);

/* Returns 1 if arranged music is currently playing */
int  enhanced_music_is_playing(void);

/* Render frames into stereo int16_t buffer (interleaved L, R) */
void enhanced_music_render(int16_t *out, uint32_t frames, int target_rate);

/* Shutdown */
void enhanced_music_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FOX_ENHANCED_MUSIC_H */
