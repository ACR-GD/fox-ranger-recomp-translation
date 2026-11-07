#include "heap.h"
#include <stdlib.h>

typedef struct { uint16_t seg, paras; uint32_t bytes; uint8_t used; } Block;
#define MAX_BLOCKS 1024
static Block g_b[MAX_BLOCKS];
static int g_n;

void heap_init(uint16_t first_seg, uint16_t end_seg)
{
    g_n = 1;
    g_b[0] = (Block){ first_seg, (uint16_t)(end_seg - first_seg), 0, 0 };
}

uint16_t heap_alloc_paras(uint32_t bytes)
{
    uint32_t paras = (bytes + 15) / 16;
    if (!paras) paras = 1;
    for (int i = 0; i < g_n; i++) {
        if (g_b[i].used || g_b[i].paras < paras) continue;
        if (g_b[i].paras > paras) {                    /* split */
            if (g_n == MAX_BLOCKS) return 0;
            for (int k = g_n; k > i + 1; k--) g_b[k] = g_b[k - 1];
            g_b[i + 1] = (Block){ (uint16_t)(g_b[i].seg + paras), (uint16_t)(g_b[i].paras - paras), 0, 0 };
            g_n++;
            g_b[i].paras = (uint16_t)paras;
        }
        g_b[i].used = 1; g_b[i].bytes = bytes;
        return g_b[i].seg;
    }
    return 0;
}

int heap_free(uint16_t seg)
{
    for (int i = 0; i < g_n; i++) {
        if (g_b[i].seg != seg || !g_b[i].used) continue;
        g_b[i].used = 0; g_b[i].bytes = 0;
        if (i + 1 < g_n && !g_b[i + 1].used) {          /* merge with next */
            g_b[i].paras = (uint16_t)(g_b[i].paras + g_b[i + 1].paras);
            for (int k = i + 1; k < g_n - 1; k++) g_b[k] = g_b[k + 1];
            g_n--;
        }
        if (i > 0 && !g_b[i - 1].used) {               /* merge with previous */
            g_b[i - 1].paras = (uint16_t)(g_b[i - 1].paras + g_b[i].paras);
            for (int k = i; k < g_n - 1; k++) g_b[k] = g_b[k + 1];
            g_n--;
        }
        return 0;
    }
    return -1;
}

uint32_t heap_block_bytes(uint16_t seg)
{
    for (int i = 0; i < g_n; i++) if (g_b[i].seg == seg && g_b[i].used) return g_b[i].bytes;
    return 0;
}

uint16_t heap_largest_free(void)
{
    uint16_t m = 0;
    for (int i = 0; i < g_n; i++) if (!g_b[i].used && g_b[i].paras > m) m = g_b[i].paras;
    return m;
}
