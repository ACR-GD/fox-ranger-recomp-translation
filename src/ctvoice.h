/* ctvoice.h - native replacement of the Sound Blaster voice path.
 *
 * FOX.EXE layers (image-relative segments):
 *   113E  game SFX API (transpiled):  0002 voice_init, 0056 voice_stop,
 *         007B voice_play_bank(n), 010D voice_play_loaded, 0178 voice_load(n)
 *   159E  SB glue: hardware probes (native here) + thin wrappers that call
 *   15E5:0000 the embedded CT-VOICE.DRV entry, register ABI BX = function:
 *         0 version  1 set base (AX)  2 set IRQ (AX)  3 init  4 speaker (AL)
 *         5 status word addr (ES:DI)  6 output VOC (ES:DI -> first data block)
 *         7 input  8 stop  9 terminate  10 pause  11 continue  12 break loop
 *
 * VOICE.DAT = 18 concatenated VOC files, lengths in the u16 table DS:0B3E:
 *   clips 0-7 are loaded one at a time by voice_load(n), clips 8-17 form a
 *   resident bank (voice_load(8) reads all 62,440 bytes, voice_play_bank(n)
 *   plays the sub-VOC at sum(len[8..n-1])). 8000 Hz, 8-bit unsigned PCM.
 *
 * The status word (DS:10EE, set via fn 5) is FFFFh while a clip plays and 0
 * when it ends; ctvoice_pump() writes it back from the game thread.
 */
#ifndef FOX_CTVOICE_H
#define FOX_CTVOICE_H
#include <stdint.h>
#include "cpu.h"

#define CTVOICE_SEG 0x15E5
#define SBGLUE_SEG  0x159E

void ctvoice_register(void);          /* 15E5:0000 + 159E hardware probes */
void ctvoice_pump(void);              /* sync status word with the mixer   */

/* direct entry (register ABI), exposed for tests */
void ctvoice_entry(Cpu *c);

/* decode a VOC block chain at seg:off into 8-bit PCM; returns sample count,
 * *rate receives the first sound block's rate. Used by fn 6 and by tests. */
uint32_t voc_decode(uint16_t seg, uint16_t off, uint8_t **pcm, uint32_t *rate);

#endif
