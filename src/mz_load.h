#ifndef FOX_MZ_LOAD_H
#define FOX_MZ_LOAD_H
#include <stdint.h>

typedef struct {
    uint16_t psp_seg, load_seg;
    uint16_t cs, ip, ss, sp;        /* relocated entry state */
    uint32_t image_size;
    uint16_t nrelocs;
    uint16_t end_seg;               /* first paragraph after image + minalloc (heap start) */
} MzInfo;

/* Load an (unpacked) MZ executable into ram[] at load_seg exactly like DOS:
 * PSP at load_seg-10h with the command tail, image copied, relocations
 * patched (+load_seg). Sets g_load_seg. Returns 0 on success. */
int mz_load(const char *path, uint16_t load_seg, const char *cmdtail, MzInfo *out);

#endif
