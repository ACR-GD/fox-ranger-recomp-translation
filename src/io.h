/* io.h - x86 port I/O dispatch for recompiled `in` / `out` instructions.
 * Ports used by FOX.EXE (see RECON.md / work/triage_FOX_UNP2.txt):
 *   20h/21h PIC, 40h/43h PIT (ch0 divisor 4000h = 72.8 Hz), 3C4h-3DAh VGA,
 *   DMA 02/03/0A/0B/0C/83h (CT-VOICE), SB base [DS:10F0]+6..E,
 *   AdLib base [DS:218C] (388h/389h).
 */
#ifndef FOX_IO_H
#define FOX_IO_H
#include <stdint.h>

void     port_out8(uint16_t port, uint8_t v);
void     port_out16(uint16_t port, uint16_t v);
uint8_t  port_in8(uint16_t port);
uint16_t port_in16(uint16_t port);

/* device state the platform layer consumes */
typedef struct {
    uint16_t pit_divisor[3];   /* 0 = 65536 */
    uint8_t  pit_lohi[3], pit_mode[3];
    uint8_t  pic_imr;
    uint8_t  kbd_data;         /* port 60h: scancode of the IRQ1 being delivered */
    uint8_t  port61;           /* port 61h: speaker gate / keyboard ack bits */
    uint16_t opl_index;        /* last register selected at 388h (or SB mirror 228h) */
    uint32_t unhandled;        /* count of ports nobody claimed */
    /* optional tracer (tests): called for every OUT and IN */
    void   (*trace)(int is_out, uint16_t port, uint8_t val);
} IoState;

extern IoState g_io;
double pit_ch0_hz(void);

#endif
