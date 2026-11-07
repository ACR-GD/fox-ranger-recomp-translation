/* cheat.c - Konami cheat code for Fox Ranger: infinite shield units + menu chime */
#include "cheat.h"
#include "audio.h"
#include "cpu.h"
#include "mem.h"
#include "platform.h"
#include "recomp_rt.h"
#include "enhanced_music.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_infinite_shield = 0;
static int g_in_menu = 1; /* Start as 1 (in menu/title/attract) until gameplay begins */
static int g_konami_step = 0;
static int g_first_btn = 0;

enum CheatKey {
    CK_NONE = 0,
    CK_UP,
    CK_DOWN,
    CK_LEFT,
    CK_RIGHT,
    CK_B,
    CK_A
};

/* Synthesized 8-bit unsigned PCM confirmation chime (C5 -> E5 -> G5 -> C6) */
static uint8_t g_chime_pcm[9480];
static int     g_chime_ready = 0;

static void cheat_build_chime(void)
{
    if (g_chime_ready) return;
    const uint32_t rate = 22050;
    const struct { double freq; double dur; } notes[] = {
        { 523.25, 0.07 }, /* C5 */
        { 659.25, 0.07 }, /* E5 */
        { 783.99, 0.07 }, /* G5 */
        { 1046.50, 0.22 }, /* C6 */
    };
    uint32_t pos = 0;
    for (size_t k = 0; k < sizeof(notes) / sizeof(notes[0]); k++) {
        uint32_t n = (uint32_t)(rate * notes[k].dur);
        double freq = notes[k].freq;
        for (uint32_t i = 0; i < n && pos < sizeof(g_chime_pcm); i++) {
            double t = (double)i / (double)rate;
            double attack = (double)i / (double)(rate * 0.005);
            if (attack > 1.0) attack = 1.0;
            double decay = exp(-3.5 * ((double)i / (double)n));
            double env = attack * decay;
            double w = 0.70 * sin(2.0 * M_PI * freq * t)
                     + 0.20 * sin(4.0 * M_PI * freq * t)
                     + 0.10 * sin(6.0 * M_PI * freq * t);
            int v = (int)(128.0 + 90.0 * env * w + 0.5);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            g_chime_pcm[pos++] = (uint8_t)v;
        }
    }
    g_chime_ready = 1;
}

void cheat_play_sound(void)
{
    cheat_build_chime();
    audio_voice_play(g_chime_pcm, (uint32_t)sizeof(g_chime_pcm), 22050);
}

static void cheat_activate(void)
{
    g_infinite_shield = 1;
    if (g_load_seg) {
        wr8(SEG(0x1ADF), 0x47, 9);
        wr16(SEG(0x1682), 0x2F74, 9);
    }
    cheat_play_sound();
    fprintf(stderr, "\n=======================================================\n");
    fprintf(stderr, " [CHEAT] Konami code activated: infinite shield enabled!\n");
    fprintf(stderr, "=======================================================\n\n");
}

int cheat_is_infinite_shield(void)
{
    return g_infinite_shield;
}

void cheat_set_infinite_shield(int enabled)
{
    g_infinite_shield = enabled;
}

void cheat_set_in_menu(int in_menu)
{
    g_in_menu = in_menu;
}

