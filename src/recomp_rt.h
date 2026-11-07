/* recomp_rt.h - support code included by every generated file (gen/recomp_*.c).
 *
 * Generated functions have the signature  void f_SSSS_OOOO(Cpu *c)  where
 * SSSS:OOOO is the image-relative entry point. The caller has already pushed
 * the return address (2 bytes near, 4 bytes far) on the emulated stack and the
 * callee's ret/retf pops it, so BP-relative argument offsets are unchanged.
 *
 * Arithmetic flags live in a function-local `Flags` struct (dead stores are
 * removed by the C compiler) and are synchronised with the Cpu struct only at
 * calls and returns, where callers/callees may observe them (e.g. CF results).
 */
#ifndef FOX_RECOMP_RT_H
#define FOX_RECOMP_RT_H
#include <stdint.h>
#include <setjmp.h>
#include "cpu.h"
#include "mem.h"
#include "io.h"
#include "dos.h"

typedef struct { uint8_t cf, zf, sf, of, pf, af; } Flags;

/* ------------------------------------------------------------- calls */
/* test hook: return non-zero to swallow the call (the hook must emulate the
 * callee's ret/retf, i.e. pop the return address). seg is image-relative. */
extern int (*rt_call_hook)(Cpu *c, int is_far, uint16_t seg_img, uint16_t off);
void rt_call_near(Cpu *c, RecompFn fn, uint16_t seg_img, uint16_t off, uint16_t ret_ip);
void rt_call_far(Cpu *c, RecompFn fn, uint16_t seg_img, uint16_t off, uint16_t ret_cs, uint16_t ret_ip);
/* indirect far call through a loaded far pointer (lcall [mem]) */
void rt_call_far_ptr(Cpu *c, uint16_t seg_loaded, uint16_t off, uint16_t ret_cs, uint16_t ret_ip);
/* optional call log (debugging): every far call, before dispatch */
extern void (*rt_call_log)(Cpu *c, uint16_t seg_img, uint16_t off, const char *name);

/* ------------------------------------------------- faults and budgets */
extern int32_t  rt_budget;          /* decremented at loop back-edges (tests) */
extern jmp_buf *rt_trap;            /* if set, faults longjmp here with a code */
enum { RT_TRAP_BUDGET = 1, RT_TRAP_DIVIDE = 2, RT_TRAP_UNSUPPORTED = 3, RT_TRAP_BADSWITCH = 4, RT_TRAP_HALT = 5, RT_TRAP_MISSING = 6 };
void rt_fault(Cpu *c, int code, uint32_t where, const char *what);

/* Loop back-edges are where asynchronous events reach the game: in
 * production builds every rt_poll_interval-th back-edge calls rt_poll_fn
 * (platform.c), which delivers due timer/keyboard interrupts on the game
 * thread, pumps window events and presents frames. Tests use budgets instead. */
extern int32_t rt_poll_countdown, rt_poll_interval;
extern void (*rt_poll_fn)(Cpu *c, uint32_t at);
void rt_poll_slow(Cpu *c, uint32_t at);
#ifdef RT_BUDGET
#  define RT_BACKEDGE(c, at) do { if (--rt_budget < 0) rt_fault((c), RT_TRAP_BUDGET, (at), "budget"); } while (0)
#else
#  define RT_BACKEDGE(c, at) do { if (--rt_poll_countdown <= 0) rt_poll_slow((c), (at)); } while (0)
#endif

/* interrupt handlers (functions ending in iret) run inside an unwind context
 * so _chain_intr() can abandon the handler after jumping to the previous one */
void rt_isr_call(Cpu *c, RecompFn fn);
int  rt_isr_depth(void);
void rt_isr_unwind(Cpu *c);       /* longjmp to the innermost rt_isr_call */

/* per-instruction trace (tests: build with -DRT_TRACE) */
#ifdef RT_TRACE
void rt_step(const Cpu *c, uint32_t lin);
#  define RT_STEP(c, lin) rt_step((c), (lin))
#else
#  define RT_STEP(c, lin) do { (void)(c); } while (0)
#endif

/* ------------------------------------------------------------- flags */
static inline uint8_t rt_parity(uint8_t v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (uint8_t)(~v & 1); }
static inline void rt_szp8(Flags *F, uint8_t r)   { F->zf = r == 0; F->sf = r >> 7; F->pf = rt_parity(r); }
static inline void rt_szp16(Flags *F, uint16_t r) { F->zf = r == 0; F->sf = r >> 15; F->pf = rt_parity((uint8_t)r); }

static inline uint8_t add8(Flags *F, uint8_t a, uint8_t b, int cin)
{ unsigned r = (unsigned)a + b + (cin & 1); uint8_t o = (uint8_t)r;
  F->cf = r > 0xFF; F->of = ((a ^ o) & (b ^ o)) >> 7 & 1; F->af = ((a ^ b ^ o) >> 4) & 1; rt_szp8(F, o); return o; }
