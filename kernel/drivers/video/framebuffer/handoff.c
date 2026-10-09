#include "handoff.h"

// ====== read_field ======

static uint64_t read_field(const unsigned char *data, uint32_t bytes)
{
    uint64_t value = 0;
    uint32_t index;
    for (index = 0; index < bytes; ++index) value |= (uint64_t)data[index] << (index * 8);
    return value;
}

// ====== fb_handoff_memory_valid ======

int fb_handoff_memory_valid(const OS_FRAMEBUFFER *fb, const void *map,
    uint64_t map_bytes, uint64_t descriptor_bytes, uint32_t bits)
{
    const unsigned char *data = (const unsigned char *)map;
    uint64_t index, base, end, limit;
    if (!fb_handoff_valid(fb,bits) || !map || descriptor_bytes < 40 || descriptor_bytes > 4096 ||
        !map_bytes || map_bytes % descriptor_bytes || map_bytes / descriptor_bytes > 4096 ||
        (uint64_t)(uintptr_t)map > UINT64_MAX - map_bytes) return 0;
    if (!fb->Base) return 1;
    base = fb->Base & ~4095ULL;
    end = (fb->Base + fb->Capacity + 4095) & ~4095ULL;
    limit = 1ULL << bits;
    if (base < (uint64_t)(uintptr_t)map + map_bytes && (uint64_t)(uintptr_t)map < end) return 0;
    for (index = 0; index < map_bytes / descriptor_bytes; ++index) {
        const unsigned char *descriptor = data + index * descriptor_bytes;
        uint64_t type = read_field(descriptor,4);
        uint64_t physical = read_field(descriptor + 8,8);
        uint64_t pages = read_field(descriptor + 24,8);
        uint64_t bytes;
        if (!pages || (physical & 4095) || physical >= limit || pages > (limit - physical) / 4096) return 0;
        bytes = pages * 4096;
        if (base < physical + bytes && physical < end && type != 0 && type != 11) return 0;
    }
    return 1;
}
