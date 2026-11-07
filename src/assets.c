#include "assets.h"
#include "io.h"
#include "heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)*n);
    if (d && fread(d, 1, (size_t)*n, f) != (size_t)*n) { free(d); d = NULL; }
    fclose(f);
    return d;
}

static uint32_t copy_to_heap(const uint8_t *src, uint32_t n)
{
    uint16_t s = heap_alloc_paras(n);
    if (!s) return 0;
    memcpy(&ram[LIN(s, 0)], src, n);
    return LIN(s, 0);
}

int foxdat_load(const char *path, FoxDat *out)
{
    long n; uint8_t *d = slurp(path, &n);
    if (!d) return -1;
    long o = 0x300;
    memcpy(out->palette, d, 768);
    for (int k = 0; k < 4; k++) o += 1 + 2 * ((d[o] & 0x7F) * 2);   /* 4 motion records */
    out->font8x8_lin  = copy_to_heap(d + o, 0x2F8); o += 0x2F8;
    out->font8x16_lin = copy_to_heap(d + o, 0x5F0); o += 0x5F0;
    out->cho_lin  = copy_to_heap(d + o, 8 * 19 * 32); o += 8 * 19 * 32;
    out->jung_lin = copy_to_heap(d + o, 2 * 21 * 32); o += 2 * 21 * 32;
    out->jong_lin = copy_to_heap(d + o, 3 * 27 * 32); o += 3 * 27 * 32;
    free(d);
    if (o != n) { fprintf(stderr, "%s: layout mismatch (%ld of %ld)\n", path, o, n); return -1; }
    return 0;
}

int vsprite_load(const char *path, FarPtr desc, const uint8_t *wanted)
{
    long n; uint8_t *d = slurp(path, &n);
    if (!d) return -1;
    const uint8_t *dir = d + n - VSPRITE_COUNT * 6;
    long o = 0;
    for (int k = 0; k < VSPRITE_COUNT; k++) {
        uint16_t w = (uint16_t)(dir[6 * k] | dir[6 * k + 1] << 8);
        uint16_t h = (uint16_t)(dir[6 * k + 2] | dir[6 * k + 3] << 8);
        uint16_t packed = (uint16_t)(dir[6 * k + 4] | dir[6 * k + 5] << 8);
        uint32_t size = (uint32_t)w * h;
        int want = !wanted || wanted[k];
        uint16_t seg = (size && want) ? heap_alloc_paras(size) : 0;
        if (size && want && !seg) { fprintf(stderr, "vsprite: heap exhausted at %d\n", k); free(d); return -1; }
        uint8_t *dst = &ram[LIN(seg, 0)];
        if (!want || !size) o += packed ? packed : size;
        else if (!packed) { memcpy(dst, d + o, size); o += size; }
        else {                                   /* RLE: c&80 -> run of c&7F, else c literals */
            long i = o, end = o + packed; uint32_t p = 0;
            while (i < end) {
                uint8_t c = d[i++];
                if (c & 0x80) { uint8_t v = d[i++]; for (int r = 0; r < (c & 0x7F); r++) if (p < size) dst[p++] = v; }
                else { for (int r = 0; r < c; r++, i++) if (p < size) dst[p++] = d[i]; }
            }
            o = end;
        }
        uint16_t e = (uint16_t)(desc.off + SPRITE_DESC_SIZE * k);
        wr16(desc.seg, e, w); wr16(desc.seg, (uint16_t)(e + 2), h); wr16(desc.seg, (uint16_t)(e + 4), packed);
        wr8(desc.seg, (uint16_t)(e + 6), (uint8_t)(want && size));
        wr16(desc.seg, (uint16_t)(e + 7), 0); wr16(desc.seg, (uint16_t)(e + 9), seg);
    }
    free(d);
    return o == n - VSPRITE_COUNT * 6 ? 0 : -1;
}

void palette_to_dac(const uint8_t pal[768])
{
    port_out8(0x3C8, 0);
    for (int i = 0; i < 768; i++) port_out8(0x3C9, pal[i]);
}
