/* testhooks.c - accessors so tests/diff_blit.py can drive the runtime via ctypes */
#include "../src/mem.h"
#include "../src/vga_modex.h"
#include "../src/io.h"

uint8_t *t_ram(void)          { return ram; }
uint8_t *t_vram(int plane)    { return g_vga.vram[plane & 3]; }
uint8_t  t_seq(int i)         { return g_vga.seq[i]; }
uint8_t  t_gc(int i)          { return g_vga.gc[i]; }
uint8_t  t_crtc(int i)        { return g_vga.crtc[i]; }
void     t_set_load_seg(uint16_t s) { g_load_seg = s; }

/* reproduce FOX.EXE's mode set (img 04AE2): mode 13h, then unchain */
void t_mode_y(void)
{
    vga_reset(&g_vga);
    port_out8(0x3C4, 4); port_out8(0x3C5, (uint8_t)((port_in8(0x3C5) & 0xF7) | 0x04));
    port_out8(0x3CE, 5); port_out8(0x3CF, (uint8_t)(port_in8(0x3CF) & 0xEF));
    port_out8(0x3CE, 6); port_out8(0x3CF, (uint8_t)(port_in8(0x3CF) & 0xFD));
    port_out16(0x3C4, 0x0F02);
    port_out8(0x3D4, 0x14); port_out8(0x3D5, (uint8_t)(port_in8(0x3D5) & 0xBF));
    port_out8(0x3D4, 0x17); port_out8(0x3D5, (uint8_t)(port_in8(0x3D5) | 0x40));
}
