/* mem.h - simulated real-mode address space for the Fox Ranger recompilation.
 *
 * The original program keeps all of its data in the 1 MB real-mode space
 * (DGROUP 1ADF, far heaps, data inside code segments such as 10A7:000A..002C,
 * VGA memory at A000). The recompiled C code therefore keeps the *same*
 * segment:offset layout: the unpacked EXE image is loaded into ram[] at
 * g_load_seg exactly like DOS would (see mz_load.c), and every recompiled
 * memory access goes through the helpers below.
 *
 *  - REAL_PTR() gives a raw host pointer. It bypasses the VGA window and must
 *    only be used for ordinary RAM (DGROUP, heap, sprite buffers).
 *  - mem_rd* / mem_wr* route 0xA0000..0xAFFFF to the planar VGA emulation.
 */
#ifndef FOX_MEM_H
#define FOX_MEM_H

#include <stdint.h>
#include <string.h>

#define MEM_SIZE      0x100000u            /* 1 MB conventional + UMA            */
#define MEM_SLACK     0x10000u             /* seg:off may reach FFFF:FFFF = 10FFEF */
#define VGA_WIN_BASE  0xA0000u
#define VGA_WIN_SIZE  0x10000u

extern uint8_t  ram[MEM_SIZE + MEM_SLACK];
extern uint32_t g_mem_writes;              /* bumped per CPU write: idle (busy-wait) detection */
extern uint16_t g_load_seg;                /* segment the image was loaded at     */

/* linear address of seg:off (16-bit offset wrap like the CPU) */
#define LIN(seg, off)      ((((uint32_t)(uint16_t)(seg)) << 4) + (uint16_t)(off))
#define REAL_PTR(seg, off) ((void *)&ram[LIN(seg, off)])
/* image-relative segment (as written in the disassembly) -> loaded segment */
#define SEG(s)             ((uint16_t)((s) + g_load_seg))

/* VGA memory window hooks (vga_modex.c) */
uint8_t vga_mem_read(uint16_t off);
void    vga_mem_write(uint16_t off, uint8_t v);

static inline int mem_is_vga(uint32_t a) { return a - VGA_WIN_BASE < VGA_WIN_SIZE; }

static inline uint8_t mem_rd8(uint32_t a)
{
    return mem_is_vga(a) ? vga_mem_read((uint16_t)(a - VGA_WIN_BASE)) : ram[a];
}
static inline void mem_wr8(uint32_t a, uint8_t v)
{
    g_mem_writes++;
    if (mem_is_vga(a)) vga_mem_write((uint16_t)(a - VGA_WIN_BASE), v);
    else if (a < 0xC0000u || a >= MEM_SIZE) ram[a] = v;   /* C0000-FFFFF: ROM, ignore */
}
static inline uint16_t mem_rd16(uint32_t a) { return (uint16_t)(mem_rd8(a) | (mem_rd8(a + 1) << 8)); }
static inline void mem_wr16(uint32_t a, uint16_t v) { mem_wr8(a, (uint8_t)v); mem_wr8(a + 1, (uint8_t)(v >> 8)); }

/* segment-relative forms; the 16-bit offset wraps inside the segment */
static inline uint8_t  rd8 (uint16_t s, uint16_t o)             { return mem_rd8(LIN(s, o)); }
static inline uint16_t rd16(uint16_t s, uint16_t o)             { return (uint16_t)(rd8(s, o) | (rd8(s, (uint16_t)(o + 1)) << 8)); }
static inline void     wr8 (uint16_t s, uint16_t o, uint8_t v)  { mem_wr8(LIN(s, o), v); }
static inline void     wr16(uint16_t s, uint16_t o, uint16_t v) { wr8(s, o, (uint8_t)v); wr8(s, (uint16_t)(o + 1), (uint8_t)(v >> 8)); }

/* far pointers are stored little-endian as off:seg */
typedef struct { uint16_t off, seg; } FarPtr;
static inline FarPtr rdfar(uint16_t s, uint16_t o) { FarPtr p = { rd16(s, o), rd16(s, (uint16_t)(o + 2)) }; return p; }

void mem_reset(void);

#endif
