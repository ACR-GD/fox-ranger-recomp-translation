#include "launcher.h"
#include "ingest.h"
#include "native_dialog.h"
#include "retro_font.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANV_W 640
#define CANV_H 480

#define STAR_COUNT 85

typedef struct {
    int x, y, speed;
    uint32_t color;
} Star;

typedef struct {
    int x, y, w, h;
    const char *label;
    int id;
} ButtonDef;

enum ButtonId {
    BTN_NONE = 0,
    BTN_BROWSE_FOLDER,
    BTN_BROWSE_FILE,
    BTN_SYNTH_OPL3,
    BTN_SYNTH_ARRANGED,
    BTN_SYNTH_MT32,
    BTN_SYNTH_SC55,
    BTN_VOL_MASTER_DOWN,
    BTN_VOL_MASTER_UP,
    BTN_VOL_MUSIC_DOWN,
    BTN_VOL_MUSIC_UP,
    BTN_VOL_SFX_DOWN,
    BTN_VOL_SFX_UP,
    BTN_VOL_VOICE_DOWN,
    BTN_VOL_VOICE_UP,
    BTN_SFX_HYBRID,
    BTN_SFX_PCSPK,
    BTN_SFX_ADLIB,
    BTN_SCALE_1,
    BTN_SCALE_2,
    BTN_SCALE_3,
    BTN_SCALE_4,
    BTN_SCALE_FULL,
    BTN_ASPECT_4_3,
    BTN_ASPECT_16_10,
    BTN_AUTOFIRE_ON,
    BTN_AUTOFIRE_OFF,
    BTN_SHIELD_NORMAL,
    BTN_SHIELD_INFINITE,
    BTN_START,
    BTN_QUIT
};

static void init_stars(Star *stars)
{
    uint32_t colors[3] = { 0xFF446699, 0xFF88AACC, 0xFFFFFFFF };
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i].x = rand() % CANV_W;
        stars[i].y = rand() % CANV_H;
        stars[i].speed = 1 + (rand() % 3);
        stars[i].color = colors[stars[i].speed - 1];
    }
}

static void update_stars(Star *stars)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i].y += stars[i].speed;
        if (stars[i].y >= CANV_H) {
            stars[i].y = 0;
            stars[i].x = rand() % CANV_W;
        }
    }
}

static void draw_stars(uint32_t *fb, const Star *stars)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        int idx = stars[i].y * CANV_W + stars[i].x;
        if (idx >= 0 && idx < CANV_W * CANV_H) {
            fb[idx] = stars[i].color;
        }
    }
}

