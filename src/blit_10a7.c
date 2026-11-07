/* blit_10a7.c - native replacement for FOX.EXE segment 10A7.
 * Semantics follow the original instructions exactly (16-bit wrap-around,
 * signed/unsigned compares as in the asm, `loop` counts of 0 = 65536,
 * per-pixel map-mask writes) so it can be checked byte-for-byte against the
 * x86 code: tests/diff_blit.py runs both and compares all four planes. */
#include "blit_10a7.h"
#include "io.h"

#define ST(off)   LIN(SEG(BLIT_SEG), (off))
#define PAGE()    ((uint16_t)(ram[ST(0x0A)] | (ram[ST(0x0B)] << 8)))
#define CLIP(off) ((int16_t)(ram[ST(off)] | (ram[ST((off) + 1)] << 8)))
#define EFFECT()  (ram[ST(0x14)])

static inline uint8_t rol8(uint8_t v, unsigned n) { n &= 7; return (uint8_t)((v << n) | (v >> ((8 - n) & 7))); }
static inline void map_mask(uint8_t m) { port_out8(0x3C5, m); }
static inline void vput(uint16_t page, uint16_t di, uint8_t v) { wr8(page, di, v); }

/* step the plane mask like `rol al,1; cmp al,11h; jne; inc di` */
#define NEXT_PIXEL(al, di) do { (al) = rol8((al), 1); if ((al) == 0x11) (di)++; } while (0)

typedef struct { uint16_t x0, y0, L, R, T, B; } Clip;

/* shared centred clip of 002D / 01A7 */
static Clip clip_centred(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    Clip c = { 0 };
    c.x0 = (uint16_t)(x - (w >> 1));
    if ((int16_t)c.x0 < CLIP(0x0C)) c.L = (uint16_t)(CLIP(0x0C) - c.x0);
    uint16_t ax = (uint16_t)(c.x0 + w);
    if ((int16_t)ax > CLIP(0x0E)) c.R = (uint16_t)(ax - CLIP(0x0E));
    c.y0 = (uint16_t)(y - (h >> 1));
    if ((int16_t)c.y0 < CLIP(0x10)) c.T = (uint16_t)(CLIP(0x10) - c.y0);
    ax = (uint16_t)(c.y0 + h);
    if ((int16_t)ax > CLIP(0x12)) c.B = (uint16_t)(ax - CLIP(0x12));
    return c;
}

static void sprite_centred(uint16_t x, uint16_t y, FarPtr desc, int mono)
{
    uint16_t page = PAGE();
    uint16_t w = rd16(desc.seg, desc.off), h = rd16(desc.seg, (uint16_t)(desc.off + 2));
    Clip c = clip_centred(x, y, w, h);
    uint16_t row = (uint16_t)((uint16_t)(0x50 * (uint16_t)(c.y0 + c.T)) + ((uint16_t)(c.x0 + c.L) >> 2));
    uint16_t cols = (uint16_t)(w - c.L - c.R);
    if ((int16_t)cols <= 0) return;
    uint16_t rows = (uint16_t)(h - c.T - c.B);
    if ((int16_t)rows <= 0) return;
    FarPtr pix = rdfar(desc.seg, (uint16_t)(desc.off + 7));
    uint16_t si = (uint16_t)(pix.off + (uint16_t)(c.T * w) + c.L);
    uint16_t skip = (uint16_t)(c.R + c.L);

    port_out8(0x3C4, 2);
    uint8_t mask = rol8(0x11, (uint16_t)(c.x0 + c.L) & 3);
    uint8_t eff = EFFECT();
    uint16_t di = row;
    do {                                   /* rows > 0 here */
        uint8_t al = mask;
        uint16_t n = cols;
        do {
            uint8_t ah = rd8(pix.seg, si);
            if (ah) {
                if (mono) ah = eff;
                else if (eff) ah = (eff == 0x64) ? 1 : (uint8_t)(ah & 0x44);
                map_mask(al);
                vput(page, di, ah);
            }
            si++;
            NEXT_PIXEL(al, di);
        } while (--n);
        si = (uint16_t)(si + skip);
        row = (uint16_t)(row + 0x50);
        di = row;
    } while (--rows);
}

