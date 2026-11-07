/* dos.h - host-backed DOS services for the recompiled program.
 *
 * Paths from the game ("fox.dat", "\EV22.SYS", "C:\FOX\score.dat") are
 * resolved against the game directory: drive letters and leading
 * backslashes are dropped, separators converted, and every component is
 * matched case-insensitively against the real directory entries (DOS
 * file names are upper-case 8.3, host file systems are often case-sensitive).
 */
#ifndef FOX_DOS_H
#define FOX_DOS_H
#include <stdint.h>
#include <stdio.h>
#include "cpu.h"

void dos_init(const char *game_dir);
const char *dos_game_dir(void);

/* resolve a DOS path; returns 0 and fills out (host path). For creation
 * (must_exist = 0) the last component may be new and keeps its spelling. */
int  dos_resolve(const char *dos_path, char *out, size_t outsz, int must_exist);

/* read a NUL-terminated string from emulated memory */
void dos_read_cstr(uint16_t seg, uint16_t off, char *out, size_t outsz);

/* DOS handle table (INT 21h 3Ch-42h); handles 0-4 are reserved */
int  dos_open(const char *dos_path, int mode, int create);   /* -> handle or -DOS error */
FILE *dos_handle_file(int h);
int  dos_close(int h);

/* INT 21h / INT 10h entry for recompiled `int` instructions */
void dos_int21(Cpu *c);
void bios_int10(Cpu *c);
void rt_interrupt(Cpu *c, uint8_t n);

/* exit hook: recompiled exit()/INT 21h 4Ch land here */
extern void (*dos_exit_hook)(int code);

#endif
