#include "ingest.h"
#include "enhanced_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

static int file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && !S_ISDIR(st.st_mode));
}

static int dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int check_asset_ci(const char *dir, const char *name, char *out_found, size_t out_len)
{
    char buf[512];
    /* Try uppercase */
    snprintf(buf, sizeof(buf), "%s%c%s", dir, PATH_SEP, name);
    if (file_exists(buf)) {
        if (out_found) snprintf(out_found, out_len, "%s", buf);
        return 1;
    }
    /* Try lowercase */
    char lower[64];
    size_t i = 0;
    for (; name[i] && i < sizeof(lower) - 1; i++) {
        lower[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] + 32) : name[i];
    }
    lower[i] = '\0';
    snprintf(buf, sizeof(buf), "%s%c%s", dir, PATH_SEP, lower);
    if (file_exists(buf)) {
        if (out_found) snprintf(out_found, out_len, "%s", buf);
        return 1;
    }
    return 0;
}

int ingest_scan(const char *folder_or_file, IngestStatus *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    if (!folder_or_file || !folder_or_file[0]) {
        snprintf(out->status_msg, sizeof(out->status_msg), "No folder or file selected.");
        return -1;
    }

    /* Check if target is directory or file */
    char folder[512];
    char candidate_file[512] = "";

    if (dir_exists(folder_or_file)) {
        snprintf(folder, sizeof(folder), "%s", folder_or_file);
    } else if (file_exists(folder_or_file)) {
        snprintf(candidate_file, sizeof(candidate_file), "%s", folder_or_file);
        /* Extract parent directory */
        const char *p1 = strrchr(folder_or_file, '/');
        const char *p2 = strrchr(folder_or_file, '\\');
        const char *p = p1 > p2 ? p1 : p2;
        if (p) {
            size_t len = (size_t)(p - folder_or_file);
            if (len >= sizeof(folder)) len = sizeof(folder) - 1;
            strncpy(folder, folder_or_file, len);
            folder[len] = '\0';
        } else {
            snprintf(folder, sizeof(folder), ".");
        }
    } else {
        snprintf(out->status_msg, sizeof(out->status_msg), "Path not found: %s", folder_or_file);
        return -1;
    }

    /* Strip trailing slashes */
    size_t flen = strlen(folder);
    while (flen > 1 && (folder[flen - 1] == '/' || folder[flen - 1] == '\\')) {
        folder[--flen] = '\0';
    }
    snprintf(out->folder, sizeof(out->folder), "%s", folder);

    /* Check data files */
    out->has_fox_dat     = check_asset_ci(folder, "FOX.DAT", NULL, 0);
    out->has_adlib_dat   = check_asset_ci(folder, "ADLIB.DAT", NULL, 0);
    out->has_voice_dat   = check_asset_ci(folder, "VOICE.DAT", NULL, 0);
    out->has_stage_dat   = check_asset_ci(folder, "STAGE.DAT", NULL, 0);
    out->has_vsprite_pat = check_asset_ci(folder, "VSPRITE.PAT", NULL, 0);

    /* Optional sound and enhanced audio hardware files */
    out->has_mt32_dat    = check_asset_ci(folder, "MT32.DAT", NULL, 0);
    out->has_sc55_dat    = check_asset_ci(folder, "SC55.DAT", NULL, 0);

    char enh_path[512];
    snprintf(enh_path, sizeof(enh_path), "%s%cenhanced_music", folder, PATH_SEP);
    out->has_enhanced_music = enhanced_music_has_tracks(enh_path) ||
                             enhanced_music_has_tracks("enhanced_music") ||
                             enhanced_music_has_tracks("fox-ranger/enhanced_music");

    /* Check executable */
    char found_exe[512] = "";
    if (candidate_file[0] && file_exists(candidate_file)) {
        snprintf(found_exe, sizeof(found_exe), "%s", candidate_file);
        out->has_executable = 1;
    } else if (check_asset_ci(folder, "FOX_EN_FINAL.EXE", found_exe, sizeof(found_exe))) {
        out->has_executable = 1;
        out->is_translated = 1;
    } else if (file_exists("work/FOX_EN_FINAL.EXE")) {
        snprintf(found_exe, sizeof(found_exe), "work/FOX_EN_FINAL.EXE");
        out->has_executable = 1;
        out->is_translated = 1;
    } else if (check_asset_ci(folder, "FOX_EN.EXE", found_exe, sizeof(found_exe))) {
        out->has_executable = 1;
        out->is_translated = 1;
    } else if (check_asset_ci(folder, "FOX_UNP2.EXE", found_exe, sizeof(found_exe))) {
        out->has_executable = 1;
    } else if (file_exists("work/FOX_UNP2.EXE")) {
        snprintf(found_exe, sizeof(found_exe), "work/FOX_UNP2.EXE");
        out->has_executable = 1;
    } else if (check_asset_ci(folder, "FOX.EXE", found_exe, sizeof(found_exe))) {
        out->has_executable = 1;
    }

    if (found_exe[0]) {
        snprintf(out->exe_path, sizeof(out->exe_path), "%s", found_exe);
    }

    /* Evaluate overall readiness */
    if (out->has_fox_dat && out->has_adlib_dat && out->has_voice_dat &&
        out->has_stage_dat && out->has_vsprite_pat && out->has_executable) {
        out->is_ready = 1;
        snprintf(out->status_msg, sizeof(out->status_msg), "Status: DOS Assets Verified & Ready");
        return 0;
    }

    snprintf(out->status_msg, sizeof(out->status_msg), "Status: Missing Required DOS Assets");
    return 1;
}
