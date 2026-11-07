/* crt_shims.h - native replacements for the Microsoft C 6.0 run-time (seg 134D).
 *
 * The game calls 33 CRT entry points (the other ~57 CRT functions are
 * start-up, stdio and heap internals that become unreachable). Each shim keeps
 * the original far-call ABI: arguments on the emulated stack, results in AX or
 * DX:AX, caller pops (cdecl) - except the _aF* long-arithmetic helpers which
 * pop their own 8 bytes (`retf 8`) and _aFlshl/_aFlshr which take DX:AX, CL.
 *
 *   01DF exit        06A6 fclose      07A0 fopen       07BC fread
 *   092E fwrite      0AC6 puts        19CC _fmalloc    19D2 _ffree
 *   1C92 _fmsize     1CE6 _fstrcpy    1D1C _fstrlen    1D7C fseek
 *   1F98 sprintf     2004 inp         2012 outp        2020 _fmemmove
 *   2148 _fmemset    21F4 abs         2214 rand        224C _dos_allocmem
 *   2266 _chain_intr 2282 _dos_getvect 229F _dos_findfirst 22D2 _dos_setvect
 *   22E8 _disable    22EA _enable     22EC _aFldiv     2386 _aFlmul
 *   23B8 _aFlrem     2458 _aFlshl     2464 _aFlshr     2470 _fmemcpy
 *   24CE _fmemset (second copy, byte-identical to 2148)
 */
#ifndef FOX_CRT_SHIMS_H
#define FOX_CRT_SHIMS_H
#include <stdint.h>
#include "cpu.h"

#define CRT_SEG 0x134D
#define DGROUP_SEG 0x1ADF

void crt_init(void);          /* allocates the emulated _iob table; call after heap_init */
void crt_register(void);

/* platform hook: called after _dos_setvect / INT 21h 25h changed a vector */
extern void (*crt_vector_hook)(uint8_t vec, uint16_t seg, uint16_t off);
extern uint8_t crt_interrupts_enabled;

/* pure helpers, exposed for tests */
int32_t crt_ldiv(int32_t a, int32_t b);
int32_t crt_lrem(int32_t a, int32_t b);
int     crt_sprintf(uint16_t bseg, uint16_t boff, uint16_t fseg, uint16_t foff,
                    uint16_t ss, uint16_t argp);

#endif
