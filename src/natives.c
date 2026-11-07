/* natives.c - native replacements for individual FOX.EXE routines that are
 * not transpiled (see work/function_modules.csv strategy column). */
#include "natives.h"
#include "vga_modex.h"

#define RETF(c) ((c)->sp = (uint16_t)((c)->sp + 4))

/* 04A4:00A2 (img 04AE2) set_video_mode: mode 13h -> unchained Mode Y, VRAM cleared */
static void n_set_mode_y(Cpu *c) { vga_fox_set_mode_y(&g_vga); RETF(c); }

/* 04A4:14B0 (img 05EF0) check_keyfiles: \EV21.SYS / \EV22.SYS size check.
 * Always "authorised" - the same bypass tools/patch_protection.py applies. */
static void n_check_keyfiles(Cpu *c) { c->al = 0; RETF(c); }

void natives_register(void)
{
    far_register(0x04A4, 0x00A2, n_set_mode_y, "set_mode_y");
    far_register(0x04A4, 0x14B0, n_check_keyfiles, "check_keyfiles");
}
