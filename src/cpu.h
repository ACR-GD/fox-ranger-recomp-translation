/* cpu.h - register file and stack conventions shared by recompiled functions.
 *
 * Calling convention (MSC 6, large model, cdecl, far procs):
 *   caller pushes args right-to-left, then the recompiled `lcall` pushes a
 *   4-byte return address (CS, IP) onto the *emulated* stack before invoking
 *   the C function, and the callee's `retf` pops it. Keeping the real stack
 *   layout means BP-relative argument/local accesses in recompiled bodies are
 *   translated literally ([bp+6] = first arg of a far proc).
 *   Return value in AX (DX:AX for long / far pointers); SI, DI, BP, DS, SS
 *   are preserved; AX, BX, CX, DX, ES are scratch.
 * Native replacements (e.g. blit_10a7.c) read their arguments with farg().
 */
#ifndef FOX_CPU_H
#define FOX_CPU_H

#include <stdint.h>
#include "mem.h"

#define REG16(n) union { uint16_t n##x; struct { uint8_t n##l, n##h; }; }

typedef struct Cpu {
    REG16(a); REG16(b); REG16(c); REG16(d);      /* ax/al/ah ... dx/dl/dh */
    uint16_t si, di, bp, sp;
    uint16_t cs, ds, es, ss;
    uint16_t ip;
    /* flags kept as separate booleans; recompiled code materialises them lazily */
    uint8_t  cf, zf, sf, of, pf, af, df, ifl;
} Cpu;

typedef char fox_assert_u16_size[(sizeof(uint16_t) == 2) ? 1 : -1];

static inline void     push16(Cpu *c, uint16_t v) { c->sp = (uint16_t)(c->sp - 2); wr16(c->ss, c->sp, v); }
static inline uint16_t pop16(Cpu *c)              { uint16_t v = rd16(c->ss, c->sp); c->sp = (uint16_t)(c->sp + 2); return v; }

/* n-th 16-bit argument of a far proc *at entry* (before push bp): SS:SP+4+2n */
static inline uint16_t farg(const Cpu *c, int n) { return rd16(c->ss, (uint16_t)(c->sp + 4 + 2 * n)); }
static inline FarPtr   farg_ptr(const Cpu *c, int n) { FarPtr p = { farg(c, n), farg(c, n + 1) }; return p; }

typedef void (*RecompFn)(Cpu *);

/* far-call dispatch table: lets recompiled code call native replacements and
 * resolve indirect far calls (lcall [mem]) by segment:offset */
#define FAR_FN_ISR 1   /* ends with iret: run inside an unwind context (see rt_isr_call) */
void     far_register(uint16_t seg, uint16_t off, RecompFn fn, const char *name);
void     far_register_flags(uint16_t seg, uint16_t off, RecompFn fn, const char *name, int flags);
/* register at an absolute (not image-relative) segment, e.g. BIOS F000:xxxx */
void     far_register_abs(uint16_t seg_abs, uint16_t off, RecompFn fn, const char *name, int flags);
RecompFn far_lookup(uint16_t seg, uint16_t off);
RecompFn far_lookup_ex(uint16_t seg, uint16_t off, int *flags, const char **name);
/* emulate `lcall seg:off` from recompiled code: push return address, call, (callee retf pops) */
void     far_call(Cpu *c, uint16_t seg, uint16_t off, uint16_t ret_cs, uint16_t ret_ip);

#endif
