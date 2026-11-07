#include "mem.h"

uint8_t  ram[MEM_SIZE + MEM_SLACK];
uint16_t g_load_seg = 0x1000;
uint32_t g_mem_writes;

void mem_reset(void)
{
    memset(ram, 0, sizeof ram);
    /* BIOS data area values the game reads directly */
    ram[0x463] = 0xD4;   /* 0040:0063 CRTC base port = 03D4h (used by 05BE8, 10A7:070C) */
    ram[0x464] = 0x03;
}
