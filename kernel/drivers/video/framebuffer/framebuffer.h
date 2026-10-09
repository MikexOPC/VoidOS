#ifndef OS_FRAMEBUFFER_H
#define OS_FRAMEBUFFER_H
#include <stdint.h>

/* UEFI 2.10 section12.9.2: formats0/1 are RGB/BGR reserved-byte pixels.
   Only numeric metadata here: not ownership of RAM, mapping or a GPU. */
typedef struct {
    uint64_t Base;
    uint64_t Capacity;
    uint64_t Used;
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;
    uint32_t Format;
} OS_FRAMEBUFFER;
_Static_assert(sizeof(OS_FRAMEBUFFER) == 40, "Framebuffer metadata ABI");

uint64_t fb_mode_bytes(uint32_t width, uint32_t height, uint32_t stride, uint32_t format);
uint32_t fb_mode_score(uint32_t width, uint32_t height, uint32_t stride, uint32_t format);
uint64_t fb_descriptor_bytes(uint64_t base, uint64_t capacity, uint32_t width,
    uint32_t height, uint32_t stride, uint32_t format, uint32_t physical_bits);
uint64_t fb_pixel_offset(uint32_t width, uint32_t height, uint32_t stride,
    uint32_t format, uint64_t buffer_bytes, uint32_t x, uint32_t y);
uint32_t fb_color_word(uint32_t format, uint8_t red, uint8_t green, uint8_t blue);

#endif
