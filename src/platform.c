#include "platform.h"
#include "audio.h"
#include "cheat.h"
#include "ctvoice.h"
#include "io.h"
#include "mem.h"
#include "recomp_rt.h"
#include "vga_modex.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FRAME_US    (1000000.0 / 70.086)       /* VGA 320x200@70 Hz */
#define RETRACE_US  1430.0                     /* vertical blanking ~ 45 of 449 lines */
#define BIOS_SEG    0xF000

static PlatformOpts  g_o;
static int           g_log_idle, g_log_rate;      /* FOX_IDLE_LOG / FOX_RATE_LOG, read once */
static PlatformStats g_st;
static void        (*g_exit_fn)(int);

/* ------------------------------------------------------------------ clock */
static uint64_t g_virt_us, g_pc0, g_pcf;
uint64_t platform_now_us(void)
{
    if (g_o.headless) return g_virt_us;
    return (SDL_GetPerformanceCounter() - g_pc0) * 1000000ull / g_pcf;
}

/* ------------------------------------------------------------- SDL video */
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static int           g_tex_h;
static uint32_t      g_px[320 * 480];

static double g_t_present_ms, g_t_sleep_ms;
void platform_present(void)
{
    if (!g_ren) return;
    g_st.frames++;
    uint64_t p0 = SDL_GetPerformanceCounter();
    int w = vga_width(&g_vga), h = vga_height(&g_vga);
    if (w > 320) w = 320;
    if (!g_tex || g_tex_h != h) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        g_tex_h = h;
    }
    vga_render_argb(&g_vga, g_px, w);
    SDL_UpdateTexture(g_tex, NULL, g_px, w * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
    g_t_present_ms += (SDL_GetPerformanceCounter() - p0) * 1000.0 / g_pcf;
}

