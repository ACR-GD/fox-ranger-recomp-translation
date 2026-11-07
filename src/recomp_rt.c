#include "recomp_rt.h"
#include <stdio.h>
#include <stdlib.h>

int (*rt_call_hook)(Cpu *c, int is_far, uint16_t seg_img, uint16_t off);
void (*rt_call_log)(Cpu *c, uint16_t seg_img, uint16_t off, const char *name);
int32_t  rt_budget = INT32_MAX;
jmp_buf *rt_trap;
int32_t  rt_poll_countdown = 1024, rt_poll_interval = 1024;
void   (*rt_poll_fn)(Cpu *c, uint32_t at);

void rt_poll_slow(Cpu *c, uint32_t at)
{
    rt_poll_countdown = rt_poll_interval;
    if (rt_poll_fn) rt_poll_fn(c, at);
}

#define ISR_MAX 16
static jmp_buf *g_isr_ctx[ISR_MAX];
static int g_isr_depth;

void rt_isr_call(Cpu *c, RecompFn fn)
{
    jmp_buf jb;
    if (g_isr_depth >= ISR_MAX) { fprintf(stderr, "interrupt nesting too deep\n"); abort(); }
    g_isr_ctx[g_isr_depth++] = &jb;
    if (!setjmp(jb)) fn(c);
    g_isr_depth--;
}

int rt_isr_depth(void) { return g_isr_depth; }

void rt_isr_unwind(Cpu *c)
{
    (void)c;
    if (g_isr_depth > 0) longjmp(*g_isr_ctx[g_isr_depth - 1], 1);
}

void rt_fault(Cpu *c, int code, uint32_t where, const char *what)
{
    (void)c;
    if (rt_trap) longjmp(*rt_trap, code);
    fprintf(stderr, "recomp fault %d (%s) at image %05X\n", code, what, where);
    abort();
}

static void missing(uint16_t seg, uint16_t off)
{
    if (!rt_trap) fprintf(stderr, "call to %04X:%04X: no recompiled or native function\n", seg, off);
    if (rt_trap) longjmp(*rt_trap, RT_TRAP_MISSING);
    abort();
}

void rt_call_near(Cpu *c, RecompFn fn, uint16_t seg_img, uint16_t off, uint16_t ret_ip)
{
    push16(c, ret_ip);
    if (rt_call_hook && rt_call_hook(c, 0, seg_img, off)) return;
    if (!fn) fn = far_lookup(SEG(seg_img), off);
    if (!fn) missing(seg_img, off);
    fn(c);
}

void rt_call_far(Cpu *c, RecompFn fn, uint16_t seg_img, uint16_t off, uint16_t ret_cs, uint16_t ret_ip)
{
    push16(c, ret_cs);
    push16(c, ret_ip);
    if (rt_call_hook && rt_call_hook(c, 1, seg_img, off)) return;
    int flags = 0;
    const char *name = NULL;
    RecompFn found = far_lookup_ex(SEG(seg_img), off, &flags, &name);
    if (!fn) fn = found;
    if (rt_call_log) rt_call_log(c, seg_img, off, name);
    if (!fn) missing(seg_img, off);
    uint16_t saved = c->cs;
    c->cs = SEG(seg_img);
    if (flags & FAR_FN_ISR) rt_isr_call(c, fn);     /* `pushf; lcall [old]` chaining */
    else fn(c);
    c->cs = saved;
}

void rt_call_far_ptr(Cpu *c, uint16_t seg_loaded, uint16_t off, uint16_t ret_cs, uint16_t ret_ip)
{
    rt_call_far(c, NULL, (uint16_t)(seg_loaded - g_load_seg), off, ret_cs, ret_ip);
}
