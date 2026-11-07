/* platform.h - DOS-machine services for the recompiled game, driven from the
 * game thread itself (no extra threads touch emulated state):
 *
 *  - interrupts: IRQ0 (PIT) and IRQ1 (keyboard) are delivered synchronously at
 *    loop back-edges of the generated code (rt_poll_fn) and dispatched through
 *    the IVT in ram[] to the game's handlers (music multiplexer 1337:00D3 ->
 *    game timer 0E0C:016A -> BIOS; keyboard 0C6A:0139), honouring IF and the
 *    PIC mask. The PIT runs as a fixed-timestep accumulator at the currently
 *    programmed divisor (the game sets 4000h = 72.82 Hz; the music driver
 *    raises it and divides back down itself).
 *  - video: 70.086 Hz retrace model for port 3DAh, frame presented at each
 *    retrace start from the CRTC start address.
 *  - idle detection: a poll with no CPU memory writes since the previous one is
 *    a busy-wait; the platform sleeps (or, headless, jumps virtual time) to the
 *    next event instead of spinning.
 *  - headless mode: virtual clock, scripted key taps, screenshots, WAV capture
 *    -> deterministic, reproducible runs.
 */
#ifndef FOX_PLATFORM_H
#define FOX_PLATFORM_H
#include <stdint.h>
#include "cpu.h"

typedef struct {
    int         headless;      /* no window / audio device, virtual clock     */
    double      seconds;       /* stop after N seconds of (virtual) time; 0 = run until exit */
    const char *wav;           /* capture mixed audio (headless)               */
    const char *shots;         /* "T:file.ppm,T:file.ppm" (seconds)            */
    const char *keys;          /* "T:KEY,T:KEY" key taps; KEY = ENTER SPACE ESC UP DOWN LEFT RIGHT CTRL ALT A-Z 0-9 */
    int         scale;         /* window scale (320x240 logical)               */
    int         fullscreen;    /* 1 = fullscreen desktop                       */
    int         aspect_4_3;    /* 1 = 4:3 (320x240), 0 = 16:10 (320x200)      */
    int         autofire;      /* 1 = rapid autofire on hold                   */
    int         verbose;
} PlatformOpts;

int      platform_init(const PlatformOpts *o);  /* SDL video/audio (or headless) */
void     platform_install(void);                /* IVT, BIOS handlers, poll + 3DAh hooks; after mz_load */
void     platform_shutdown(void);
uint64_t platform_now_us(void);
void     platform_present(void);
/* exit path for exit()/INT 21h 4Ch/window close: longjmps to the boot code */
void     platform_set_exit(void (*fn)(int code));

typedef struct { uint64_t irq0, irq1, frames, idle_sleeps, polls; } PlatformStats;
PlatformStats platform_stats(void);

#endif