void blit_sprite(uint16_t x, uint16_t y, FarPtr d)      { sprite_centred(x, y, d, 0); }
void blit_sprite_mono(uint16_t x, uint16_t y, FarPtr d) { sprite_centred(x, y, d, 1); }

void blit_sprite_opaque(uint16_t x, uint16_t y, FarPtr desc)
{
    uint16_t page = PAGE();
    uint16_t row = (uint16_t)((uint16_t)(0x50 * y) + (x >> 2));
    uint16_t w = rd16(desc.seg, desc.off), h = rd16(desc.seg, (uint16_t)(desc.off + 2));
    FarPtr pix = rdfar(desc.seg, (uint16_t)(desc.off + 7));
    uint16_t si = pix.off, di = row;
    port_out8(0x3C4, 2);
    uint8_t mask = rol8(0x11, x & 3);
    uint16_t rows = h;
    do {
        uint8_t al = mask;
        uint16_t n = w;
        do {
            map_mask(al);
            vput(page, di, rd8(pix.seg, si));
            si++;
            NEXT_PIXEL(al, di);
        } while (--n);
        row = (uint16_t)(row + 0x50);
        di = row;
    } while (--rows);
}

/* vertical clip shared by 0390 / 0466; returns 0 if the glyph is skipped */
static int clip_glyph16(uint16_t y, uint16_t *T, uint16_t *B, int signed_b)
{
    *T = *B = 0;
    uint16_t cy0 = (uint16_t)CLIP(0x10), cy1 = (uint16_t)CLIP(0x12);
    if (cy0 >= y) {                                  /* sub ax,[10]-y ; jb skip */
        *T = (uint16_t)(cy0 - y);
        if (*T >= 0x10) return 0;                    /* cmp ax,10h ; jb ok */
    }
    uint16_t ax = (uint16_t)(y + 0x10);
    if (ax >= cy1) {                                 /* sub ax,[12] ; jb skip */
        *B = (uint16_t)(ax - cy1);
        /* 0390: cmp ax,10h ; jl ok (signed)   0466: cmp ax,10h ; jb ok (unsigned) */
        if (signed_b ? (int16_t)*B >= 0x10 : *B >= 0x10) return 0;
    }
    return 1;
}

void blit_glyph16(uint16_t x, uint16_t y, uint16_t colour, FarPtr g)
{
    uint16_t page = PAGE(), T, B;
    if (!clip_glyph16(y, &T, &B, 1)) return;
    uint16_t row = (uint16_t)((uint16_t)(0x50 * (uint16_t)(y + T)) + (x >> 2)), di = row;
    uint16_t si = (uint16_t)(g.off + (uint16_t)(T << 1));
    port_out8(0x3C4, 2);
    uint8_t mask = rol8(0x11, x & 3), bl = (uint8_t)colour;
    uint16_t rows = (uint16_t)(0x10 - T - B);
    do {
        uint8_t al = mask, bh = rd8(g.seg, si), ah = rd8(g.seg, (uint16_t)(si + 1));
        uint16_t n = 8;
        do {
            map_mask(al);
            if (bh & 0x80) vput(page, di, bl);
            bh = (uint8_t)(bh << 1);
            if (ah & 0x80) vput(page, (uint16_t)(di + 2), bl);
            ah = (uint8_t)(ah << 1);
            NEXT_PIXEL(al, di);
        } while (--n);
        row = (uint16_t)(row + 0x50);
        di = row;
        si = (uint16_t)(si + 2);
    } while (--rows);
}

static void glyph8(uint16_t page, uint16_t row, uint16_t x, uint16_t colour, FarPtr g, uint16_t si, uint16_t rows)
{
    port_out8(0x3C4, 2);
    uint8_t mask = rol8(0x11, x & 3), bl = (uint8_t)colour;
    int opaque = (colour >> 8) != 0;
    uint16_t di = row;
    do {
        uint8_t al = mask, bh = rd8(g.seg, si);
        uint16_t n = 8;
        do {
            map_mask(al);
            if (bh & 0x80) vput(page, di, bl);
            else if (opaque) vput(page, di, 0);
            bh = (uint8_t)(bh << 1);
            NEXT_PIXEL(al, di);
        } while (--n);
        row = (uint16_t)(row + 0x50);
        di = row;
        si++;
    } while (--rows);
}

