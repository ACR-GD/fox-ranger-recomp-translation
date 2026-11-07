/* heap.h - paragraph allocator over the emulated far heap (ram[] between the
 * end of the loaded image + minalloc and A000h). Backs farmalloc/_ffree/
 * _fmsize/_dos_allocmem shims and the native asset loaders.
 * Book-keeping lives on the host side; blocks are returned as seg:0000. */
#ifndef FOX_HEAP_H
#define FOX_HEAP_H
#include <stdint.h>

void     heap_init(uint16_t first_seg, uint16_t end_seg);
uint16_t heap_alloc_paras(uint32_t bytes);     /* segment, 0 on failure          */
int      heap_free(uint16_t seg);              /* 0 ok, -1 not a block start     */
uint32_t heap_block_bytes(uint16_t seg);       /* requested size, 0 if unknown   */
uint16_t heap_largest_free(void);              /* paragraphs (DOS 48h failure BX) */

#endif
