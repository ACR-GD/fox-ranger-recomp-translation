#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>

/* Far-call registry: (image-relative seg, off) -> C function.
 * Keys are image-relative so tables can be generated from functions.csv. */
typedef struct { uint32_t key; RecompFn fn; const char *name; int flags; } FarEntry;

/* open-addressing hash keyed by image-relative linear address (seg*16+off);
 * key 0xFFFFFFFF marks an empty slot. First registration of a key wins. */
#define FAR_HASH 4096u                    /* power of two, > 2x entries */
static FarEntry g_far[FAR_HASH];
static int g_far_init;

static uint32_t far_key(uint16_t seg_rel, uint16_t off) { return ((uint32_t)seg_rel << 4) + off; }
static uint32_t far_slot(uint32_t key) { return (key * 2654435761u) >> 20 & (FAR_HASH - 1); }

void far_register(uint16_t seg, uint16_t off, RecompFn fn, const char *name)
{
    far_register_flags(seg, off, fn, name, 0);
}

void far_register_abs(uint16_t seg_abs, uint16_t off, RecompFn fn, const char *name, int flags)
{
    far_register_flags((uint16_t)(seg_abs - g_load_seg), off, fn, name, flags);
}

void far_register_flags(uint16_t seg, uint16_t off, RecompFn fn, const char *name, int flags)
{
    if (!g_far_init) { for (uint32_t i = 0; i < FAR_HASH; i++) g_far[i].key = 0xFFFFFFFFu; g_far_init = 1; }
    uint32_t key = far_key(seg, off);
    for (uint32_t i = far_slot(key), n = 0; n < FAR_HASH; i = (i + 1) & (FAR_HASH - 1), n++) {
        if (g_far[i].key == key) return;                       /* first registration wins */
        if (g_far[i].key == 0xFFFFFFFFu) { g_far[i] = (FarEntry){ key, fn, name, flags }; return; }
    }
    fprintf(stderr, "far_register: table full\n"); exit(1);
}

RecompFn far_lookup_ex(uint16_t seg, uint16_t off, int *flags, const char **name)
{
    /* callers pass *loaded* segments (relocated values from memory or code) */
    if (!g_far_init) return NULL;
    uint32_t key = far_key((uint16_t)(seg - g_load_seg), off);
    for (uint32_t i = far_slot(key), n = 0; n < FAR_HASH; i = (i + 1) & (FAR_HASH - 1), n++) {
        if (g_far[i].key == 0xFFFFFFFFu) return NULL;
        if (g_far[i].key == key) {
            if (flags) *flags = g_far[i].flags;
            if (name) *name = g_far[i].name;
            return g_far[i].fn;
        }
    }
    return NULL;
}

RecompFn far_lookup(uint16_t seg, uint16_t off) { return far_lookup_ex(seg, off, NULL, NULL); }

void far_call(Cpu *c, uint16_t seg, uint16_t off, uint16_t ret_cs, uint16_t ret_ip)
{
    RecompFn fn = far_lookup(seg, off);
    if (!fn) {
        fprintf(stderr, "far_call: no function at %04X:%04X (image %04X:%04X)\n",
                seg, off, (uint16_t)(seg - g_load_seg), off);
        abort();
    }
    push16(c, ret_cs);
    push16(c, ret_ip);
    uint16_t saved_cs = c->cs;
    c->cs = seg;
    fn(c);                 /* callee pops the return address (retf) */
    c->cs = saved_cs;
}
