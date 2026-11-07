#include "vga_modex.h"
#include "mem.h"
#include <string.h>

Vga g_vga;

/* mem.h window hooks route to the global adapter */
uint8_t vga_mem_read(uint16_t off)           { return vga_read(&g_vga, off); }
void    vga_mem_write(uint16_t off, uint8_t v) { vga_write(&g_vga, off, v); }

static int chain4(const Vga *v) { return (v->seq[4] & 0x08) != 0; }

void vga_reset(Vga *v)
{
    void (*hook)(struct Vga *) = v->on_status_read;
    memset(v, 0, sizeof *v);
    v->on_status_read = hook;
    v->dac_mask = 0xFF;
    vga_set_mode13(v);
}

void vga_set_mode13(Vga *v)
{
    static const uint8_t crtc13[0x19] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF };
    memcpy(v->crtc, crtc13, sizeof crtc13);
    v->misc_out = 0x63;
    v->seq[0] = 0x03; v->seq[1] = 0x01; v->seq[2] = 0x0F; v->seq[3] = 0x00; v->seq[4] = 0x0E;
    memset(v->gc, 0, sizeof v->gc);
    v->gc[5] = 0x40; v->gc[6] = 0x05; v->gc[7] = 0x0F; v->gc[8] = 0xFF;
    for (int i = 0; i < 16; i++) v->attr[i] = (uint8_t)i;
    v->attr[0x10] = 0x41; v->attr[0x12] = 0x0F;
    memset(v->vram, 0, sizeof v->vram);   /* mode set clears memory */
}

void vga_fox_set_mode_y(Vga *v)
{
    vga_set_mode13(v);
    vga_port_out8(v, 0x3C4, 4); vga_port_out8(v, 0x3C5, (uint8_t)((vga_port_in8(v, 0x3C5) & 0xF7) | 0x04));
    vga_port_out8(v, 0x3CE, 5); vga_port_out8(v, 0x3CF, (uint8_t)(vga_port_in8(v, 0x3CF) & 0xEF));
    vga_port_out8(v, 0x3CE, 6); vga_port_out8(v, 0x3CF, (uint8_t)(vga_port_in8(v, 0x3CF) & 0xFD));
    vga_port_out16(v, 0x3C4, 0x0F02);
    for (uint32_t a = 0; a < 0x10000; a += 2) { vga_write(v, (uint16_t)a, 0); vga_write(v, (uint16_t)(a + 1), 0); }
    vga_port_out8(v, 0x3D4, 0x14); vga_port_out8(v, 0x3D5, (uint8_t)(vga_port_in8(v, 0x3D5) & 0xBF));
    vga_port_out8(v, 0x3D4, 0x17); vga_port_out8(v, 0x3D5, (uint8_t)(vga_port_in8(v, 0x3D5) | 0x40));
}

/* ---------------------------------------------------------------- memory */
/* Addressing model: in chain-4 a CPU byte address a selects plane (a & 3) at
 * plane offset (a >> 2). This is the DOSBox-style "linear" model; it keeps
 * mode 13h and the unchained view consistent. (Real hardware keeps the low
 * address bits, but Fox Ranger clears VRAM after unchaining, so the
 * difference is never visible.) Unchained: plane offset = a, planes by map mask. */

static uint8_t alu(const Vga *v, int p, uint8_t d)
{
    switch ((v->gc[3] >> 3) & 3) {
    case 1: return d & v->latch[p];
    case 2: return d | v->latch[p];
    case 3: return d ^ v->latch[p];
    default: return d;
    }
}