static int is_inside(int mx, int my, int x, int y, int w, int h)
{
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

LauncherResult launcher_run(LauncherConfig *cfg)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "launcher: SDL_Init failed: %s\n", SDL_GetError());
        return LAUNCHER_ERROR;
    }

    int win_w = CANV_W * 2;
    int win_h = CANV_H * 2;
    SDL_Window *win = SDL_CreateWindow(
        "Fox Ranger (1992) - Mission Control & Recomp Launcher",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!win) {
        fprintf(stderr, "launcher: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return LAUNCHER_ERROR;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, 0);
    }
    if (!ren) {
        SDL_DestroyWindow(win);
        return LAUNCHER_ERROR;
    }

    SDL_RenderSetLogicalSize(ren, CANV_W, CANV_H);

    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CANV_W, CANV_H
    );
    if (!tex) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return LAUNCHER_ERROR;
    }

    uint32_t *fb = calloc(CANV_W * CANV_H, sizeof(uint32_t));
    if (!fb) {
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return LAUNCHER_ERROR;
    }

    Star stars[STAR_COUNT];
    init_stars(stars);

    IngestStatus ing;
    ingest_scan(cfg->game_dir, &ing);
    if (!ing.is_ready && cfg->exe_path[0]) {
        ingest_scan(cfg->exe_path, &ing);
    }

    int running = 1;
    LauncherResult result = LAUNCHER_QUIT;
    int mouse_x = 0, mouse_y = 0;
    int pulse = 0;

    while (running) {
        int hovered_btn = BTN_NONE;

        /* Dynamic labels */
        char master_str[16], music_str[16], sfx_str[16], voice_str[16];
        snprintf(master_str, sizeof(master_str), "%3d%%", cfg->volume_master);
        snprintf(music_str,  sizeof(music_str),  "%3d%%", cfg->volume_music);
        snprintf(sfx_str,    sizeof(sfx_str),    "%3d%%", cfg->volume_sfx);
        snprintf(voice_str,  sizeof(voice_str),  "%3d%%", cfg->volume_voice);

        const char *arr_label  = ing.has_enhanced_music ? "ARRANGED BGM" : "ARRANGED [--]";
        const char *mt32_label = ing.has_mt32_dat ? "ROLAND MT-32" : "MT-32 [--]";
        const char *sc55_label = ing.has_sc55_dat ? "ROLAND SC-55" : "SC-55 [--]";

        /* Define button layout */
        ButtonDef buttons[] = {
            /* Panel 1 (Data) */
            { 345, 134, 130, 20, "BROWSE FOLDER", BTN_BROWSE_FOLDER },
            { 485, 134, 125, 20, "SELECT FILE...", BTN_BROWSE_FILE },

            /* Panel 2 (Audio & Synth) */
            { 120, 186, 95,  20, "NUKED-OPL3",   BTN_SYNTH_OPL3 },
            { 220, 186, 125, 20, arr_label,      BTN_SYNTH_ARRANGED },
            { 350, 186, 115, 20, mt32_label,     BTN_SYNTH_MT32 },
            { 470, 186, 115, 20, sc55_label,     BTN_SYNTH_SC55 },

            { 120, 210, 190, 20, "HYBRID (OPL3 + SPK)", BTN_SFX_HYBRID },
            { 318, 210, 120, 20, "PC-SPEAKER",         BTN_SFX_PCSPK },
            { 445, 210, 100, 20, "ADLIB FM",           BTN_SFX_ADLIB },

            /* Volume controls */
            {  90, 234, 24, 18, "-", BTN_VOL_MASTER_DOWN },
            { 150, 234, 24, 18, "+", BTN_VOL_MASTER_UP },
            { 370, 234, 24, 18, "-", BTN_VOL_MUSIC_DOWN },
            { 430, 234, 24, 18, "+", BTN_VOL_MUSIC_UP },

            {  90, 258, 24, 18, "-", BTN_VOL_SFX_DOWN },
            { 150, 258, 24, 18, "+", BTN_VOL_SFX_UP },
            { 370, 258, 24, 18, "-", BTN_VOL_VOICE_DOWN },
            { 430, 258, 24, 18, "+", BTN_VOL_VOICE_UP },

            /* Panel 3 (Mission Parameters) */
            { 120, 314, 40, 20, "1x",   BTN_SCALE_1 },
            { 165, 314, 40, 20, "2x",   BTN_SCALE_2 },
            { 210, 314, 40, 20, "3x",   BTN_SCALE_3 },
            { 255, 314, 40, 20, "4x",   BTN_SCALE_4 },
            { 300, 314, 65, 20, "FULL", BTN_SCALE_FULL },

            { 440, 314, 90, 20, "4:3 CRT",   BTN_ASPECT_4_3 },
            { 535, 314, 75, 20, "16:10",     BTN_ASPECT_16_10 },

            { 120, 340, 225, 20, "RAPID AUTOFIRE (HOLD D/CTRL)", BTN_AUTOFIRE_ON },
            { 355, 340, 130, 20, "ORIGINAL FIRE",                BTN_AUTOFIRE_OFF },

            { 120, 366, 145, 20, "NORMAL SHIELDS",           BTN_SHIELD_NORMAL },
            { 275, 366, 215, 20, "INFINITE SHIELDS (CHEAT)", BTN_SHIELD_INFINITE },

            /* Footer */
            { 20, 408, 140, 36, "QUIT (ESC)", BTN_QUIT },
            { 175, 408, 445, 36, ing.is_ready ? ">>> START MISSION (ENTER) <<<" : "[ DOS ASSETS REQUIRED ]", BTN_START }
        };
        int num_buttons = sizeof(buttons) / sizeof(buttons[0]);

        for (int i = 0; i < num_buttons; i++) {
            if (is_inside(mouse_x, mouse_y, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h)) {
                hovered_btn = buttons[i].id;
                break;
            }
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
                result = LAUNCHER_QUIT;
            } else if (ev.type == SDL_MOUSEMOTION) {
                /* Map logical coordinates */
                mouse_x = ev.motion.x;
                mouse_y = ev.motion.y;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                if (hovered_btn == BTN_QUIT) {
                    running = 0;
                    result = LAUNCHER_QUIT;
                } else if (hovered_btn == BTN_BROWSE_FOLDER) {
                    char sel[512] = "";
                    if (native_dialog_browse_folder("Select Fox Ranger DOS Game Folder", sel, sizeof(sel)) == 0) {
                        snprintf(cfg->game_dir, sizeof(cfg->game_dir), "%s", sel);
                        ingest_scan(cfg->game_dir, &ing);
                        if (ing.has_executable && ing.exe_path[0]) {
                            snprintf(cfg->exe_path, sizeof(cfg->exe_path), "%s", ing.exe_path);
                        }
                        launcher_config_save(cfg);
                    }
                } else if (hovered_btn == BTN_BROWSE_FILE) {
                    char sel[512] = "";
                    if (native_dialog_browse_file("Select FOX.EXE or Game Executable", sel, sizeof(sel)) == 0) {
                        snprintf(cfg->exe_path, sizeof(cfg->exe_path), "%s", sel);
                        ingest_scan(cfg->exe_path, &ing);
                        if (ing.folder[0]) {
                            snprintf(cfg->game_dir, sizeof(cfg->game_dir), "%s", ing.folder);
                        }
                        launcher_config_save(cfg);
                    }
                } else if (hovered_btn == BTN_SYNTH_OPL3) { cfg->music_type = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SYNTH_ARRANGED) {
                    if (ing.has_enhanced_music) { cfg->music_type = 1; launcher_config_save(cfg); }
                } else if (hovered_btn == BTN_SYNTH_MT32) {
                    if (ing.has_mt32_dat) { cfg->music_type = 2; launcher_config_save(cfg); }
                } else if (hovered_btn == BTN_SYNTH_SC55) {
                    if (ing.has_sc55_dat) { cfg->music_type = 3; launcher_config_save(cfg); }
                } else if (hovered_btn == BTN_SFX_HYBRID) { cfg->sfx_mode = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SFX_PCSPK) { cfg->sfx_mode = 1; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SFX_ADLIB) { cfg->sfx_mode = 2; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_VOL_MASTER_DOWN) {
                    cfg->volume_master = cfg->volume_master >= 10 ? cfg->volume_master - 10 : 0;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_MASTER_UP) {
                    cfg->volume_master = cfg->volume_master <= 90 ? cfg->volume_master + 10 : 100;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_MUSIC_DOWN) {
                    cfg->volume_music = cfg->volume_music >= 10 ? cfg->volume_music - 10 : 0;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_MUSIC_UP) {
                    cfg->volume_music = cfg->volume_music <= 90 ? cfg->volume_music + 10 : 100;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_SFX_DOWN) {
                    cfg->volume_sfx = cfg->volume_sfx >= 10 ? cfg->volume_sfx - 10 : 0;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_SFX_UP) {
                    cfg->volume_sfx = cfg->volume_sfx <= 90 ? cfg->volume_sfx + 10 : 100;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_VOICE_DOWN) {
                    cfg->volume_voice = cfg->volume_voice >= 10 ? cfg->volume_voice - 10 : 0;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_VOL_VOICE_UP) {
                    cfg->volume_voice = cfg->volume_voice <= 90 ? cfg->volume_voice + 10 : 100;
                    launcher_config_save(cfg);
                } else if (hovered_btn == BTN_SCALE_1) { cfg->scale = 1; cfg->fullscreen = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SCALE_2) { cfg->scale = 2; cfg->fullscreen = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SCALE_3) { cfg->scale = 3; cfg->fullscreen = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SCALE_4) { cfg->scale = 4; cfg->fullscreen = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SCALE_FULL) { cfg->fullscreen = 1; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_ASPECT_4_3) { cfg->aspect_4_3 = 1; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_ASPECT_16_10) { cfg->aspect_4_3 = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_AUTOFIRE_ON) { cfg->autofire = 1; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_AUTOFIRE_OFF) { cfg->autofire = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SHIELD_NORMAL) { cfg->infinite_shields = 0; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_SHIELD_INFINITE) { cfg->infinite_shields = 1; launcher_config_save(cfg); }
                else if (hovered_btn == BTN_START) {
                    if (ing.is_ready) {
                        launcher_config_save(cfg);
                        running = 0;
                        result = LAUNCHER_START_GAME;
                    } else {
                        /* Prompt folder chooser if not ready */
                        char sel[512] = "";
                        if (native_dialog_browse_folder("Select Fox Ranger DOS Game Folder", sel, sizeof(sel)) == 0) {
                            snprintf(cfg->game_dir, sizeof(cfg->game_dir), "%s", sel);
                            ingest_scan(cfg->game_dir, &ing);
                            launcher_config_save(cfg);
                        }
                    }
                }
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                    result = LAUNCHER_QUIT;
                } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER || ev.key.keysym.sym == SDLK_SPACE) {
                    if (ing.is_ready) {
                        launcher_config_save(cfg);
                        running = 0;
                        result = LAUNCHER_START_GAME;
                    } else {
                        char sel[512] = "";
                        if (native_dialog_browse_folder("Select Fox Ranger DOS Game Folder", sel, sizeof(sel)) == 0) {
                            snprintf(cfg->game_dir, sizeof(cfg->game_dir), "%s", sel);
                            ingest_scan(cfg->game_dir, &ing);
                            launcher_config_save(cfg);
                        }
                    }
                } else if (ev.key.keysym.sym == SDLK_b) {
                    char sel[512] = "";
                    if (native_dialog_browse_folder("Select Fox Ranger DOS Game Folder", sel, sizeof(sel)) == 0) {
                        snprintf(cfg->game_dir, sizeof(cfg->game_dir), "%s", sel);
                        ingest_scan(cfg->game_dir, &ing);
                        launcher_config_save(cfg);
                    }
                }
            }
        }

        /* ----------------------- Render Frame ----------------------- */
        retro_fill_rect(fb, CANV_W, CANV_H, 0, 0, CANV_W, CANV_H, 0xFF060B18);
        update_stars(stars);
        draw_stars(fb, stars);

        /* Outer border */
        retro_draw_panel(fb, CANV_W, CANV_H, 8, 6, CANV_W - 16, CANV_H - 12, 0xFF0088CC, 0x00000000);

        /* Header Title */
        retro_draw_text_2x(fb, CANV_W, CANV_H, 40, 12, "FOX RANGER : MISSION CONTROL", 0xFFFFCC00, 0xFF332200);
        retro_draw_text(fb, CANV_W, CANV_H, 118, 44, "[ 1992 SOFT ACTION * RECOMP & TRANSLATION BY SECOND SAVE ]", 0xFF00EEFF, 0);

        /* ------------------- Panel 1: Game Data ------------------- */
        retro_draw_panel(fb, CANV_W, CANV_H, 20, 62, 600, 98, 0xFF0077AA, 0xEE081428);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 66, "1992 DOS GAME ASSETS CONFIGURATION", 0xFF00FFFF, 0);

        /* Path display */
        char path_disp[80];
        snprintf(path_disp, sizeof(path_disp), "Folder: %s", cfg->game_dir[0] ? cfg->game_dir : "[ NOT CONFIGURED - ACTION REQUIRED ]");
        retro_draw_text(fb, CANV_W, CANV_H, 30, 82, path_disp, cfg->game_dir[0] ? 0xFFFFFFFF : 0xFFFFAA33, 0);

        /* Asset badges: Row 1 */
        int badge_y1 = 98;
        retro_draw_text(fb, CANV_W, CANV_H, 30,  badge_y1, "FOX.DAT:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 95,  badge_y1, ing.has_fox_dat ? "[OK]" : "[--]", ing.has_fox_dat ? 0xFF00FF66 : 0xFFFF4444, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 135, badge_y1, "ADLIB:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 185, badge_y1, ing.has_adlib_dat ? "[OK]" : "[--]", ing.has_adlib_dat ? 0xFF00FF66 : 0xFFFF4444, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 225, badge_y1, "VOICE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 275, badge_y1, ing.has_voice_dat ? "[OK]" : "[--]", ing.has_voice_dat ? 0xFF00FF66 : 0xFFFF4444, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 315, badge_y1, "STAGE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 365, badge_y1, ing.has_stage_dat ? "[OK]" : "[--]", ing.has_stage_dat ? 0xFF00FF66 : 0xFFFF4444, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 405, badge_y1, "SPRITE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 465, badge_y1, ing.has_vsprite_pat ? "[OK]" : "[--]", ing.has_vsprite_pat ? 0xFF00FF66 : 0xFFFF4444, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 505, badge_y1, "EXE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 545, badge_y1, ing.has_executable ? "[OK]" : "[--]", ing.has_executable ? 0xFF00FF66 : 0xFFFF4444, 0);

        /* Hardware badges: Row 2 */
        int badge_y2 = 116;
        retro_draw_text(fb, CANV_W, CANV_H, 30,  badge_y2, "MT-32:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 80,  badge_y2, ing.has_mt32_dat ? "[OK]" : "[--]", ing.has_mt32_dat ? 0xFF00FF66 : 0xFF667788, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 135, badge_y2, "SC-55:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 185, badge_y2, ing.has_sc55_dat ? "[OK]" : "[--]", ing.has_sc55_dat ? 0xFF00FF66 : 0xFF667788, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 240, badge_y2, "ARRANGED CD TRACKS:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 405, badge_y2, ing.has_enhanced_music ? "[AVAILABLE]" : "[NO FILES]", ing.has_enhanced_music ? 0xFF00FF66 : 0xFF667788, 0);

        /* Status line and browse prompt */
        retro_draw_text(fb, CANV_W, CANV_H, 30, 136, ing.status_msg, ing.is_ready ? 0xFF55FF55 : 0xFFFFCC00, 0);

        /* ------------------- Panel 2: Audio & Soundtrack ------------------- */
        retro_draw_panel(fb, CANV_W, CANV_H, 20, 166, 600, 122, 0xFF0077AA, 0xEE081428);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 170, "AUDIO SYNTHESIS & SOUNDTRACK MIXER", 0xFF00FFFF, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 30, 188, "Music Synth:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 212, "SFX Engine:", 0xFFCCCCCC, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 30,  236, "Master:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 310, 236, "Music:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30,  260, "SFX:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 310, 260, "Voice:", 0xFFCCCCCC, 0);

        /* Volume Percentage Labels */
        retro_draw_text(fb, CANV_W, CANV_H, 116, 235, master_str, 0xFFFFFFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 396, 235, music_str,  0xFFFFFFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 116, 259, sfx_str,    0xFFFFFFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 396, 259, voice_str,  0xFFFFFFFF, 0);

        /* Volume Level Bars */
        retro_draw_panel(fb, CANV_W, CANV_H, 180, 238, 85, 10, 0xFF006699, 0xFF051020);
        int w_m = (81 * cfg->volume_master) / 100;
        if (w_m > 0) retro_fill_rect(fb, CANV_W, CANV_H, 182, 240, w_m, 6, 0xFF00FFCC);

        retro_draw_panel(fb, CANV_W, CANV_H, 460, 238, 85, 10, 0xFF006699, 0xFF051020);
        int w_mus = (81 * cfg->volume_music) / 100;
        if (w_mus > 0) retro_fill_rect(fb, CANV_W, CANV_H, 462, 240, w_mus, 6, 0xFF00FFCC);

        retro_draw_panel(fb, CANV_W, CANV_H, 180, 262, 85, 10, 0xFF006699, 0xFF051020);
        int w_s = (81 * cfg->volume_sfx) / 100;
        if (w_s > 0) retro_fill_rect(fb, CANV_W, CANV_H, 182, 264, w_s, 6, 0xFF00FFCC);

        retro_draw_panel(fb, CANV_W, CANV_H, 460, 262, 85, 10, 0xFF006699, 0xFF051020);
        int w_v = (81 * cfg->volume_voice) / 100;
        if (w_v > 0) retro_fill_rect(fb, CANV_W, CANV_H, 462, 264, w_v, 6, 0xFF00FFCC);

        /* ------------------- Panel 3: Settings ------------------- */
        retro_draw_panel(fb, CANV_W, CANV_H, 20, 294, 600, 102, 0xFF0077AA, 0xEE081428);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 298, "MISSION & SYSTEM PARAMETERS", 0xFF00FFFF, 0);

        retro_draw_text(fb, CANV_W, CANV_H, 30, 316, "Scale:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 380, 316, "Aspect:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 342, "Weapon:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 368, "Shields:", 0xFFCCCCCC, 0);

        /* Render Buttons */
        for (int i = 0; i < num_buttons; i++) {
            ButtonDef *b = &buttons[i];
            int is_hov = (hovered_btn == b->id);
            int is_sel = 0;

            if (b->id == BTN_SYNTH_OPL3) is_sel = (cfg->music_type == 0);
            else if (b->id == BTN_SYNTH_ARRANGED) is_sel = (cfg->music_type == 1);
            else if (b->id == BTN_SYNTH_MT32) is_sel = (cfg->music_type == 2);
            else if (b->id == BTN_SYNTH_SC55) is_sel = (cfg->music_type == 3);
            else if (b->id == BTN_SFX_HYBRID) is_sel = (cfg->sfx_mode == 0);
            else if (b->id == BTN_SFX_PCSPK) is_sel = (cfg->sfx_mode == 1);
            else if (b->id == BTN_SFX_ADLIB) is_sel = (cfg->sfx_mode == 2);
            else if (b->id == BTN_SCALE_1) is_sel = (!cfg->fullscreen && cfg->scale == 1);
            else if (b->id == BTN_SCALE_2) is_sel = (!cfg->fullscreen && cfg->scale == 2);
            else if (b->id == BTN_SCALE_3) is_sel = (!cfg->fullscreen && cfg->scale == 3);
            else if (b->id == BTN_SCALE_4) is_sel = (!cfg->fullscreen && cfg->scale == 4);
            else if (b->id == BTN_SCALE_FULL) is_sel = cfg->fullscreen;
            else if (b->id == BTN_ASPECT_4_3) is_sel = cfg->aspect_4_3;
            else if (b->id == BTN_ASPECT_16_10) is_sel = !cfg->aspect_4_3;
            else if (b->id == BTN_AUTOFIRE_ON) is_sel = cfg->autofire;
            else if (b->id == BTN_AUTOFIRE_OFF) is_sel = !cfg->autofire;
            else if (b->id == BTN_SHIELD_NORMAL) is_sel = !cfg->infinite_shields;
            else if (b->id == BTN_SHIELD_INFINITE) is_sel = cfg->infinite_shields;
            else if (b->id == BTN_START && ing.is_ready) {
                pulse = (pulse + 1) % 40;
                is_sel = (pulse < 20);
            }

            retro_draw_button(fb, CANV_W, CANV_H, b->x, b->y, b->w, b->h, b->label, is_hov, is_sel);
        }

        /* Footer line */
        retro_draw_text(fb, CANV_W, CANV_H, 110, 456, "Use Mouse or Enter/Space to select * B: Browse * Esc: Quit", 0xFF6688AA, 0);

        /* Push to display */
        SDL_UpdateTexture(tex, NULL, fb, CANV_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        SDL_Delay(16);
    }

    free(fb);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);

    return result;
}