static inline uint16_t add16(Flags *F, uint16_t a, uint16_t b, int cin)
{ uint32_t r = (uint32_t)a + b + (cin & 1); uint16_t o = (uint16_t)r;
  F->cf = r > 0xFFFF; F->of = ((a ^ o) & (b ^ o)) >> 15 & 1; F->af = ((a ^ b ^ o) >> 4) & 1; rt_szp16(F, o); return o; }
static inline uint8_t sub8(Flags *F, uint8_t a, uint8_t b, int bin)
{ unsigned r = (unsigned)a - b - (bin & 1); uint8_t o = (uint8_t)r;
  F->cf = ((unsigned)b + (bin & 1)) > a; F->of = ((a ^ b) & (a ^ o)) >> 7 & 1; F->af = ((a ^ b ^ o) >> 4) & 1; rt_szp8(F, o); return o; }
static inline uint16_t sub16(Flags *F, uint16_t a, uint16_t b, int bin)
{ uint32_t r = (uint32_t)a - b - (bin & 1); uint16_t o = (uint16_t)r;
  F->cf = ((uint32_t)b + (bin & 1)) > a; F->of = ((a ^ b) & (a ^ o)) >> 15 & 1; F->af = ((a ^ b ^ o) >> 4) & 1; rt_szp16(F, o); return o; }
static inline uint8_t  logic8(Flags *F, uint8_t r)   { F->cf = F->of = 0; rt_szp8(F, r); return r; }
static inline uint16_t logic16(Flags *F, uint16_t r) { F->cf = F->of = 0; rt_szp16(F, r); return r; }
static inline uint8_t  inc8(Flags *F, uint8_t a)   { uint8_t c0 = F->cf; uint8_t r = add8(F, a, 1, 0); F->cf = c0; return r; }
static inline uint16_t inc16(Flags *F, uint16_t a) { uint8_t c0 = F->cf; uint16_t r = add16(F, a, 1, 0); F->cf = c0; return r; }
static inline uint8_t  dec8(Flags *F, uint8_t a)   { uint8_t c0 = F->cf; uint8_t r = sub8(F, a, 1, 0); F->cf = c0; return r; }
static inline uint16_t dec16(Flags *F, uint16_t a) { uint8_t c0 = F->cf; uint16_t r = sub16(F, a, 1, 0); F->cf = c0; return r; }
static inline uint8_t  neg8(Flags *F, uint8_t a)   { uint8_t r = sub8(F, 0, a, 0); F->cf = a != 0; return r; }
static inline uint16_t neg16(Flags *F, uint16_t a) { uint16_t r = sub16(F, 0, a, 0); F->cf = a != 0; return r; }

/* shifts/rotates: count masked to 5 bits (80286+ behaviour, as emulated by the
 * reference); count 0 leaves flags untouched */
#define RT_SHIFT(BITS, T, MSB)                                                         \
static inline T shl##BITS(Flags *F, T a, unsigned n) { n &= 31; if (!n) return a;      \
  uint32_t r = (uint32_t)a << n; T o = (T)r; F->cf = n <= BITS ? (r >> BITS) & 1 : 0;   \
  F->of = ((o >> MSB) & 1) ^ F->cf; rt_szp##BITS(F, o); return o; }                    \
static inline T shr##BITS(Flags *F, T a, unsigned n) { n &= 31; if (!n) return a;      \
  T o = n >= BITS ? 0 : (T)(a >> n); F->cf = n <= BITS ? (a >> (n - 1)) & 1 : 0;        \
  F->of = n == 1 ? (a >> MSB) & 1 : 0; rt_szp##BITS(F, o); return o; }                 \
static inline T sar##BITS(Flags *F, T a, unsigned n) { n &= 31; if (!n) return a;      \
  int32_t s = (int32_t)(int##BITS##_t)a; T o;                                           \
  if (n >= BITS) { o = (T)(s < 0 ? -1 : 0); F->cf = s < 0; }                            \
  else { o = (T)(s >> n); F->cf = (s >> (n - 1)) & 1; }                                 \
  F->of = 0; rt_szp##BITS(F, o); return o; }                                           \
static inline T rol##BITS(Flags *F, T a, unsigned n) { n &= 31; if (!n) return a;      \
  unsigned k = n % BITS; T o = k ? (T)((a << k) | (a >> (BITS - k))) : a;               \
  F->cf = o & 1; F->of = ((o >> MSB) & 1) ^ F->cf; return o; }                          \
static inline T ror##BITS(Flags *F, T a, unsigned n) { n &= 31; if (!n) return a;      \
  unsigned k = n % BITS; T o = k ? (T)((a >> k) | (a << (BITS - k))) : a;               \
  F->cf = (o >> MSB) & 1; F->of = ((o >> MSB) ^ (o >> (MSB - 1))) & 1; return o; }      \