void cheat_on_key(SDL_Scancode s)
{
    /* Only allow entering the code when in main menu or title screen */
    if (!g_in_menu) return;

    enum CheatKey k = CK_NONE;
    if (s == SDL_SCANCODE_UP || s == SDL_SCANCODE_KP_8) k = CK_UP;
    else if (s == SDL_SCANCODE_DOWN || s == SDL_SCANCODE_KP_2) k = CK_DOWN;
    else if (s == SDL_SCANCODE_LEFT || s == SDL_SCANCODE_KP_4) k = CK_LEFT;
    else if (s == SDL_SCANCODE_RIGHT || s == SDL_SCANCODE_KP_6) k = CK_RIGHT;
    else if (s == SDL_SCANCODE_B || s == SDL_SCANCODE_C || s == SDL_SCANCODE_LALT || s == SDL_SCANCODE_RALT) k = CK_B;
    else if (s == SDL_SCANCODE_A || s == SDL_SCANCODE_D || s == SDL_SCANCODE_LCTRL || s == SDL_SCANCODE_RCTRL) k = CK_A;
    else {
        g_konami_step = 0;
        return;
    }

    switch (g_konami_step) {
    case 0:
        if (k == CK_UP) g_konami_step = 1;
        break;
    case 1:
        if (k == CK_UP) g_konami_step = 2;
        else g_konami_step = 0;
        break;
    case 2:
        if (k == CK_DOWN) g_konami_step = 3;
        else if (k == CK_UP) g_konami_step = 2;
        else g_konami_step = 0;
        break;
    case 3:
        if (k == CK_DOWN) g_konami_step = 4;
        else if (k == CK_UP) g_konami_step = 1;
        else g_konami_step = 0;
        break;
    case 4:
        if (k == CK_LEFT) g_konami_step = 5;
        else if (k == CK_UP) g_konami_step = 1;
        else g_konami_step = 0;
        break;
    case 5:
        if (k == CK_RIGHT) g_konami_step = 6;
        else if (k == CK_UP) g_konami_step = 1;
        else g_konami_step = 0;
        break;
    case 6:
        if (k == CK_LEFT) g_konami_step = 7;
        else if (k == CK_UP) g_konami_step = 1;
        else g_konami_step = 0;
        break;
    case 7:
        if (k == CK_RIGHT) g_konami_step = 8;
        else if (k == CK_UP) g_konami_step = 1;
        else g_konami_step = 0;
        break;
    case 8:
        if (k == CK_B || k == CK_A) {
            g_first_btn = k;
            g_konami_step = 9;
        } else if (k == CK_UP) {
            g_konami_step = 1;
        } else {
            g_konami_step = 0;
        }
        break;
    case 9:
        if ((g_first_btn == CK_B && k == CK_A) || (g_first_btn == CK_A && k == CK_B)) {
            cheat_activate();
            g_konami_step = 0;
        } else if (k == CK_UP) {
            g_konami_step = 1;
        } else {
            g_konami_step = 0;
        }
        break;
    default:
        g_konami_step = 0;
        break;
    }
}

void cheat_poll(Cpu *c)
{
    (void)c;
    if (g_infinite_shield && !g_in_menu) {
        if (g_load_seg) {
            uint16_t cur = rd16(SEG(0x1682), 0x2F74);
            if (cur < 9 || cur == 0x4B3) {
                wr16(SEG(0x1682), 0x2F74, 9);
            }
        }
    }
}