static void draw_demo_cursor(uint32_t *fb, int mx, int my, int clicking)
{
    static const char *arrow[14] = {
        "X.............",
        "XX............",
        "XOX...........",
        "XOOX..........",
        "XOOOX.........",
        "XOOOOX........",
        "XOOOOOX.......",
        "XOOOOOOX......",
        "XOOOOOOOX.....",
        "XOOOOOOOOX....",
        "XOOOOOXXXXX...",
        "XOOXOOX.......",
        "XOX.XOOX......",
        "XX...XX......."
    };
    if (clicking) {
        /* Draw click highlight ring */
        retro_draw_panel(fb, CANV_W, CANV_H, mx - 8, my - 8, 17, 17, 0xFFFFFF00, 0);
    }
    for (int r = 0; r < 14; r++) {
        for (int c = 0; c < 14; c++) {
            char ch = arrow[r][c];
            if (ch == '.') continue;
            int px = mx + c, py = my + r;
            if (px >= 0 && px < CANV_W && py >= 0 && py < CANV_H) {
                fb[py * CANV_W + px] = (ch == 'X') ? 0xFF002244 : (clicking ? 0xFFFFEE44 : 0xFF00FFFF);
            }
        }
    }
}

int launcher_record_demo(const char *frames_dir, const char *game_dir)
{
    uint32_t *fb = calloc(CANV_W * CANV_H, sizeof(uint32_t));
    if (!fb) return -1;

    srand(1992);
    Star stars[STAR_COUNT];
    init_stars(stars);

    LauncherConfig cfg;
    launcher_config_defaults(&cfg);
    cfg.game_dir[0] = '\0';
    cfg.exe_path[0] = '\0';
    cfg.scale = 3;
    cfg.aspect_4_3 = 1;
    cfg.autofire = 1;
    cfg.infinite_shields = 0;
    cfg.music_type = 0;
    cfg.sfx_mode = 0;
    cfg.volume_master = 90;
    cfg.volume_music = 80;
    cfg.volume_sfx = 90;
    cfg.volume_voice = 90;

    IngestStatus ing;
    memset(&ing, 0, sizeof(ing));
    snprintf(ing.status_msg, sizeof(ing.status_msg), "ACTION REQUIRED: Select 1992 Fox Ranger DOS Folder");

    typedef struct { int frame; int mx, my; int click_btn; } Waypoint;
    static const Waypoint wp[] = {
        {   0, 320, 260, BTN_NONE },
        {  36, 410, 144, BTN_BROWSE_FOLDER },     /* Click Browse Folder -> Ingest DOS assets! */
        {  68, 410, 144, BTN_NONE },              /* Hold to admire [OK] asset badges */
        {  92, 165, 196, BTN_SYNTH_OPL3 },        /* Hover Nuked-OPL3 */
        { 112, 215, 220, BTN_SFX_HYBRID },        /* Hover Hybrid SFX */
        { 132, 162, 243, BTN_VOL_MASTER_UP },     /* Click Master + -> 100% */
        { 152, 442, 243, BTN_VOL_MUSIC_UP },      /* Click Music + -> 90% */
        { 164, 442, 243, BTN_VOL_MUSIC_UP },      /* Click Music + -> 100% */
        { 182, 162, 267, BTN_VOL_SFX_UP },        /* Click SFX + -> 100% */
        { 200, 442, 267, BTN_VOL_VOICE_UP },      /* Click Voice + -> 100% */
        { 222, 275, 324, BTN_SCALE_4 },           /* Click 4x Scale */
        { 242, 485, 324, BTN_ASPECT_4_3 },        /* Hover 4:3 CRT */
        { 262, 230, 350, BTN_AUTOFIRE_ON },       /* Hover Rapid Autofire */
        { 282, 380, 376, BTN_SHIELD_INFINITE },   /* Toggle Infinite Shields */
        { 305, 190, 376, BTN_SHIELD_NORMAL },     /* Back to Normal Shields */
        { 335, 395, 426, BTN_START },             /* Move to >>> START MISSION <<< & Click! */
        { 360, 395, 426, BTN_NONE }
    };
    int n_wp = sizeof(wp) / sizeof(wp[0]);
    int total_frames = 360; /* 12.0s at 30 FPS */
    int pulse = 0;

    for (int f = 0; f < total_frames; f++) {
        int mx = wp[n_wp - 1].mx, my = wp[n_wp - 1].my;
        int clicking = 0;

        for (int k = 0; k < n_wp - 1; k++) {
            if (f >= wp[k].frame && f <= wp[k + 1].frame) {
                int span = wp[k + 1].frame - wp[k].frame;
                double t = span > 0 ? (double)(f - wp[k].frame) / (double)span : 1.0;
                /* Smoothstep interpolation */
                double s = t * t * (3.0 - 2.0 * t);
                mx = (int)(wp[k].mx + (wp[k + 1].mx - wp[k].mx) * s);
                my = (int)(wp[k].my + (wp[k + 1].my - wp[k].my) * s);
                if (f == wp[k + 1].frame && wp[k + 1].click_btn != BTN_NONE) {
                    int b = wp[k + 1].click_btn;
                    if (b == BTN_BROWSE_FOLDER) {
                        snprintf(cfg.game_dir, sizeof(cfg.game_dir), "%s", game_dir ? game_dir : "fox-ranger");
                        ingest_scan(cfg.game_dir, &ing);
                    } else if (b == BTN_VOL_MASTER_UP && cfg.volume_master <= 90) cfg.volume_master += 10;
                    else if (b == BTN_VOL_MUSIC_UP && cfg.volume_music <= 90) cfg.volume_music += 10;
                    else if (b == BTN_VOL_SFX_UP && cfg.volume_sfx <= 90) cfg.volume_sfx += 10;
                    else if (b == BTN_VOL_VOICE_UP && cfg.volume_voice <= 90) cfg.volume_voice += 10;
                    else if (b == BTN_SCALE_4) cfg.scale = 4;
                    else if (b == BTN_SHIELD_INFINITE) cfg.infinite_shields = 1;
                    else if (b == BTN_SHIELD_NORMAL) cfg.infinite_shields = 0;
                }
                if (wp[k + 1].click_btn != BTN_NONE && f >= wp[k + 1].frame && f <= wp[k + 1].frame + 5) {
                    clicking = 1;
                }
                break;
            }
        }

        char master_str[16], music_str[16], sfx_str[16], voice_str[16];
        snprintf(master_str, sizeof(master_str), "%3d%%", cfg.volume_master);
        snprintf(music_str,  sizeof(music_str),  "%3d%%", cfg.volume_music);
        snprintf(sfx_str,    sizeof(sfx_str),    "%3d%%", cfg.volume_sfx);
        snprintf(voice_str,  sizeof(voice_str),  "%3d%%", cfg.volume_voice);

        const char *arr_label  = ing.has_enhanced_music ? "ARRANGED BGM" : "ARRANGED [--]";
        const char *mt32_label = ing.has_mt32_dat ? "ROLAND MT-32" : "MT-32 [--]";
        const char *sc55_label = ing.has_sc55_dat ? "ROLAND SC-55" : "SC-55 [--]";

        ButtonDef buttons[] = {
            { 345, 134, 130, 20, "BROWSE FOLDER", BTN_BROWSE_FOLDER },
            { 485, 134, 125, 20, "SELECT FILE...", BTN_BROWSE_FILE },
            { 120, 186, 95,  20, "NUKED-OPL3",   BTN_SYNTH_OPL3 },
            { 220, 186, 125, 20, arr_label,      BTN_SYNTH_ARRANGED },
            { 350, 186, 115, 20, mt32_label,     BTN_SYNTH_MT32 },
            { 470, 186, 115, 20, sc55_label,     BTN_SYNTH_SC55 },
            { 120, 210, 190, 20, "HYBRID (OPL3 + SPK)", BTN_SFX_HYBRID },
            { 318, 210, 120, 20, "PC-SPEAKER",         BTN_SFX_PCSPK },
            { 445, 210, 100, 20, "ADLIB FM",           BTN_SFX_ADLIB },
            {  90, 234, 24, 18, "-", BTN_VOL_MASTER_DOWN },
            { 150, 234, 24, 18, "+", BTN_VOL_MASTER_UP },
            { 370, 234, 24, 18, "-", BTN_VOL_MUSIC_DOWN },
            { 430, 234, 24, 18, "+", BTN_VOL_MUSIC_UP },
            {  90, 258, 24, 18, "-", BTN_VOL_SFX_DOWN },
            { 150, 258, 24, 18, "+", BTN_VOL_SFX_UP },
            { 370, 258, 24, 18, "-", BTN_VOL_VOICE_DOWN },
            { 430, 258, 24, 18, "+", BTN_VOL_VOICE_UP },
            { 120, 314, 40, 20, "1x",   BTN_SCALE_1 },
            { 165, 314, 40, 20, "2x",   BTN_SCALE_2 },
            { 210, 314, 40, 20, "3x",   BTN_SCALE_3 },
            { 255, 314, 40, 20, "4x",   BTN_SCALE_4 },
            { 300, 314, 65, 20, "FULL", BTN_SCALE_FULL },
            { 440, 314, 90, 20, "4:3 CRT",   BTN_ASPECT_4_3 },
            { 535, 314, 75, 20, "16:10",     BTN_ASPECT_16_10 },
            { 120, 340, 225, 20, "RAPID AUTOFIRE (HOLD D/CTRL)", BTN_AUTOFIRE_ON },
            { 355, 340, 130, 20, "ORIGINAL FIRE",                BTN_AUTOFIRE_OFF },
            { 120, 366, 145, 20, "NORMAL SHIELDS",           BTN_SHIELD_NORMAL },
            { 275, 366, 215, 20, "INFINITE SHIELDS (CHEAT)", BTN_SHIELD_INFINITE },
            { 20, 408, 140, 36, "QUIT (ESC)", BTN_QUIT },
            { 175, 408, 445, 36, ing.is_ready ? ">>> START MISSION (ENTER) <<<" : "[ DOS ASSETS REQUIRED ]", BTN_START }
        };
        int num_buttons = sizeof(buttons) / sizeof(buttons[0]);
        int hovered_btn = BTN_NONE;
        for (int i = 0; i < num_buttons; i++) {
            if (is_inside(mx, my, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h)) {
                hovered_btn = buttons[i].id;
                break;
            }
        }

        retro_fill_rect(fb, CANV_W, CANV_H, 0, 0, CANV_W, CANV_H, 0xFF060B18);
        update_stars(stars);
        draw_stars(fb, stars);
        retro_draw_panel(fb, CANV_W, CANV_H, 8, 6, CANV_W - 16, CANV_H - 12, 0xFF0088CC, 0x00000000);
        retro_draw_text_2x(fb, CANV_W, CANV_H, 40, 12, "FOX RANGER : MISSION CONTROL", 0xFFFFCC00, 0xFF332200);
        retro_draw_text(fb, CANV_W, CANV_H, 118, 44, "[ 1992 SOFT ACTION * RECOMP & TRANSLATION BY SECOND SAVE ]", 0xFF00EEFF, 0);

        retro_draw_panel(fb, CANV_W, CANV_H, 20, 62, 600, 98, 0xFF0077AA, 0xEE081428);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 66, "1992 DOS GAME ASSETS CONFIGURATION", 0xFF00FFFF, 0);
        char path_disp[80];
        snprintf(path_disp, sizeof(path_disp), "Folder: %s", cfg.game_dir[0] ? cfg.game_dir : "[ NOT CONFIGURED - ACTION REQUIRED ]");
        retro_draw_text(fb, CANV_W, CANV_H, 30, 82, path_disp, cfg.game_dir[0] ? 0xFFFFFFFF : 0xFFFFAA33, 0);

        int badge_y1 = 98;
        retro_draw_text(fb, CANV_W, CANV_H, 30,  badge_y1, "FOX.DAT:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 95,  badge_y1, ing.has_fox_dat ? "[OK]" : "[--]", ing.has_fox_dat ? 0xFF00FF66 : 0xFFFF4444, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 135, badge_y1, "ADLIB:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 185, badge_y1, ing.has_adlib_dat ? "[OK]" : "[--]", ing.has_adlib_dat ? 0xFF00FF66 : 0xFFFF4444, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 225, badge_y1, "VOICE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 275, badge_y1, ing.has_voice_dat ? "[OK]" : "[--]", ing.has_voice_dat ? 0xFF00FF66 : 0xFFFF4444, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 315, badge_y1, "STAGE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 365, badge_y1, ing.has_stage_dat ? "[OK]" : "[--]", ing.has_stage_dat ? 0xFF00FF66 : 0xFFFF4444, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 405, badge_y1, "SPRITE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 465, badge_y1, ing.has_vsprite_pat ? "[OK]" : "[--]", ing.has_vsprite_pat ? 0xFF00FF66 : 0xFFFF4444, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 505, badge_y1, "EXE:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 545, badge_y1, ing.has_executable ? "[OK]" : "[--]", ing.has_executable ? 0xFF00FF66 : 0xFFFF4444, 0);

        int badge_y2 = 116;
        retro_draw_text(fb, CANV_W, CANV_H, 30,  badge_y2, "MT-32:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 80,  badge_y2, ing.has_mt32_dat ? "[OK]" : "[--]", ing.has_mt32_dat ? 0xFF00FF66 : 0xFF667788, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 135, badge_y2, "SC-55:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 185, badge_y2, ing.has_sc55_dat ? "[OK]" : "[--]", ing.has_sc55_dat ? 0xFF00FF66 : 0xFF667788, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 240, badge_y2, "ARRANGED CD TRACKS:", 0xFF88AACC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 405, badge_y2, ing.has_enhanced_music ? "[AVAILABLE]" : "[NO FILES]", ing.has_enhanced_music ? 0xFF00FF66 : 0xFF667788, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 136, ing.status_msg, ing.is_ready ? 0xFF55FF55 : 0xFFFFCC00, 0);

        retro_draw_panel(fb, CANV_W, CANV_H, 20, 166, 600, 122, 0xFF0077AA, 0xEE081428);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 170, "AUDIO SYNTHESIS & SOUNDTRACK MIXER", 0xFF00FFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 188, "Music Synth:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 212, "SFX Engine:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30,  236, "Master:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 310, 236, "Music:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30,  260, "SFX:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 310, 260, "Voice:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 116, 235, master_str, 0xFFFFFFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 396, 235, music_str,  0xFFFFFFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 116, 259, sfx_str,    0xFFFFFFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 396, 259, voice_str,  0xFFFFFFFF, 0);

        retro_draw_panel(fb, CANV_W, CANV_H, 180, 238, 85, 10, 0xFF006699, 0xFF051020);
        int w_m = (81 * cfg.volume_master) / 100;
        if (w_m > 0) retro_fill_rect(fb, CANV_W, CANV_H, 182, 240, w_m, 6, 0xFF00FFCC);
        retro_draw_panel(fb, CANV_W, CANV_H, 460, 238, 85, 10, 0xFF006699, 0xFF051020);
        int w_mus = (81 * cfg.volume_music) / 100;
        if (w_mus > 0) retro_fill_rect(fb, CANV_W, CANV_H, 462, 240, w_mus, 6, 0xFF00FFCC);
        retro_draw_panel(fb, CANV_W, CANV_H, 180, 262, 85, 10, 0xFF006699, 0xFF051020);
        int w_s = (81 * cfg.volume_sfx) / 100;
        if (w_s > 0) retro_fill_rect(fb, CANV_W, CANV_H, 182, 264, w_s, 6, 0xFF00FFCC);
        retro_draw_panel(fb, CANV_W, CANV_H, 460, 262, 85, 10, 0xFF006699, 0xFF051020);
        int w_v = (81 * cfg.volume_voice) / 100;
        if (w_v > 0) retro_fill_rect(fb, CANV_W, CANV_H, 462, 264, w_v, 6, 0xFF00FFCC);

        retro_draw_panel(fb, CANV_W, CANV_H, 20, 294, 600, 102, 0xFF0077AA, 0xEE081428);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 298, "MISSION & SYSTEM PARAMETERS", 0xFF00FFFF, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 316, "Scale:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 380, 316, "Aspect:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 342, "Weapon:", 0xFFCCCCCC, 0);
        retro_draw_text(fb, CANV_W, CANV_H, 30, 368, "Shields:", 0xFFCCCCCC, 0);

        for (int i = 0; i < num_buttons; i++) {
            ButtonDef *b = &buttons[i];
            int is_hov = (hovered_btn == b->id);
            int is_sel = 0;
            if (b->id == BTN_SYNTH_OPL3) is_sel = (cfg.music_type == 0);
            else if (b->id == BTN_SYNTH_ARRANGED) is_sel = (cfg.music_type == 1);
            else if (b->id == BTN_SYNTH_MT32) is_sel = (cfg.music_type == 2);
            else if (b->id == BTN_SYNTH_SC55) is_sel = (cfg.music_type == 3);
            else if (b->id == BTN_SFX_HYBRID) is_sel = (cfg.sfx_mode == 0);
            else if (b->id == BTN_SFX_PCSPK) is_sel = (cfg.sfx_mode == 1);
            else if (b->id == BTN_SFX_ADLIB) is_sel = (cfg.sfx_mode == 2);
            else if (b->id == BTN_SCALE_1) is_sel = (!cfg.fullscreen && cfg.scale == 1);
            else if (b->id == BTN_SCALE_2) is_sel = (!cfg.fullscreen && cfg.scale == 2);
            else if (b->id == BTN_SCALE_3) is_sel = (!cfg.fullscreen && cfg.scale == 3);
            else if (b->id == BTN_SCALE_4) is_sel = (!cfg.fullscreen && cfg.scale == 4);
            else if (b->id == BTN_SCALE_FULL) is_sel = cfg.fullscreen;
            else if (b->id == BTN_ASPECT_4_3) is_sel = cfg.aspect_4_3;
            else if (b->id == BTN_ASPECT_16_10) is_sel = !cfg.aspect_4_3;
            else if (b->id == BTN_AUTOFIRE_ON) is_sel = cfg.autofire;
            else if (b->id == BTN_AUTOFIRE_OFF) is_sel = !cfg.autofire;
            else if (b->id == BTN_SHIELD_NORMAL) is_sel = !cfg.infinite_shields;
            else if (b->id == BTN_SHIELD_INFINITE) is_sel = cfg.infinite_shields;
            else if (b->id == BTN_START && ing.is_ready) {
                pulse = (pulse + 1) % 30;
                is_sel = (pulse < 15);
            }
            retro_draw_button(fb, CANV_W, CANV_H, b->x, b->y, b->w, b->h, b->label, is_hov, is_sel);
        }

        retro_draw_text(fb, CANV_W, CANV_H, 110, 456, "Use Mouse or Enter/Space to select * B: Browse * Esc: Quit", 0xFF6688AA, 0);
        draw_demo_cursor(fb, mx, my, clicking);

        char path[512];
        snprintf(path, sizeof(path), "%s/frame_%06d.ppm", frames_dir, f);
        FILE *fp = fopen(path, "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", CANV_W, CANV_H);
            for (int i = 0; i < CANV_W * CANV_H; i++) {
                uint8_t rgb[3] = { (uint8_t)(fb[i] >> 16), (uint8_t)(fb[i] >> 8), (uint8_t)fb[i] };
                fwrite(rgb, 1, 3, fp);
            }
            fclose(fp);
        }
    }

    free(fb);
    return 0;
}
