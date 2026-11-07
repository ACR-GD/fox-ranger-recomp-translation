/* assets.h - native loaders for FOX.DAT / VSPRITE.PAT (formats in RECON.md).
 * They place data in emulated RAM using the game's own layouts, so they can
 * stand in for the 116B loader functions or feed tests and the demo. */
#ifndef FOX_ASSETS_H
#define FOX_ASSETS_H
#include <stdint.h>
#include "mem.h"

#define VSPRITE_COUNT 208
#define SPRITE_DESC_SIZE 11

typedef struct {
    uint8_t  palette[768];          /* 6-bit VGA DAC values */
    uint32_t font8x8_lin, font8x16_lin, cho_lin, jung_lin, jong_lin;   /* in ram[] */
} FoxDat;

#include "heap.h"

int  foxdat_load(const char *path, FoxDat *out);           /* copies glyph tables into heap */
/* decode sprites; writes 208 descriptors (+0 w,+2 h,+4 packed,+6 flag,+7 far ptr) at desc.
 * All 208 decoded = 953 KB, more than conventional memory: like the game
 * (per-stage "needed" bytes at DS:03C2) only sprites with wanted[k] != 0 are
 * decoded; the rest get flag 0 and a null pointer. wanted == NULL -> all. */
int  vsprite_load(const char *path, FarPtr desc, const uint8_t *wanted);
void palette_to_dac(const uint8_t pal[768]);

#endif
