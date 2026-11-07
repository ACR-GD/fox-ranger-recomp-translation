/* main_sdl.c - boots the recompiled Fox Ranger.
 *
 * Loads the (unpacked, translated) EXE image into emulated memory like DOS,
 * installs the native layers (CRT shims, blitters, CT-VOICE, platform BIOS /
 * interrupts) and the recompiled code, then performs the work of the MSC
 * start-up code (134D:001A) natively and calls main() at 0000:0175:
 *   SS = DS = DGROUP, SP = 2CBEh (stack top after `add sp, 24BEh`),
 *   BSS DS:2174-24BF zeroed, main(argc, argv, envp) with argv from the
 *   command tail (default " a v" = AdLib music + voice, as in FR.BAT).
 *
 *   foxrt [--game DIR] [--exe FILE] [--args " a v"] [--scale N] [--verbose]
 *         [--headless] [--seconds S] [--keys "T:KEY,..."] [--shots "T:file.ppm,..."]
 *         [--wav out.wav]
 *
 * Controls: arrows move, D = gun (Ctrl), C = bomb/special (Alt), Space/Enter
 * select, Esc. The original Ctrl/Alt keys keep working. Scripted keys: ENTER
 * SPACE ESC UP DOWN LEFT RIGHT CTRL ALT SHIFT FIRE BOMB A-Z 0-9.
 *
 * The copy-protection check (04A4:14B0) is replaced natively (always passes),
 * and all text comes from the loaded image, so work/FOX_EN_FINAL.EXE gives
 * the English game.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "blit_10a7.h"
#include "cpu.h"
#include "crt_shims.h"
#include "ctvoice.h"
#include "dos.h"
#include "heap.h"
#include "mem.h"
#include "mz_load.h"
#include "natives.h"
#include "platform.h"
#include "cheat.h"
#include "recomp_gen.h"
#include "vga_modex.h"
#include "launcher.h"
#include "launcher_config.h"
#include "ingest.h"
#include "enhanced_music.h"
#include <SDL.h>

#define LOAD_SEG   0x0800          /* PSP at 07F0h: ~620 KB conventional memory free */
#define DGROUP_IMG 0x1ADF
#define MAIN_SEG   0x0000
#define MAIN_OFF   0x0175

static jmp_buf g_exit_jb;
static FILE   *g_calls;
static void log_call(Cpu *c, uint16_t seg, uint16_t off, const char *name)
{
    (void)c;
    fprintf(g_calls, "%9.4f %04X:%04X %s\n", platform_now_us() / 1e6, seg, off, name ? name : "?");
}
static int     g_exit_code;
static void on_exit_request(int code) { g_exit_code = code; longjmp(g_exit_jb, 1); }

/* build argv/envp for main(): far pointers in a heap block */
static void build_args(Cpu *c, const char *tail, uint16_t *argc_out, FarPtr *argv_out, FarPtr *envp_out)
{
    uint16_t seg = heap_alloc_paras(512);
    uint16_t str = 0x40, argc = 0;
    const char *argv0 = "C:\\FOX\\FOX.EXE";
    char buf[128];
    snprintf(buf, sizeof buf, "%s", tail ? tail : "");
    const char *words[16] = { argv0 };
    argc = 1;
    for (char *t = strtok(buf, " \t"); t && argc < 15; t = strtok(NULL, " \t")) words[argc++] = t;
    for (uint16_t i = 0; i < argc; i++) {
        wr16(seg, (uint16_t)(4 * i), str); wr16(seg, (uint16_t)(4 * i + 2), seg);
        for (const char *p = words[i]; ; p++) { wr8(seg, str++, (uint8_t)*p); if (!*p) break; }
    }
    wr16(seg, (uint16_t)(4 * argc), 0); wr16(seg, (uint16_t)(4 * argc + 2), 0);   /* argv[argc] = NULL */
    uint16_t envp = (uint16_t)(4 * argc + 4);
    wr16(seg, envp, 0); wr16(seg, (uint16_t)(envp + 2), 0);                        /* empty environment */
    (void)c;
    *argc_out = argc;
    *argv_out = (FarPtr){ 0, seg };
    *envp_out = (FarPtr){ envp, seg };
}