void blit_glyph8x16(uint16_t x, uint16_t y, uint16_t colour, FarPtr g)
{
    uint16_t T, B;
    if (!clip_glyph16(y, &T, &B, 0)) return;
    uint16_t row = (uint16_t)((uint16_t)(0x50 * (uint16_t)(y + T)) + (x >> 2));
    glyph8(PAGE(), row, x, colour, g, (uint16_t)(g.off + T), (uint16_t)(0x10 - T - B));
}

void blit_glyph8x8(uint16_t x, uint16_t y, uint16_t colour, FarPtr g)
{
    uint16_t row = (uint16_t)((uint16_t)(0x50 * y) + (x >> 2));
    glyph8(PAGE(), row, x, colour, g, g.off, 8);
}

void blit_sprite_shrunk(uint16_t x, uint16_t y, uint16_t div16, FarPtr desc)
{
    uint8_t div = (uint8_t)div16;
    uint16_t page = PAGE();
    uint16_t w = rd16(desc.seg, desc.off), h = rd16(desc.seg, (uint16_t)(desc.off + 2));
    if (!div || w / div > 0xFF || h / div > 0xFF) return;   /* original: #DE divide fault */
    uint16_t cols = (uint8_t)(w / div);                      /* div r8 -> AL */
    if (!cols) return;
    uint16_t rows = (uint8_t)(h / div);
    if (!rows) return;
    uint16_t x0 = (uint16_t)(x - (cols >> 1)), y0 = (uint16_t)(y - (rows >> 1));
    uint16_t row = (uint16_t)((uint16_t)(0x50 * y0) + (x0 >> 2)), di = row;
    uint16_t stride = (uint16_t)((uint8_t)w * div);          /* mul r8 uses AL = low byte of w */
    FarPtr pix = rdfar(desc.seg, (uint16_t)(desc.off + 7));
    uint16_t line = pix.off, si = line;
    port_out8(0x3C4, 2);
    uint8_t mask = rol8(0x11, x0 & 3);
    do {
        uint8_t al = mask;
        uint16_t n = cols;
        do {
            uint8_t ah = rd8(pix.seg, si);
            if (ah) { map_mask(al); vput(page, di, ah); }
            si = (uint16_t)(si + div);
            NEXT_PIXEL(al, di);
        } while (--n);
        row = (uint16_t)(row + 0x50);
        di = row;
        line = (uint16_t)(line + stride);
        si = line;
    } while (--rows);
}

void blit_show_page(void)
{
    uint16_t crtc = (uint16_t)(ram[0x463] | (ram[0x464] << 8));
    uint8_t hi = (uint8_t)((PAGE() >> 8) << 4);
    port_out16(crtc, (uint16_t)(0x0C | (hi << 8)));
}

static void movsb_rows(uint16_t ds, uint16_t *si, uint16_t es, uint16_t *di, uint16_t n)
{
    while (n--) { wr8(es, *di, rd8(ds, *si)); (*si)++; (*di)++; }
}

void blit_scroll_strip(uint16_t row, uint16_t rows, uint16_t width, uint16_t buf)
{
    port_out16(0x3C4, 0x0F02);                     /* map mask = all planes */
    port_out8(0x3CE, 5);                           /* GC mode: write mode 1 */
    port_out8(0x3CF, (uint8_t)((port_in8(0x3CF) & 0xFC) | 1));
    uint16_t page = PAGE();
    uint16_t base = (uint16_t)(0x50 * row), bx = (uint16_t)(0x50 - width);
    uint16_t si = base, di = (uint16_t)(base + bx), right = di;
    uint16_t n = rows;
    do {                                           /* 1: buf left strip -> page right edge */
        movsb_rows(buf, &si, page, &di, width);
        di = (uint16_t)(di + bx); si = (uint16_t)(si + bx);
    } while (--n);
    di = base; si = (uint16_t)(base + width);      /* 2: rotate buf left by width */
    movsb_rows(buf, &si, buf, &di, (uint16_t)(0x50 * rows));
    di = si = right; n = rows;
    do {                                           /* 3: page right edge -> buf right edge */
        movsb_rows(page, &si, buf, &di, width);
        di = (uint16_t)(di + bx); si = (uint16_t)(si + bx);
    } while (--n);
}