static int write_ppm(const char *path)
{
    int w = vga_width(&g_vga), h = vga_height(&g_vga);
    if (w > 320) w = 320;
    vga_render_argb(&g_vga, g_px, w);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = { (uint8_t)(g_px[i] >> 16), (uint8_t)(g_px[i] >> 8), (uint8_t)g_px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* --------------------------------------------------------- XT scancodes */
static uint8_t sdl_to_xt(SDL_Scancode s)
{
    static const uint8_t letters[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                         0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return (uint8_t)(0x02 + (s - SDL_SCANCODE_1));
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return (uint8_t)(0x3B + (s - SDL_SCANCODE_F1));
    switch (s) {
    case SDL_SCANCODE_0: return 0x0B;          case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_RETURN: return 0x1C;     case SDL_SCANCODE_KP_ENTER: return 0x1C;
    case SDL_SCANCODE_SPACE: return 0x39;      case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;        case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;     case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;     case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_LSHIFT: return 0x2A;     case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4B;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return 0x51;
    default: return 0;
    }
}

/* ---------------------------------------------------- control bindings
 * The game's keyboard ISR (0C6A:0139) knows Ctrl = button 1 (gun: sets
 * key-state bits 400h|10h) and Alt = button 2 (bomb/special: 800h|20h), the
 * same bits as its parallel-port pad. On a Mac Ctrl+arrow is taken by the
 * system, so D and C are bound as well. They send the button's scancode
 * *and* their own letter, so they still type in the high-score name entry
 * (the ISR stores the last raw scancode; gameplay ignores letter codes). */
#define SC_CTRL 0x1D
#define SC_ALT  0x38
static int bind_extra(SDL_Scancode s, uint8_t out[2])
{
    switch (s) {
    case SDL_SCANCODE_D: out[0] = SC_CTRL; return 1;   /* gun   */
    case SDL_SCANCODE_C: out[0] = SC_ALT;  return 1;   /* bomb  */
    default: return 0;
    }
}

static void kq_push(uint8_t sc);
static int g_fire_held = 0;
static uint32_t g_fire_pulse_frame = 0;

static void key_event(SDL_Scancode s, int down)
{
    if (s == SDL_SCANCODE_D || s == SDL_SCANCODE_LCTRL) {
        g_fire_held = down;
    }
    if (down) cheat_on_key(s);
    uint8_t extra[2];
    int n = bind_extra(s, extra);
    uint8_t sc = sdl_to_xt(s);
    if (down) {                                     /* button first, letter last */
        for (int i = 0; i < n; i++) kq_push(extra[i]);
        if (sc) kq_push(sc);
    } else {                                        /* release in reverse order  */
        if (sc) kq_push((uint8_t)(sc | 0x80));
        for (int i = n - 1; i >= 0; i--) kq_push((uint8_t)(extra[i] | 0x80));
    }
}

/* script key names -> SDL scancodes (so scripted taps use the same bindings) */
static SDL_Scancode name_to_sdl(const char *n)
{
    static const struct { const char *n; SDL_Scancode s; } t[] = {
        { "ENTER", SDL_SCANCODE_RETURN }, { "SPACE", SDL_SCANCODE_SPACE }, { "ESC", SDL_SCANCODE_ESCAPE },
        { "UP", SDL_SCANCODE_UP }, { "DOWN", SDL_SCANCODE_DOWN }, { "LEFT", SDL_SCANCODE_LEFT },
        { "RIGHT", SDL_SCANCODE_RIGHT }, { "CTRL", SDL_SCANCODE_LCTRL }, { "ALT", SDL_SCANCODE_LALT },
        { "SHIFT", SDL_SCANCODE_LSHIFT }, { "FIRE", SDL_SCANCODE_D }, { "BOMB", SDL_SCANCODE_C } };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(n, t[i].n)) return t[i].s;
    if (n[0] >= 'A' && n[0] <= 'Z' && !n[1]) return (SDL_Scancode)(SDL_SCANCODE_A + (n[0] - 'A'));
    if (n[0] >= '1' && n[0] <= '9' && !n[1]) return (SDL_Scancode)(SDL_SCANCODE_1 + (n[0] - '1'));
    if (n[0] == '0' && !n[1]) return SDL_SCANCODE_0;
    return SDL_SCANCODE_UNKNOWN;
}

/* ------------------------------------------------------ keyboard queue */
#define KQ 64
static uint8_t g_kq[KQ];
static int     g_kq_r, g_kq_w;
static void kq_push(uint8_t sc) { int n = (g_kq_w + 1) % KQ; if (n != g_kq_r) { g_kq[g_kq_w] = sc; g_kq_w = n; } }

/* -------------------------------------------------- scripted events */
typedef struct { uint64_t t; int kind; int key; char path[200]; } Ev;   /* kind 0 key down, 2 key up, 1 shot */
static Ev  *g_ev; static int g_nev, g_iev;

static int ev_cmp(const void *a, const void *b)
{
    const Ev *x = a, *y = b;
    return x->t < y->t ? -1 : x->t > y->t;
}

static void parse_events(void)
{
    char buf[4096];
    for (int pass = 0; pass < 2; pass++) {
        const char *src = pass ? g_o.shots : g_o.keys;
        if (!src) continue;
        snprintf(buf, sizeof buf, "%s", src);
        for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            char *colon = strchr(tok, ':');
            if (!colon) continue;
            *colon = 0;
            uint64_t t = (uint64_t)(atof(tok) * 1e6);
            g_ev = realloc(g_ev, sizeof *g_ev * (size_t)(g_nev + 2));
            if (pass) { g_ev[g_nev] = (Ev){ t, 1, 0, "" }; snprintf(g_ev[g_nev].path, sizeof g_ev[g_nev].path, "%s", colon + 1); g_nev++; }
            else {
                SDL_Scancode k = name_to_sdl(colon + 1);
                if (k == SDL_SCANCODE_UNKNOWN) { fprintf(stderr, "unknown key '%s'\n", colon + 1); continue; }
                g_ev[g_nev++] = (Ev){ t, 0, (int)k, "" };
                g_ev[g_nev++] = (Ev){ t + 120000, 2, (int)k, "" };      /* release after 120 ms */
            }
        }
    }
    if (g_nev) qsort(g_ev, (size_t)g_nev, sizeof *g_ev, ev_cmp);
}

/* ------------------------------------------------------ headless audio */
static FILE    *g_wav;
static uint64_t g_audio_us, g_wav_frames;
static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t rate = (uint32_t)audio_sample_rate(), bytes = frames * 4;
    uint8_t h[44] = "RIFF....WAVEfmt ";
    uint32_t v[] = { 36 + bytes };
    memcpy(h + 4, v, 4);
    uint8_t fmt[24] = { 16, 0, 0, 0, 1, 0, 2, 0 };
    memcpy(fmt + 8, &rate, 4);
    uint32_t br = rate * 4; memcpy(fmt + 12, &br, 4);
    fmt[16] = 4; fmt[18] = 16;
    memcpy(h + 16, fmt, 20);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f); fseek(f, 0, SEEK_END);
}
static void headless_audio(uint64_t now)
{
    if (!g_o.headless) return;
    uint64_t want = now * (uint64_t)audio_sample_rate() / 1000000ull;
    static int16_t buf[2 * 4096];
    while (g_wav_frames < want) {
        uint32_t n = (uint32_t)(want - g_wav_frames); if (n > 4096) n = 4096;
        audio_render(buf, n);
        if (g_wav) fwrite(buf, 4, n, g_wav);
        g_wav_frames += n;
    }
    g_audio_us = now;
}

