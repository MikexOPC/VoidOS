#include "mapping.h"
#include "handoff.h"

// ====== fb_mapping_geometry ======

int fb_mapping_geometry(const OS_FRAMEBUFFER *framebuffer, uint32_t physical_bits,
    FB_MAPPING_GEOMETRY *output)
{
    FB_MAPPING_GEOMETRY draft = {0};
    uint64_t input = (uint64_t)(uintptr_t)framebuffer;
    uint64_t target = (uint64_t)(uintptr_t)output;
    if (!framebuffer || !output || (input & 7) || (target & 7) ||
        input > UINT64_MAX - sizeof(*framebuffer) ||
        target > UINT64_MAX - sizeof(*output) ||
        (target < input + sizeof(*framebuffer) && input < target + sizeof(*output)) ||
        !fb_handoff_valid(framebuffer, physical_bits)) return 0;
    if (framebuffer->Base) {
        draft.Pages = fb_mapping_pages(framebuffer->Base, framebuffer->Capacity,
            framebuffer->Used, physical_bits);
        draft.TablePages = fb_mapping_tables(draft.Pages);
        if (!draft.Pages || !draft.TablePages) return 0;
        draft.Physical = framebuffer->Base & ~4095ULL;
        draft.Offset = framebuffer->Base & 4095;
        draft.Used = framebuffer->Used;
    }
    *output = draft;
    return 1;
}
