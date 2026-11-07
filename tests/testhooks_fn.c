/* testhooks_fn.c - drive single recompiled functions for tests/diff_fn.py */
#include <stdlib.h>
#include <string.h>
#include "../src/recomp_rt.h"
#include "../src/vga_modex.h"
#include "../src/crt_shims.h"
#include "../src/heap.h"
#include "recomp_gen.h"

static Cpu g_cpu;
Cpu *t_cpu(void) { return &g_cpu; }

/* ---- stubs: callee entry (image seg:off) -> bytes popped on return */
#define MAX_STUBS 1024
static struct { uint32_t lin; uint16_t pop; } g_stub[MAX_STUBS];
static int g_nstub;
void t_stub_clear(void) { g_nstub = 0; }
void t_stub_add(uint16_t seg, uint16_t off, uint16_t pop)
{
    if (g_nstub < MAX_STUBS) { g_stub[g_nstub].lin = ((uint32_t)seg << 4) + off; g_stub[g_nstub].pop = pop; g_nstub++; }
}

/* ---- call trace: target linear + 6 argument words */
#define MAX_CALLS 65536
typedef struct { uint32_t lin; uint16_t args[6]; } CallRec;
static CallRec g_calls[MAX_CALLS];
static int g_ncalls;
int t_ncalls(void) { return g_ncalls; }
CallRec *t_calls(void) { return g_calls; }

static int hook(Cpu *c, int is_far, uint16_t seg, uint16_t off)
{
    (void)is_far;
    uint32_t lin = ((uint32_t)seg << 4) + off;
    for (int i = 0; i < g_nstub; i++) {
        if (g_stub[i].lin != lin) continue;
        if (g_ncalls < MAX_CALLS) {
            CallRec *r = &g_calls[g_ncalls];
            r->lin = lin;
            uint16_t base = (uint16_t)(c->sp + g_stub[i].pop);    /* args above the return address */
            for (int k = 0; k < 6; k++) r->args[k] = rd16(c->ss, (uint16_t)(base + 2 * k));
        }
        uint32_t k = (uint32_t)g_ncalls++;
        c->ax = (uint16_t)(0x1234 + 0x9E37 * k + lin * 31);
        c->dx = (uint16_t)(0x0F0F + 0x3B5D * k + lin);
        c->sp = (uint16_t)(c->sp + g_stub[i].pop);
        return 1;
    }
    return 0;
}

/* ---- I/O trace */
#define MAX_IO 4096
static uint32_t g_io_tr[MAX_IO];
static int g_nio;
static void io_tr(int is_out, uint16_t port, uint8_t val)
{ if (is_out && g_nio < MAX_IO) g_io_tr[g_nio++] = ((uint32_t)port << 8) | val; }
int t_nio(void) { return g_nio; }
uint32_t *t_io(void) { return g_io_tr; }

void t_runtime_init(uint16_t heap_lo, uint16_t heap_hi)
{
    recomp_register_all();
    crt_register();
    heap_init(heap_lo, heap_hi);
    crt_init();
    rt_call_hook = hook;
    g_io.trace = io_tr;
}

/* run one recompiled function; ret_cs/ret_ip are pushed like the caller would.
 * returns 0 = returned normally, else RT_TRAP_* code */
int t_run(uint16_t seg, uint16_t off, int is_far, uint16_t ret_cs, uint16_t ret_ip, int32_t budget)
{
    RecompFn fn = far_lookup(SEG(seg), off);
    if (!fn) return 99;
    g_ncalls = 0; g_nio = 0;
    rt_budget = budget;
    jmp_buf jb;
    rt_trap = &jb;
    int code = setjmp(jb);
    if (code == 0) {
        Cpu *c = &g_cpu;
        if (is_far) push16(c, ret_cs);
        push16(c, ret_ip);
        c->cs = SEG(seg);
        fn(c);
    }
    rt_trap = NULL;
    return code;
}

#ifdef RT_TRACE
/* (lin, ax bx cx dx si di bp sp ds es) before each recompiled instruction */
#define MAX_TRACE 200000
static uint16_t g_tr[MAX_TRACE][12];
static int g_ntr;
void rt_step(const Cpu *c, uint32_t lin)
{
    if (g_ntr >= MAX_TRACE) return;
    uint16_t *t = g_tr[g_ntr++];
    t[0] = (uint16_t)lin; t[1] = (uint16_t)(lin >> 16);
    t[2] = c->ax; t[3] = c->bx; t[4] = c->cx; t[5] = c->dx; t[6] = c->si; t[7] = c->di;
    t[8] = c->bp; t[9] = c->sp; t[10] = c->ds; t[11] = c->es;
}
int t_ntrace(void) { return g_ntr; }
uint16_t *t_trace(void) { return &g_tr[0][0]; }
void t_trace_reset(void) { g_ntr = 0; }
#endif
