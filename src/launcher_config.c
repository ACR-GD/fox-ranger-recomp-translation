#include "launcher_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0755)
#endif

static void get_config_path(char *out, size_t max_len)
{
    /* 1. If foxranger.ini exists or current directory is writable, use it */
    FILE *f = fopen("foxranger.ini", "r");
    if (f) {
        fclose(f);
        snprintf(out, max_len, "foxranger.ini");
        return;
    }
    f = fopen("foxranger.ini", "a");
    if (f) {
        fclose(f);
        snprintf(out, max_len, "foxranger.ini");
        return;
    }

    /* 2. Standard user directory */
    const char *home = getenv("HOME");
#ifdef __APPLE__
    if (home) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/Library/Application Support/FoxRanger", home);
        mkdir_one(dir);
        snprintf(out, max_len, "%s/foxranger.ini", dir);
        return;
    }
#elif defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s\\FoxRanger", appdata);
        mkdir_one(dir);
        snprintf(out, max_len, "%s\\foxranger.ini", dir);
        return;
    }
#else
    if (home) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/.config/foxranger", home);
        mkdir_one(dir);
        snprintf(out, max_len, "%s/foxranger.ini", dir);
        return;
    }
#endif
    snprintf(out, max_len, "foxranger.ini");
}

void launcher_config_defaults(LauncherConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->game_dir, sizeof(cfg->game_dir), "fox-ranger");
    snprintf(cfg->exe_path, sizeof(cfg->exe_path), "work/FOX_EN_FINAL.EXE");
    cfg->scale = 3;
    cfg->aspect_4_3 = 1;
    cfg->fullscreen = 0;
    cfg->sfx_mode = 0;           /* hybrid mode default */
    cfg->autofire = 1;           /* autofire default on */
    cfg->infinite_shields = 0;
    cfg->language = 0;           /* 0 = English */
    cfg->music_type = 0;         /* 0 = Nuked-OPL3 */
    cfg->volume_master = 100;
    cfg->volume_music = 100;
    cfg->volume_sfx = 100;
    cfg->volume_voice = 100;
}

int launcher_config_load(LauncherConfig *cfg)
{
    launcher_config_defaults(cfg);

    char path[512];
    get_config_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* trim leading/trailing whitespace */
        while (*key == ' ' || *key == '\t') key++;
        char *end = key + strlen(key) - 1;
        while (end > key && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';

        while (*val == ' ' || *val == '\t') val++;
        end = val + strlen(val) - 1;
        while (end >= val && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';

        if (!strcmp(key, "game_dir")) snprintf(cfg->game_dir, sizeof(cfg->game_dir), "%s", val);
        else if (!strcmp(key, "exe_path")) snprintf(cfg->exe_path, sizeof(cfg->exe_path), "%s", val);
        else if (!strcmp(key, "scale")) cfg->scale = atoi(val);
        else if (!strcmp(key, "aspect_4_3")) cfg->aspect_4_3 = atoi(val);
        else if (!strcmp(key, "fullscreen")) cfg->fullscreen = atoi(val);
        else if (!strcmp(key, "sfx_mode")) cfg->sfx_mode = atoi(val);
        else if (!strcmp(key, "autofire")) cfg->autofire = atoi(val);
        else if (!strcmp(key, "infinite_shields")) cfg->infinite_shields = atoi(val);
        else if (!strcmp(key, "language")) cfg->language = atoi(val);
        else if (!strcmp(key, "music_type")) cfg->music_type = atoi(val);
        else if (!strcmp(key, "volume_master")) cfg->volume_master = atoi(val);
        else if (!strcmp(key, "volume_music")) cfg->volume_music = atoi(val);
        else if (!strcmp(key, "volume_sfx")) cfg->volume_sfx = atoi(val);
        else if (!strcmp(key, "volume_voice")) cfg->volume_voice = atoi(val);
    }
    fclose(f);
    return 0;
}

int launcher_config_save(const LauncherConfig *cfg)
{
    char path[512];
    get_config_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# Fox Ranger Recomp Configuration\n\n");
    fprintf(f, "[game]\n");
    fprintf(f, "game_dir = %s\n", cfg->game_dir);
    fprintf(f, "exe_path = %s\n", cfg->exe_path);
    fprintf(f, "language = %d\n\n", cfg->language);

    fprintf(f, "[video]\n");
    fprintf(f, "scale = %d\n", cfg->scale);
    fprintf(f, "aspect_4_3 = %d\n", cfg->aspect_4_3);
    fprintf(f, "fullscreen = %d\n\n", cfg->fullscreen);

    fprintf(f, "[audio]\n");
    fprintf(f, "sfx_mode = %d\n", cfg->sfx_mode);
    fprintf(f, "music_type = %d\n", cfg->music_type);
    fprintf(f, "volume_master = %d\n", cfg->volume_master);
    fprintf(f, "volume_music = %d\n", cfg->volume_music);
    fprintf(f, "volume_sfx = %d\n", cfg->volume_sfx);
    fprintf(f, "volume_voice = %d\n\n", cfg->volume_voice);

    fprintf(f, "[input]\n");
    fprintf(f, "autofire = %d\n\n", cfg->autofire);

    fprintf(f, "[cheats]\n");
    fprintf(f, "infinite_shields = %d\n", cfg->infinite_shields);

    fclose(f);
    return 0;
}