void blit_scale_sprite(uint16_t dw, uint16_t dh, FarPtr desc, FarPtr dst)
{
    uint16_t w = rd16(desc.seg, desc.off), h = rd16(desc.seg, (uint16_t)(desc.off + 2));
    FarPtr pix = rdfar(desc.seg, (uint16_t)(desc.off + 7));
    if (!dw || !dh) return;                        /* original: #DE divide fault */
    uint16_t di = dst.off, sy = 0;
    uint16_t outer = dh;
    do {
        uint16_t sx = 0, inner = dw;
        do {
            uint16_t si = pix.off;
            si = (uint16_t)(si + (uint16_t)((uint16_t)(((uint32_t)h * sy) / dh) * w));
            si = (uint16_t)(si + (uint16_t)(((uint32_t)w * sx) / dw));
            wr8(dst.seg, di, rd8(pix.seg, si));
            di++; sx++;
        } while (--inner);
        sy++;
    } while (--outer);
}

/* ---------------------------------------------- Cpu-ABI adapters (retf) */
#define RETF(c) ((c)->sp = (uint16_t)((c)->sp + 4))
static void a002D(Cpu *c) { blit_sprite(farg(c, 0), farg(c, 1), farg_ptr(c, 2)); RETF(c); }
static void a01A7(Cpu *c) { blit_sprite_mono(farg(c, 0), farg(c, 1), farg_ptr(c, 2)); RETF(c); }
static void a030D(Cpu *c) { blit_sprite_opaque(farg(c, 0), farg(c, 1), farg_ptr(c, 2)); RETF(c); }
static void a0390(Cpu *c) { blit_glyph16(farg(c, 0), farg(c, 1), farg(c, 2), farg_ptr(c, 3)); RETF(c); }
static void a0466(Cpu *c) { blit_glyph8x16(farg(c, 0), farg(c, 1), farg(c, 2), farg_ptr(c, 3)); RETF(c); }
static void a056C(Cpu *c) { blit_glyph8x8(farg(c, 0), farg(c, 1), farg(c, 2), farg_ptr(c, 3)); RETF(c); }
static void a061C(Cpu *c) { blit_sprite_shrunk(farg(c, 0), farg(c, 1), farg(c, 2), farg_ptr(c, 3)); RETF(c); }
static void a070C(Cpu *c) { blit_show_page(); RETF(c); }
static void a0721(Cpu *c) { blit_scroll_strip(farg(c, 0), farg(c, 1), farg(c, 2), farg(c, 3)); RETF(c); }
static void a07AE(Cpu *c) { blit_scale_sprite(farg(c, 0), farg(c, 1), farg_ptr(c, 2), farg_ptr(c, 4)); RETF(c); }

void blit_10a7_register(void)
{
    far_register(BLIT_SEG, 0x002D, a002D, "blit_sprite");
    far_register(BLIT_SEG, 0x01A7, a01A7, "blit_sprite_mono");
    far_register(BLIT_SEG, 0x030D, a030D, "blit_sprite_opaque");
    far_register(BLIT_SEG, 0x0390, a0390, "blit_glyph16");
    far_register(BLIT_SEG, 0x0466, a0466, "blit_glyph8x16");
    far_register(BLIT_SEG, 0x056C, a056C, "blit_glyph8x8");
    far_register(BLIT_SEG, 0x061C, a061C, "blit_sprite_shrunk");
    far_register(BLIT_SEG, 0x070C, a070C, "blit_show_page");
    far_register(BLIT_SEG, 0x0721, a0721, "blit_scroll_strip");
    far_register(BLIT_SEG, 0x07AE, a07AE, "blit_scale_sprite");
}
