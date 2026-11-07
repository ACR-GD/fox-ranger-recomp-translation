#include "io.h"
#include "vga_modex.h"
#include "audio.h"
#include <stdio.h>

IoState g_io;

static int is_vga(uint16_t p) { return p >= 0x3B0 && p <= 0x3DF; }

double pit_ch0_hz(void)
{
    uint32_t d = g_io.pit_divisor[0] ? g_io.pit_divisor[0] : 65536u;
    return 1193181.667 / d;
}

void port_out8(uint16_t port, uint8_t v)
{
    if (g_io.trace) g_io.trace(1, port, v);
    if (is_vga(port)) { vga_port_out8(&g_vga, port, v); return; }
    switch (port) {
    case 0x20: return;                                   /* EOI */
    case 0x61: g_io.port61 = v; return;                  /* speaker gate / kbd ack */
    case 0x21: g_io.pic_imr = v; return;
    case 0x43: {                                         /* PIT control word */
        int ch = v >> 6;
        if (ch < 3) { g_io.pit_mode[ch] = v; g_io.pit_lohi[ch] = 0; }
        return; }
    case 0x40: case 0x41: case 0x42: {
        int ch = port - 0x40;
        if (g_io.pit_lohi[ch] == 0) { g_io.pit_divisor[ch] = (uint16_t)((g_io.pit_divisor[ch] & 0xFF00) | v); g_io.pit_lohi[ch] = 1; }
        else { g_io.pit_divisor[ch] = (uint16_t)((g_io.pit_divisor[ch] & 0x00FF) | (v << 8)); g_io.pit_lohi[ch] = 0; }
        return; }
    /* OPL2: AdLib ports (driver base in [DS:218C]) and the SB FM mirror */
    case 0x388: case 0x228: g_io.opl_index = v; return;
    case 0x389: case 0x229: audio_opl_write(g_io.opl_index, v); return;
    case 0x38A: g_io.opl_index = (uint16_t)(0x100 | v); return;      /* OPL3 bank 1 */
    case 0x38B: audio_opl_write(g_io.opl_index, v); return;
    default:
        /* SB DSP (22xh) and DMA are replaced at the CT-VOICE driver level,
         * not emulated at port level - see RECOMP_PLAN.md */
        g_io.unhandled++;
        return;
    }
}

void port_out16(uint16_t port, uint16_t v)
{
    if (is_vga(port)) {
        if (g_io.trace) { g_io.trace(1, port, (uint8_t)v); g_io.trace(1, (uint16_t)(port + 1), (uint8_t)(v >> 8)); }
        vga_port_out16(&g_vga, port, v);
        return;
    }
    port_out8(port, (uint8_t)v);
    port_out8((uint16_t)(port + 1), (uint8_t)(v >> 8));
}

static uint8_t in8(uint16_t port)
{
    if (is_vga(port)) return vga_port_in8(&g_vga, port);
    switch (port) {
    case 0x21: return g_io.pic_imr;
    case 0x60: return g_io.kbd_data;
    case 0x61: return g_io.port61;
    case 0x64: return 0x14;                              /* KBC status: input buffer empty */
    case 0x388: case 0x228: return audio_opl_status();
    default: g_io.unhandled++; return 0xFF;
    }
}

uint8_t port_in8(uint16_t port)
{
    uint8_t v = in8(port);
    if (g_io.trace) g_io.trace(0, port, v);
    return v;
}

uint16_t port_in16(uint16_t port)
{
    return (uint16_t)(port_in8(port) | (port_in8((uint16_t)(port + 1)) << 8));
}
