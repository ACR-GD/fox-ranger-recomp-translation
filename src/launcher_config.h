#ifndef FOX_LAUNCHER_CONFIG_H
#define FOX_LAUNCHER_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char game_dir[512];
    char exe_path[512];
    int  scale;                  /* 1, 2, 3, 4, 5 */
    int  aspect_4_3;             /* 1 = 4:3 CRT, 0 = 16:10 */
    int  fullscreen;             /* 0 = windowed, 1 = fullscreen */
    int  sfx_mode;               /* 0 = hybrid, 1 = purist pc speaker, 2 = adlib */
    int  autofire;               /* 1 = on, 0 = off */
    int  infinite_shields;       /* 1 = on, 0 = off */
    int  language;               /* 0 = English, 1 = Korean */
    int  music_type;             /* 0 = OPL3, 1 = Arranged, 2 = MT-32, 3 = SC-55 */
    int  volume_master;          /* 0 - 100 */
    int  volume_music;           /* 0 - 100 */
    int  volume_sfx;             /* 0 - 100 */
    int  volume_voice;           /* 0 - 100 */
} LauncherConfig;

void launcher_config_defaults(LauncherConfig *cfg);
int  launcher_config_load(LauncherConfig *cfg);
int  launcher_config_save(const LauncherConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* FOX_LAUNCHER_CONFIG_H */