/* ------------------------------------------------------- interrupts */
static void native_iret(Cpu *c)
{
    c->sp = (uint16_t)(c->sp + 4);                 /* ip, cs */
    uint16_t fl = pop16(c);
    c->ifl = (fl >> 9) & 1; c->df = (fl >> 10) & 1;
}

static void bios_int08(Cpu *c)                     /* F000:FEA5 */
{
    uint32_t t = ((uint32_t)rd16(0x40, 0x6E) << 16 | rd16(0x40, 0x6C)) + 1;
    if (t >= 0x1800B0u) { t = 0; wr8(0x40, 0x70, 1); }
    wr16(0x40, 0x6C, (uint16_t)t); wr16(0x40, 0x6E, (uint16_t)(t >> 16));
    native_iret(c);
}
static void bios_int09(Cpu *c) { (void)port_in8(0x60); native_iret(c); }   /* F000:E987 */
static void bios_dummy(Cpu *c) { native_iret(c); }                        /* F000:FF53 */

static void deliver(Cpu *c, int irq)
{
    uint8_t vec = (uint8_t)(8 + irq);
    uint16_t off = rd16(0, (uint16_t)(vec * 4)), seg = rd16(0, (uint16_t)(vec * 4 + 2));
    int flags = 0;
    RecompFn fn = far_lookup_ex(seg, off, &flags, NULL);
    if (!fn) {
        static int warned;
        if (!warned++) fprintf(stderr, "IRQ%d: vector %04X:%04X has no handler\n", irq, seg, off);
        return;
    }
    uint16_t fl = (uint16_t)(0x0002 | (c->ifl << 9) | (c->df << 10));
    push16(c, fl); push16(c, c->cs); push16(c, 0xFFFF);
    c->ifl = 0;
    uint16_t saved = c->cs;
    c->cs = seg;
    rt_isr_call(c, fn);
    c->cs = saved;
    if (irq == 0) g_st.irq0++; else g_st.irq1++;
}

static double   g_next_tick, g_next_frame, g_present_due, g_present_us = 1e6 / 60.0;
static double   g_t_frame0;
static double   frame_phase(double now);
static uint64_t g_last_events;
static uint32_t g_last_writes;

static double tick_us(void) { return 1000000.0 / pit_ch0_hz(); }

static void pump_sdl(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT && g_exit_fn) g_exit_fn(0);
        if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat)
            key_event(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
    }
}

