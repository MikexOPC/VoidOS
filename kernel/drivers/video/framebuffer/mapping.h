#ifndef OS_FRAMEBUFFER_MAPPING_GEOMETRY_H
#define OS_FRAMEBUFFER_MAPPING_GEOMETRY_H
#include "framebuffer.h"

typedef struct {
    uint64_t  Physical;
    uint64_t  Offset;
    uint64_t  Pages;
    uint64_t  TablePages;
    uint64_t  Used;
} FB_MAPPING_GEOMETRY;
_Static_assert(sizeof(FB_MAPPING_GEOMETRY) == 40, "Framebuffer plan ABI");

uint64_t fb_mapping_pages(uint64_t base, uint64_t capacity, uint64_t used, uint32_t bits);
uint64_t fb_mapping_tables(uint64_t pages);
int fb_mapping_geometry(const OS_FRAMEBUFFER *framebuffer, uint32_t physical_bits,
    FB_MAPPING_GEOMETRY *output);
#endif