void vga_write(Vga *v, uint16_t off, uint8_t val)
{
    uint8_t mask = v->seq[2] & 0x0F;
    uint16_t a = off;
    if (chain4(v)) { mask &= (uint8_t)(1u << (off & 3)); a = (uint16_t)(off >> 2); }

    int mode = v->gc[5] & 3;
    uint8_t bm = v->gc[8];
    int rot = v->gc[3] & 7;
    uint8_t rv = (uint8_t)((val >> rot) | (val << ((8 - rot) & 7)));

    for (int p = 0; p < 4; p++) {
        if (!(mask & (1u << p))) continue;
        uint8_t d;
        switch (mode) {
        case 0:
            d = (v->gc[1] & (1u << p)) ? ((v->gc[0] >> p) & 1 ? 0xFF : 0x00) : rv;
            d = alu(v, p, d);
            d = (uint8_t)((d & bm) | (v->latch[p] & ~bm));
            break;
        case 1:
            d = v->latch[p];
            break;
        case 2:
            d = (val >> p) & 1 ? 0xFF : 0x00;
            d = alu(v, p, d);
            d = (uint8_t)((d & bm) | (v->latch[p] & ~bm));
            break;
        default: { /* 3 */
            uint8_t m = rv & bm;
            d = (v->gc[0] >> p) & 1 ? 0xFF : 0x00;
            d = (uint8_t)((d & m) | (v->latch[p] & ~m));
            break; }
        }
        v->vram[p][a] = d;
    }
}

uint8_t vga_read(Vga *v, uint16_t off)
{
    uint16_t a = chain4(v) ? (uint16_t)(off >> 2) : off;
    for (int p = 0; p < 4; p++) v->latch[p] = v->vram[p][a];
    if (chain4(v)) return v->latch[off & 3];
    if (!(v->gc[5] & 0x08))                       /* read mode 0 */
        return v->latch[v->gc[4] & 3];
    uint8_t r = 0xFF;                            /* read mode 1: colour compare */
    for (int p = 0; p < 4; p++) {
        if (!(v->gc[7] & (1u << p))) continue;
        uint8_t want = (v->gc[2] >> p) & 1 ? 0xFF : 0x00;
        r &= (uint8_t)~(v->latch[p] ^ want);
    }
    return r;
}

/* ------------------------------------------------------------------ ports */
void vga_port_out8(Vga *v, uint16_t port, uint8_t val)
{
    switch (port) {
    case 0x3C0:
        if (!v->attr_flip) v->attr_idx = val & 0x3F;
        else if ((v->attr_idx & 0x1F) < sizeof v->attr) v->attr[v->attr_idx & 0x1F] = val;
        v->attr_flip ^= 1;
        break;
    case 0x3C2: v->misc_out = val; break;
    case 0x3C4: v->seq_idx = val & 7; break;
    case 0x3C5: if (v->seq_idx < sizeof v->seq) v->seq[v->seq_idx] = val; break;
    case 0x3C6: v->dac_mask = val; break;
    case 0x3C7: v->dac_ridx = val; v->dac_rcomp = 0; break;
    case 0x3C8: v->dac_widx = val; v->dac_wcomp = 0; break;
    case 0x3C9:
        v->dac_wtmp[v->dac_wcomp++] = val & 0x3F;
        if (v->dac_wcomp == 3) {
            memcpy(v->dac[v->dac_widx], v->dac_wtmp, 3);
            v->dac_widx++; v->dac_wcomp = 0;
        }
        break;
    case 0x3CE: v->gc_idx = val & 0x0F; break;
    case 0x3CF: if (v->gc_idx < sizeof v->gc) v->gc[v->gc_idx] = val; break;
    case 0x3B4: case 0x3D4: v->crtc_idx = val & 0x1F; break;
    case 0x3B5: case 0x3D5:
        if (v->crtc_idx < sizeof v->crtc) {
            /* CR11 bit7 write-protects CR00-CR07 (except CR07 bit4) */
            if ((v->crtc[0x11] & 0x80) && v->crtc_idx <= 7) {
                if (v->crtc_idx == 7) v->crtc[7] = (uint8_t)((v->crtc[7] & ~0x10) | (val & 0x10));
            } else v->crtc[v->crtc_idx] = val;
        }
        break;
    default: break;
    }
}