static void poll(Cpu *c, uint32_t at)
{
    cheat_poll(c);
    if (g_o.autofire && g_fire_held) {
        g_fire_pulse_frame++;
        if ((g_fire_pulse_frame % 4) == 2) {
            kq_push((uint8_t)(SC_CTRL | 0x80));
        } else if ((g_fire_pulse_frame % 4) == 0) {
            kq_push(SC_CTRL);
        }
    }
    g_st.polls++;
    uint64_t now = platform_now_us();
    if (g_o.verbose && (g_st.polls & 0xFFFFF) == 0)
        fprintf(stderr, "[poll %llu] t=%.3fs at=%05X ifl=%d isr=%d irq0=%llu irq1=%llu frames=%llu idle=%llu PIT %.1f Hz\n",
                (unsigned long long)g_st.polls, now / 1e6, at, c->ifl, rt_isr_depth(),
                (unsigned long long)g_st.irq0, (unsigned long long)g_st.irq1, (unsigned long long)g_st.frames,
                (unsigned long long)g_st.idle_sleeps, pit_ch0_hz());
    int delivered = 0;

    if (g_o.seconds > 0 && now >= (uint64_t)(g_o.seconds * 1e6) && g_exit_fn) g_exit_fn(0);
    if (g_log_rate) {
        static uint64_t last_s, last_irq0, lost;
        if (now / 1000000 != last_s) {
            fprintf(stderr, "[%3llus] PIT %.1f Hz, IRQ0 delivered %llu/s, ifl=%d depth=%d lag %.1f ms\n",
                    (unsigned long long)last_s, pit_ch0_hz(), (unsigned long long)(g_st.irq0 - last_irq0), c->ifl,
                    rt_isr_depth(), ((double)now - g_next_tick) / 1000.0);
            last_s = now / 1000000; last_irq0 = g_st.irq0; (void)lost;
        }
    }

    /* scripted events (headless or interactive) */
    while (g_iev < g_nev && g_ev[g_iev].t <= now) {
        Ev *e = &g_ev[g_iev++];
        if (e->kind != 1) key_event((SDL_Scancode)e->key, e->kind == 0);
        else if (!write_ppm(e->path) && g_o.verbose) fprintf(stderr, "[%.2fs] screenshot %s\n", now / 1e6, e->path);
    }

    /* IRQ0: fixed-timestep accumulator at the programmed PIT rate */
    int can_irq = c->ifl && rt_isr_depth() < 2;
    if (now > g_next_tick + 250000.0) g_next_tick = (double)now;     /* resync after a stall */
    for (int k = 0; k < 8 && can_irq && !(g_io.pic_imr & 1) && (double)now >= g_next_tick; k++) {
        g_next_tick += tick_us();
        deliver(c, 0); delivered = 1;
        can_irq = c->ifl && rt_isr_depth() < 2;
    }
    /* IRQ1: one scancode per poll */
    if (g_kq_r != g_kq_w && can_irq && !(g_io.pic_imr & 2)) {
        g_io.kbd_data = g_kq[g_kq_r]; g_kq_r = (g_kq_r + 1) % KQ;
        deliver(c, 1); delivered = 1;
    }

    /* video + window events */
    if ((double)now >= g_next_frame) {
        g_next_frame += FRAME_US;
        if ((double)now > g_next_frame) g_next_frame = (double)now + FRAME_US;
        /* Presentation must never pace the game: the game thread owns the
         * emulated machine. VGA frames (70 Hz) are dropped to stay at or below
         * the display rate, and after a slow present (compositor back-pressure)
         * the next ones are skipped. Headless: every frame counts. */
        if (g_o.headless) g_st.frames++;
        else if ((double)now >= g_present_due) {
            uint64_t p0 = SDL_GetPerformanceCounter();
            platform_present();
            double took = (SDL_GetPerformanceCounter() - p0) * 1e6 / (double)g_pcf;
            g_present_due = (double)now + g_present_us + (took > g_present_us / 2 ? took * 2 : 0);
        }
    }
    if (!g_o.headless && now - g_last_events >= 4000) { g_last_events = now; pump_sdl(); }
    ctvoice_pump();
    headless_audio(now);

    /* idle: no memory writes since the last poll -> busy-wait; skip to the next event */
    if (!delivered && g_mem_writes == g_last_writes) {
        double ph = frame_phase((double)now);
        double next_frame = (double)now - ph + FRAME_US;              /* next retrace start */
        double next = g_next_tick < next_frame ? g_next_tick : next_frame;
        if (ph < RETRACE_US && (double)now - ph + RETRACE_US < next) next = (double)now - ph + RETRACE_US;  /* retrace end */
        if (g_iev < g_nev && (double)g_ev[g_iev].t < next) next = (double)g_ev[g_iev].t;
        if (next <= (double)now && g_o.headless) next = (double)now + 10;   /* overdue but undeliverable */
        if (next > (double)now) {
            g_st.idle_sleeps++;
            if (g_log_idle && g_st.idle_sleeps < 60)
                fprintf(stderr, "idle at %05X: %.4f -> %.4f (tick %.4f frame %.4f) ifl=%d imr=%02X\n", at, now / 1e6, next / 1e6,
                        g_next_tick / 1e6, g_next_frame / 1e6, c->ifl, g_io.pic_imr);
            if (g_o.headless) g_virt_us = (uint64_t)next + 1;     /* land just past the event */
            else {
                double d = next - (double)now;
                if (d > 1500) {
                    uint64_t s0 = SDL_GetPerformanceCounter();
                    SDL_Delay((uint32_t)((d - 500) / 1000));
                    g_t_sleep_ms += (SDL_GetPerformanceCounter() - s0) * 1000.0 / g_pcf;
                }
            }
        }
    } else if (g_o.headless) {
        g_virt_us += 2;                            /* busy CPU work costs a little time */
    }
    g_last_writes = g_mem_writes;
}

