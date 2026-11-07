/* vga_modex.h - planar VGA emulation for unchained 256-colour modes
 * (Mode Y 320x200 as used by Fox Ranger, Mode X 320x240, and plain mode 13h).
 *
 * Fox Ranger's video set-up (img 04AE2): INT 10h mode 13h, then
 *   SEQ 04h &= ~08 (chain-4 off) | 04, GC 05h &= ~10, GC 06h &= ~02,
 *   map mask 0Fh, clear A000:0000-FFFF, CRTC 14h &= ~40 (dword off),
 *   CRTC 17h |= 40 (byte mode)                      -> 4 planes x 64 KB.
 * Pages are VRAM segments A000 / A400 / A800 (plane offsets 0000/4000/8000),
 * shown via CRTC 0Ch (10A7:070C). Write mode 1 latch copies are used by the
 * strip scroller 10A7:0721, so latches and all four write modes are modelled.
 */
#ifndef FOX_VGA_MODEX_H
#define FOX_VGA_MODEX_H

#include <stdint.h>

#define VGA_PLANE_SIZE 0x10000u

typedef struct Vga {
    uint8_t  vram[4][VGA_PLANE_SIZE];
    uint8_t  latch[4];

    uint8_t  misc_out;
    uint8_t  seq_idx, seq[5];        /* 02 map mask, 04 memory mode (bit3 chain-4)        */
    uint8_t  gc_idx,  gc[9];         /* 00 set/reset 01 enable s/r 02 colour compare
                                        03 rotate/func 04 read map 05 mode 07 dont-care 08 bitmask */
    uint8_t  crtc_idx, crtc[0x19];   /* 01 hdisp end 07 overflow 09 max scan 0C/0D start
                                        12 vdisp end 13 offset 14 underline 17 mode ctrl  */
    uint8_t  attr_idx, attr_flip, attr[0x15];

    uint8_t  dac_mask, dac_widx, dac_ridx, dac_wcomp, dac_rcomp;
    uint8_t  dac[256][3];            /* 6-bit components */
    uint8_t  dac_wtmp[3];

    uint8_t  in_vretrace;            /* set by the platform layer                          */
    void   (*on_status_read)(struct Vga *);   /* 3DAh poll hook: pump events / pace frames */
} Vga;

extern Vga g_vga;

void    vga_reset(Vga *v);
void    vga_set_mode13(Vga *v);           /* INT 10h AX=0013h */
/* native replacement for FOX.EXE 04A4:00A2 (img 04AE2): mode 13h, unchain to
 * Mode Y (chain-4 off, odd/even off, byte mode), map mask 0Fh, clear 64K/plane */
void    vga_fox_set_mode_y(Vga *v);

void    vga_port_out8(Vga *v, uint16_t port, uint8_t val);
void    vga_port_out16(Vga *v, uint16_t port, uint16_t val);   /* index + data pair */
uint8_t vga_port_in8(Vga *v, uint16_t port);

/* CPU access to the A000:0000-FFFF window (offset within the window) */
void    vga_write(Vga *v, uint16_t off, uint8_t val);
uint8_t vga_read(Vga *v, uint16_t off);

/* display geometry derived from the CRTC (200 for Mode Y / 13h, 240 for Mode X) */
int     vga_width(const Vga *v);
int     vga_height(const Vga *v);
uint16_t vga_start_addr(const Vga *v);

/* scan out the visible frame: 8-bit indices or 32-bit ARGB8888 */
void    vga_render_indexed(const Vga *v, uint8_t *dst, int pitch);
void    vga_render_argb(const Vga *v, uint32_t *dst, int pitch_px);

#endif