int cheat_call_hook(Cpu *c, int is_far, uint16_t seg_img, uint16_t off)
{
    (void)is_far;
    /* Hook sound effect dispatch (030C:1511):
     * In AdLib mode (mode == 2), player weapon firing (arg == 0) was skipped
     * in the original x86 code due to an original typo:
     *   cmp si, 1; je L_1554; jmp L_1570
     * which only executed L_1554 for si == 1. The instruction 'lea ax, [si+8]'
     * maps si = 0 to channel 8 (Tom-tom gunshot) and si = 1 to channel 9 (Top Cymbal).
     * We restore channel 8 note-on for arg == 0.
     *
     * For enemy destruction (arg == 1):
     * The original code called PC speaker 400 Hz tone (0E0C:0090(0x190)) in PC speaker mode,
     * while in AdLib mode it only triggered channel 9 (Top Cymbal), which was inaudible
     * due to high attenuation and music rhythm mask. We play the 400 Hz PC speaker burst
     * and in AdLib mode trigger full-impact explosion percussion (Bass Drum + Tom-tom + Cymbal). */
    if (seg_img == 0x030C && off == 0x1511) {
        uint16_t arg = rd16(c->ss, (uint16_t)(c->sp + 4));
        uint16_t es = rd16(c->ds, 0x175E);
        uint8_t mode = rd8(es, 0x452A);
        if (mode == 2 && arg == 0) {
            uint16_t saved_si = c->si, saved_di = c->di;
            c->si = 8;
            push16(c, 8);
            rt_call_far(c, NULL, 0x1265, 0x04DF, SEG(0x030C), 0x155D);
            c->sp = (uint16_t)(c->sp + 2);

            push16(c, 0x3C);
            push16(c, 8);
            rt_call_far(c, NULL, 0x1265, 0x0400, SEG(0x030C), 0x156D);
            c->sp = (uint16_t)(c->sp + 4);

            c->si = saved_si;
            c->di = saved_di;
            c->sp = (uint16_t)(c->sp + 4); /* far ret */
            return 1;
        }
        if (arg == 1) {
            uint16_t saved_si = c->si, saved_di = c->di;

            /* 1. Play 400 Hz PC-speaker tone (authentic original DOS destruction effect) */
            push16(c, 0x190);
            rt_call_far(c, NULL, 0x0E0C, 0x0090, SEG(0x030C), 0x169E);
            c->sp = (uint16_t)(c->sp + 2);
            wr8(c->ds, 0xB6, 1);

            /* 2. In AdLib mode, trigger punchy explosion percussion:
             * Bass Drum (ch 6) + Tom-tom (ch 8) + Cymbal crash (ch 9) */
            if (mode == 2) {
                /* Note off & on for Bass Drum (channel 6) */
                push16(c, 6);
                rt_call_far(c, NULL, 0x1265, 0x04DF, SEG(0x030C), 0x155D);
                c->sp = (uint16_t)(c->sp + 2);
                push16(c, 0x24);
                push16(c, 6);
                rt_call_far(c, NULL, 0x1265, 0x0400, SEG(0x030C), 0x156D);
                c->sp = (uint16_t)(c->sp + 4);

                /* Note off & on for Tom-tom (channel 8) */
                push16(c, 8);
                rt_call_far(c, NULL, 0x1265, 0x04DF, SEG(0x030C), 0x155D);
                c->sp = (uint16_t)(c->sp + 2);
                push16(c, 0x20);
                push16(c, 8);
                rt_call_far(c, NULL, 0x1265, 0x0400, SEG(0x030C), 0x156D);
                c->sp = (uint16_t)(c->sp + 4);

                /* Note off & on for Cymbal (channel 9) */
                push16(c, 9);
                rt_call_far(c, NULL, 0x1265, 0x04DF, SEG(0x030C), 0x155D);
                c->sp = (uint16_t)(c->sp + 2);
                push16(c, 0x3C);
                push16(c, 9);
                rt_call_far(c, NULL, 0x1265, 0x0400, SEG(0x030C), 0x156D);
                c->sp = (uint16_t)(c->sp + 4);

                /* Unmute drum operators for loud explosion burst */
                audio_opl_write(0x50, 0x00); /* Bass Drum op 13 */
                audio_opl_write(0x53, 0x00); /* Bass Drum op 16 */
                audio_opl_write(0x52, 0x00); /* Tom-tom op 15 */
                audio_opl_write(0x54, 0x00); /* Snare op 17 */
                audio_opl_write(0x55, 0x00); /* Top Cymbal op 18 */
            }

            c->si = saved_si;
            c->di = saved_di;
            c->sp = (uint16_t)(c->sp + 4); /* far ret */
            return 1;
        }
    }

    /* Hook music start (030C:002C) and stop (030C:0475) for modern arranged soundtrack */
    if (seg_img == 0x030C) {
        if (off == 0x002C) {
            uint16_t snd_seg = rd16(c->ds, 0x1722);
            if (!snd_seg) snd_seg = c->es;
            uint8_t track = rd8(snd_seg, 0x48);
            enhanced_music_play_track((int)track);
            return 0;
        }
        if (off == 0x0475) {
            enhanced_music_stop();
            return 0;
        }
    }

    /* Track state transitions */
    if (seg_img == 0x0000) {
        if (off == 0x0E9D || off == 0x0B00) {
            /* Main menu or title screen */
            g_in_menu = 1;
        } else if (off == 0x1D38) {
            /* Stage loop */
            g_in_menu = 0;
            if (g_infinite_shield && g_load_seg) {
                wr8(SEG(0x1ADF), 0x47, 9);
                wr16(SEG(0x1682), 0x2F74, 9);
            }
        }
        return 0;
    }

    if (!g_infinite_shield) return 0;

    /* Intercept player take hit (0x0D81:0x0562) */
    if (seg_img == 0x0D81 && off == 0x0562) {
        if (g_load_seg) {
            /* Trigger hit-flash so damage is visually registered */
            wr8(SEG(0x1682), 0x2F78, 0x64);
            /* Keep shields full */
            wr16(SEG(0x1682), 0x2F74, 9);
        }
        /* Emulate retf */
        c->sp = (uint16_t)(c->sp + 4);
        return 1;
    }

    /* Intercept player death (0x0D81:0x0514) */
    if (seg_img == 0x0D81 && off == 0x0514) {
        if (g_load_seg) {
            wr16(SEG(0x1682), 0x2F74, 9);
        }
        /* Emulate retf */
        c->sp = (uint16_t)(c->sp + 4);
        return 1;
    }

    return 0;
}

void cheat_init(void)
{
    rt_call_hook = cheat_call_hook;
    g_konami_step = 0;
    g_first_btn = 0;
    cheat_build_chime();
}