/* retrace is a pure function of time: frame k starts at g_t_frame0 + k * FRAME_US */
static double frame_phase(double now) { return fmod(now - g_t_frame0, FRAME_US); }
static void status_read(Vga *v)
{
    v->in_vretrace = frame_phase((double)platform_now_us()) < RETRACE_US;
}

/* ------------------------------------------------------------- set-up */
int platform_init(const PlatformOpts *o)
{
    g_o = *o;
    if (g_o.scale <= 0) g_o.scale = 3;
    g_pcf = SDL_GetPerformanceFrequency();
    g_pc0 = SDL_GetPerformanceCounter();
    parse_events();
    g_log_idle = getenv("FOX_IDLE_LOG") != NULL;
    g_log_rate = getenv("FOX_RATE_LOG") != NULL;
    if (g_o.headless) {
        if (g_o.wav) { g_wav = fopen(g_o.wav, "wb"); if (g_wav) wav_header(g_wav, 0); }
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1;
    }
    int log_h = (!g_o.aspect_4_3 && g_o.scale > 0) ? 200 : 240;
    uint32_t win_flags = SDL_WINDOW_RESIZABLE;
    if (g_o.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    g_win = SDL_CreateWindow("Fox Ranger  -  arrows move, D fire, C bomb, Space/Enter select, Esc menu",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             320 * g_o.scale, log_h * g_o.scale, win_flags);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_win || !g_ren) { fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return -1; }
    SDL_RenderSetLogicalSize(g_ren, 320, log_h);
    SDL_DisplayMode dm;
    if (!SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(g_win), &dm) && dm.refresh_rate > 0)
        g_present_us = 1e6 / (dm.refresh_rate < 70 ? dm.refresh_rate : 70);
    SDL_RaiseWindow(g_win);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    return 0;
}

void platform_install(void)
{
    /* IVT: everything -> dummy iret, timer/keyboard -> BIOS stand-ins */
    for (int v = 0; v < 256; v++) { wr16(0, (uint16_t)(v * 4), 0xFF53); wr16(0, (uint16_t)(v * 4 + 2), BIOS_SEG); }
    wr16(0, 8 * 4, 0xFEA5); wr16(0, 9 * 4, 0xE987);
    far_register_abs(BIOS_SEG, 0xFEA5, bios_int08, "bios_int08", 0);
    far_register_abs(BIOS_SEG, 0xE987, bios_int09, "bios_int09", 0);
    far_register_abs(BIOS_SEG, 0xFF53, bios_dummy, "bios_iret", 0);
    rt_poll_fn = poll;
    g_vga.on_status_read = status_read;
    uint64_t now = platform_now_us();
    g_next_tick = (double)now + tick_us();
    g_next_frame = (double)now + FRAME_US;
    g_t_frame0 = (double)now;
}

void platform_set_exit(void (*fn)(int)) { g_exit_fn = fn; }

void platform_shutdown(void)
{
    if (g_wav) { wav_header(g_wav, (uint32_t)g_wav_frames); fclose(g_wav); g_wav = NULL; }
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_tex = NULL; g_ren = NULL; g_win = NULL;
}

PlatformStats platform_stats(void)
{
    if (g_o.verbose && !g_o.headless)
        fprintf(stderr, "host time: present %.0f ms, idle sleep %.0f ms\n", g_t_present_ms, g_t_sleep_ms);
    return g_st;
}