int main(int argc, char **argv)
{
    const char *game = "fox-ranger", *exe = "work/FOX_EN_FINAL.EXE", *tail = " a v";
    const char *cli_game = NULL, *cli_exe = NULL;
    int cli_scale = 0;
    int no_launcher = 0;
    PlatformOpts po = { 0 };
    po.scale = 3;
    po.aspect_4_3 = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "%s needs a value\n", a), exit(2), ""))
        if (!strcmp(a, "--game")) { game = NEXT(); cli_game = game; }
        else if (!strcmp(a, "--exe")) { exe = NEXT(); cli_exe = exe; }
        else if (!strcmp(a, "--args")) tail = NEXT();
        else if (!strcmp(a, "--scale")) { po.scale = atoi(NEXT()); cli_scale = po.scale; }
        else if (!strcmp(a, "--no-launcher")) no_launcher = 1;
        else if (!strcmp(a, "--headless")) po.headless = 1;
        else if (!strcmp(a, "--seconds")) po.seconds = atof(NEXT());
        else if (!strcmp(a, "--keys")) po.keys = NEXT();
        else if (!strcmp(a, "--shots")) po.shots = NEXT();
        else if (!strcmp(a, "--wav")) po.wav = NEXT();
        else if (!strcmp(a, "--verbose")) po.verbose = 1;
        else if (!strcmp(a, "--trace-calls")) { g_calls = fopen(NEXT(), "w"); rt_call_log = g_calls ? log_call : NULL; }
        else {
            fprintf(stderr, "usage: %s [--game DIR] [--exe FILE] [--args \" a v\"] [--scale N] [--verbose]\n"
                            "       [--headless] [--seconds S] [--keys \"T:KEY,...\"] [--shots \"T:f.ppm,...\"] [--wav f.wav]\n"
                            "       [--no-launcher]\n", argv[0]);
            return 2;
        }
    }

    LauncherConfig cfg;
    launcher_config_load(&cfg);

    int is_scripted = (po.headless || po.keys != NULL || po.shots != NULL);
    if (!is_scripted && !no_launcher) {
        if (cli_game) snprintf(cfg.game_dir, sizeof(cfg.game_dir), "%s", cli_game);
        if (cli_exe) snprintf(cfg.exe_path, sizeof(cfg.exe_path), "%s", cli_exe);
        if (cli_scale > 0) cfg.scale = cli_scale;

        LauncherResult res = launcher_run(&cfg);
        if (res != LAUNCHER_START_GAME) {
            return 0;
        }
        game = cfg.game_dir;
        exe = cfg.exe_path;
        po.scale = cfg.scale;
        po.fullscreen = cfg.fullscreen;
        po.aspect_4_3 = cfg.aspect_4_3;
        po.autofire = cfg.autofire;
        cheat_set_infinite_shield(cfg.infinite_shields);
    }

    if (!po.headless && po.keys == NULL && po.shots == NULL) {
        if (cfg.music_type == 2) tail = " m v";
        else if (cfg.music_type == 3) tail = " s v";
        else if (cfg.sfx_mode == 1) tail = " i v";
        else tail = " a v";
    }

    /* Enhanced soundtrack setup & volume levels */
    char enh_dir[512];
    snprintf(enh_dir, sizeof(enh_dir), "%s/enhanced_music", game);
    enhanced_music_init(enh_dir);
    enhanced_music_set_enabled(cfg.music_type == 1);

    audio_set_master_volume(cfg.volume_master);
    audio_set_music_volume(cfg.volume_music);
    audio_set_sfx_volume(cfg.volume_sfx);
    audio_set_voice_volume(cfg.volume_voice);

    /* Verify executable path with fallbacks */
    static char exe_resolved[512];
    FILE *f_check = fopen(exe, "rb");
    if (f_check) {
        fclose(f_check);
    } else {
        snprintf(exe_resolved, sizeof(exe_resolved), "%s/FOX_EN_FINAL.EXE", game);
        f_check = fopen(exe_resolved, "rb");
        if (f_check) { fclose(f_check); exe = exe_resolved; }
        else {
            snprintf(exe_resolved, sizeof(exe_resolved), "%s/FOX_EN.EXE", game);
            f_check = fopen(exe_resolved, "rb");
            if (f_check) { fclose(f_check); exe = exe_resolved; }
            else if ((f_check = fopen("work/FOX_EN_FINAL.EXE", "rb"))) {
                fclose(f_check);
                exe = "work/FOX_EN_FINAL.EXE";
            }
        }
    }

    mem_reset();
    vga_reset(&g_vga);
    MzInfo mz;
    if (mz_load(exe, LOAD_SEG, tail, &mz)) return 1;
    if (po.verbose)
        fprintf(stderr, "loaded %s at %04X: %u bytes, %u relocs; heap %04X-9FFF\n",
                exe, LOAD_SEG, mz.image_size, mz.nrelocs, mz.end_seg);
    dos_init(game);
    heap_init(mz.end_seg, 0xA000);

    /* registration order matters: the first registration of an address wins */
    natives_register();
    blit_10a7_register();
    ctvoice_register();
    crt_register();
    crt_init();
    recomp_register_all();
    cheat_init();

    uint64_t t0 = SDL_GetTicks64();
    if (platform_init(&po)) return 1;
    uint64_t t1 = SDL_GetTicks64();
    if (audio_init(AUDIO_RATE_DEFAULT, !po.headless) && !po.headless)
        fprintf(stderr, "audio: no output device, continuing silently\n");
    uint64_t t2 = SDL_GetTicks64();
    platform_install();                       /* starts the clocks: after the slow host init */
    if (po.verbose && !po.headless)
        fprintf(stderr, "host init: video %llu ms, audio %llu ms\n", (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1));
    platform_set_exit(on_exit_request);
    dos_exit_hook = on_exit_request;

    static Cpu cpu;
    Cpu *c = &cpu;
    memset(c, 0, sizeof *c);
    uint16_t ds = SEG(DGROUP_IMG);
    c->ds = c->es = c->ss = ds;
    c->sp = 0x2CBE;
    c->ifl = 1;
    for (uint16_t o = 0x2174; o < 0x24C0; o++) wr8(ds, o, 0);       /* BSS */

    uint16_t gargc; FarPtr gargv, genvp;
    build_args(c, tail, &gargc, &gargv, &genvp);
    push16(c, genvp.seg); push16(c, genvp.off);
    push16(c, gargv.seg); push16(c, gargv.off);
    push16(c, gargc);

    if (!setjmp(g_exit_jb)) {
        far_call(c, SEG(MAIN_SEG), MAIN_OFF, 0, 0);
        g_exit_code = (int16_t)c->ax;
    }

    PlatformStats st = platform_stats();
    AudioStats ast = audio_stats();
    if (po.verbose) {
        fprintf(stderr, "exit %d after %.2f s: %llu timer IRQs, %llu keyboard IRQs, %llu frames, %llu idle waits, %llu polls\n",
                g_exit_code, platform_now_us() / 1e6, (unsigned long long)st.irq0, (unsigned long long)st.irq1,
                (unsigned long long)st.frames, (unsigned long long)st.idle_sleeps, (unsigned long long)st.polls);
        fprintf(stderr, "audio stats: %u opl writes, %u voice clips, %u voice frames\n",
                ast.opl_writes, ast.voice_clips, ast.voice_frames_out);
    }
    platform_shutdown();
    audio_shutdown();
    enhanced_music_shutdown();
    if (g_calls) fclose(g_calls);
    return g_exit_code & 0xFF;
}