void vga_port_out16(Vga *v, uint16_t port, uint16_t val)
{
    vga_port_out8(v, port, (uint8_t)val);
    vga_port_out8(v, (uint16_t)(port + 1), (uint8_t)(val >> 8));
}

uint8_t vga_port_in8(Vga *v, uint16_t port)
{
    switch (port) {
    case 0x3C0: return v->attr_idx;
    case 0x3C1: return (v->attr_idx & 0x1F) < sizeof v->attr ? v->attr[v->attr_idx & 0x1F] : 0;
    case 0x3C4: return v->seq_idx;
    case 0x3C5: return v->seq_idx < sizeof v->seq ? v->seq[v->seq_idx] : 0xFF;
    case 0x3C6: return v->dac_mask;
    case 0x3C7: return 0;                        /* DAC state: write mode */
    case 0x3C8: return v->dac_widx;
    case 0x3C9: {
        uint8_t r = v->dac[v->dac_ridx][v->dac_rcomp++];
        if (v->dac_rcomp == 3) { v->dac_rcomp = 0; v->dac_ridx++; }
        return r; }
    case 0x3CC: return v->misc_out;
    case 0x3CE: return v->gc_idx;
    case 0x3CF: return v->gc_idx < sizeof v->gc ? v->gc[v->gc_idx] : 0xFF;
    case 0x3B4: case 0x3D4: return v->crtc_idx;
    case 0x3B5: case 0x3D5: return v->crtc_idx < sizeof v->crtc ? v->crtc[v->crtc_idx] : 0xFF;
    case 0x3BA: case 0x3DA:
        if (v->on_status_read) v->on_status_read(v);
        v->attr_flip = 0;
        return v->in_vretrace ? 0x09 : 0x00;     /* bit3 vretrace, bit0 display disabled */
    default: return 0xFF;
    }
}

/* --------------------------------------------------------------- display */
int vga_width(const Vga *v) { return (v->crtc[1] + 1) * 4; }

int vga_height(const Vga *v)
{
    int vde = v->crtc[0x12] | ((v->crtc[7] & 0x02) << 7) | ((v->crtc[7] & 0x40) << 3);
    int scan = (v->crtc[9] & 0x1F) + 1;
    if (v->crtc[9] & 0x80) scan *= 2;
    int h = (vde + 1) / scan;
    return h > 0 && h <= 480 ? h : 200;
}

uint16_t vga_start_addr(const Vga *v) { return (uint16_t)((v->crtc[0x0C] << 8) | v->crtc[0x0D]); }

void vga_render_indexed(const Vga *v, uint8_t *dst, int pitch)
{
    int w = vga_width(v), h = vga_height(v);
    uint16_t start = vga_start_addr(v);
    uint16_t stride = (uint16_t)(v->crtc[0x13] * 2);     /* plane bytes per line (byte mode) */
    for (int y = 0; y < h; y++) {
        uint16_t line = (uint16_t)(start + y * stride);
        for (int x = 0; x < w; x++)
            dst[y * pitch + x] = v->vram[x & 3][(uint16_t)(line + (x >> 2))] & v->dac_mask;
    }
}

void vga_render_argb(const Vga *v, uint32_t *dst, int pitch_px)
{
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = v->dac[i][0], g = v->dac[i][1], b = v->dac[i][2];
        r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
        pal[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    int w = vga_width(v), h = vga_height(v);
    uint16_t start = vga_start_addr(v);
    uint16_t stride = (uint16_t)(v->crtc[0x13] * 2);
    for (int y = 0; y < h; y++) {
        uint16_t line = (uint16_t)(start + y * stride);
        for (int x = 0; x < w; x++)
            dst[y * pitch_px + x] = pal[v->vram[x & 3][(uint16_t)(line + (x >> 2))] & v->dac_mask];
    }
}
