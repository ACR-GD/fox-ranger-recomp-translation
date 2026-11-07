/* blit_10a7.h - native C replacement for segment 10A7 (sprite/glyph blitters).
 *
 * The original segment holds its state in a data block at the start of the
 * code segment (no instruction bytes are ever modified - "SMC" in the triage
 * is code-segment *data*). That block stays in emulated RAM at 10A7:0000 so
 * the 32+ external writers (page select, clip, effect) keep working:
 *
 *   10A7:000A u16  draw page segment (A000/A400/A800)  written by 32 sites
 *   10A7:000C i16  clip x0  (0, never changed)
 *   10A7:000E i16  clip x1  (320, never changed)
 *   10A7:0010 i16  clip y0  (0 / 28h letterbox)          3 writers
 *   10A7:0012 i16  clip y1  (C8h / A0h / variable)       5 writers
 *   10A7:0014 u8   effect: 0 normal, 64h flash -> colour 1, other -> pix & 44h
 *                  (for 01A7: colour used for every opaque pixel)  8 writers
 *   10A7:0018-002C scratch (row pointers, clip amounts, plane mask) - internal
 *
 * Sprite descriptor (11 bytes, table at 1682:2482, 208 entries):
 *   +0 u16 w, +2 u16 h, +4 u16 packed size, +6 u8 flag, +7 far ptr to w*h pixels
 *
 * All functions are far cdecl; offsets are the original entry points.
 */
#ifndef FOX_BLIT_10A7_H
#define FOX_BLIT_10A7_H
#include <stdint.h>
#include "cpu.h"

#define BLIT_SEG 0x10A7   /* image-relative */

/* 002D draw_sprite(x, y, desc far*)   centred, clipped, colour 0 transparent, effect byte */
void blit_sprite(uint16_t x, uint16_t y, FarPtr desc);
/* 01A7 draw_sprite_mono(x, y, desc)   centred, clipped, opaque pixels -> colour [14] */
void blit_sprite_mono(uint16_t x, uint16_t y, FarPtr desc);
/* 030D draw_sprite_opaque(x, y, desc) top-left, unclipped, every pixel written */
void blit_sprite_opaque(uint16_t x, uint16_t y, FarPtr desc);
/* 0390 draw_glyph16(x, y, colour, glyph far*)  16x16 1bpp, vertical clip, transparent */
void blit_glyph16(uint16_t x, uint16_t y, uint16_t colour, FarPtr glyph);
/* 0466 draw_glyph8x16(x, y, colour|opaque<<8, glyph)  vertical clip; hi byte != 0 -> bg 0 */
void blit_glyph8x16(uint16_t x, uint16_t y, uint16_t colour, FarPtr glyph);
/* 056C draw_glyph8x8(x, y, colour|opaque<<8, glyph)   unclipped */
void blit_glyph8x8(uint16_t x, uint16_t y, uint16_t colour, FarPtr glyph);
/* 061C draw_sprite_shrunk(x, y, div, desc)  centred, 1/div size, unclipped, transparent */
void blit_sprite_shrunk(uint16_t x, uint16_t y, uint16_t div, FarPtr desc);
/* 070C show_page()  CRTC 0Ch = (page_seg >> 8) << 4  (page A000/A400/A800 -> 00/40/80) */
void blit_show_page(void);
/* 0721 scroll_strip(row, rows, width_bytes, buf_seg)  write-mode-1 latch copies,
 *      rotates buf_seg rows left by width bytes and lands the strip at the page's
 *      right edge; leaves GC mode = write mode 1 and map mask = 0Fh like the original */
void blit_scroll_strip(uint16_t row, uint16_t rows, uint16_t width, uint16_t buf_seg);
/* 07AE scale_sprite(dw, dh, desc, dst far*)  nearest-neighbour into linear memory */
void blit_scale_sprite(uint16_t dw, uint16_t dh, FarPtr desc, FarPtr dst);

/* register the Cpu-ABI adapters in the far-call table */
void blit_10a7_register(void);

#endif