static inline T rcl##BITS(Flags *F, T a, unsigned n) { n &= 31; n %= (BITS + 1);       \
  while (n--) { unsigned c = (a >> MSB) & 1; a = (T)((a << 1) | F->cf); F->cf = (uint8_t)c; } \
  F->of = ((a >> MSB) & 1) ^ F->cf; return a; }                                         \
static inline T rcr##BITS(Flags *F, T a, unsigned n) { n &= 31; n %= (BITS + 1);       \
  while (n--) { unsigned c = a & 1; a = (T)((a >> 1) | ((T)F->cf << MSB)); F->cf = (uint8_t)c; } \
  F->of = ((a >> MSB) ^ (a >> (MSB - 1))) & 1; return a; }
RT_SHIFT(8, uint8_t, 7)
RT_SHIFT(16, uint16_t, 15)

/* multiply / divide */
static inline void mul8(Cpu *c, Flags *F, uint8_t b)  { c->ax = (uint16_t)(c->al * b); F->cf = F->of = c->ah != 0; }
static inline void mul16(Cpu *c, Flags *F, uint16_t b) { uint32_t r = (uint32_t)c->ax * b; c->ax = (uint16_t)r; c->dx = (uint16_t)(r >> 16); F->cf = F->of = c->dx != 0; }
static inline void imul8(Cpu *c, Flags *F, uint8_t b)  { int16_t r = (int16_t)((int8_t)c->al * (int8_t)b); c->ax = (uint16_t)r; F->cf = F->of = r != (int8_t)r; }
static inline void imul16(Cpu *c, Flags *F, uint16_t b) { int32_t r = (int32_t)(int16_t)c->ax * (int16_t)b; c->ax = (uint16_t)r; c->dx = (uint16_t)((uint32_t)r >> 16); F->cf = F->of = r != (int16_t)r; }
static inline uint16_t imul16_3(Flags *F, uint16_t a, uint16_t b) { int32_t r = (int32_t)(int16_t)a * (int16_t)b; F->cf = F->of = r != (int16_t)r; return (uint16_t)r; }
static inline void div8(Cpu *c, uint8_t b, uint32_t at)
{ if (!b || c->ax / b > 0xFF) rt_fault(c, RT_TRAP_DIVIDE, at, "div8"); uint16_t a = c->ax; c->al = (uint8_t)(a / b); c->ah = (uint8_t)(a % b); }
static inline void div16(Cpu *c, uint16_t b, uint32_t at)
{ uint32_t a = ((uint32_t)c->dx << 16) | c->ax; if (!b || a / b > 0xFFFF) rt_fault(c, RT_TRAP_DIVIDE, at, "div16"); c->ax = (uint16_t)(a / b); c->dx = (uint16_t)(a % b); }
static inline void idiv8(Cpu *c, uint8_t b, uint32_t at)
{ int16_t a = (int16_t)c->ax; int8_t d = (int8_t)b; if (!d) rt_fault(c, RT_TRAP_DIVIDE, at, "idiv8");
  int32_t q = a / d; if (q > 127 || q < -128) rt_fault(c, RT_TRAP_DIVIDE, at, "idiv8"); c->al = (uint8_t)q; c->ah = (uint8_t)(a % d); }
static inline void idiv16(Cpu *c, uint16_t b, uint32_t at)
{ int32_t a = (int32_t)(((uint32_t)c->dx << 16) | c->ax); int16_t d = (int16_t)b; if (!d) rt_fault(c, RT_TRAP_DIVIDE, at, "idiv16");
  int64_t q = (int64_t)a / d; if (q > 32767 || q < -32768) rt_fault(c, RT_TRAP_DIVIDE, at, "idiv16"); c->ax = (uint16_t)q; c->dx = (uint16_t)(a % d); }

/* flags <-> Cpu / FLAGS word */
static inline void rt_flags_out(Cpu *c, const Flags *F) { c->cf = F->cf; c->zf = F->zf; c->sf = F->sf; c->of = F->of; c->pf = F->pf; c->af = F->af; }
static inline void rt_flags_in(const Cpu *c, Flags *F)  { F->cf = c->cf; F->zf = c->zf; F->sf = c->sf; F->of = c->of; F->pf = c->pf; F->af = c->af; }
static inline uint16_t rt_flags_word(const Cpu *c, const Flags *F)
{ return (uint16_t)(0x0002 | F->cf | (F->pf << 2) | (F->af << 4) | (F->zf << 6) | (F->sf << 7) | (c->ifl << 9) | (c->df << 10) | (F->of << 11)); }
static inline void rt_flags_set(Cpu *c, Flags *F, uint16_t w)
{ F->cf = w & 1; F->pf = (w >> 2) & 1; F->af = (w >> 4) & 1; F->zf = (w >> 6) & 1; F->sf = (w >> 7) & 1;
  c->ifl = (w >> 9) & 1; c->df = (w >> 10) & 1; F->of = (w >> 11) & 1; }

#endif
