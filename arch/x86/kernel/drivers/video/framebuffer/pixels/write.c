#include "write.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== x86_fb_pixel ======

int x86_fb_pixel(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    unsigned char red, unsigned char green, unsigned char blue)
{
    const OS_FRAMEBUFFER *fb;
    BOOT_U64 offset, address, mapped, start;
    BOOT_U32 color;
    volatile BOOT_U32 *pixel;
    if (!owner || ((BOOT_U64)owner & 7) || owner->Self != (BOOT_U64)owner ||
        owner->Ready != 2 || owner->Published != 1 || !owner->Context || !owner->Window ||
        !owner->Window->Pool || !owner->Lapic || !owner->Inventory ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        owner->Allocated != owner->Authority.Region.TableCount ||
        owner->Pinned != (1U << owner->Allocated) - 1 || !x86_exception_environment() ||
        !x86_page_window_idle_valid(owner->Window) ||
        !x86_paging_inventory_verify_video(owner->Context, owner->Window->Pool, owner->Lapic,
            &owner->Authority, x86_fb_authority, owner->Inventory)) return 0;
    fb = &owner->Context->Info.Framebuffer;
    offset = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used, x, y);
    color = fb_color_word(fb->Format, red, green, blue);
    if (offset == ~0ULL || color == ~0U || !owner->Authority.Region.Pages ||
        owner->Authority.Region.Pages > 4321) return 0;
    mapped = owner->Authority.Region.Pages * 4096;
    start = fb->Base & 4095;
    if (offset > mapped - 4 || start > mapped - 4 - offset ||
        owner->Authority.Region.Virtual > ~0ULL - start - offset) return 0;
    address = owner->Authority.Region.Virtual + start + offset;
    if (address & 3) return 0;
    pixel = x86_fb_pixel_view(address);
    if (!pixel) return 0;
    *pixel = color;
    return *pixel == color && x86_fb_authority(&owner->Authority);
}
