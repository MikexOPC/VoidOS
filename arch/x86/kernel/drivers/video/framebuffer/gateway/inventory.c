#include "inventory.h"

// ====== x86_fb_live_inventory ======

int x86_fb_live_inventory(const X86_FB_MAPPING *owner, const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *inventory)
{
    BOOT_U32 index;
    if (!owner || !context || !pool || !lapic || !inventory) return 0;
    if (!context->Info.Framebuffer.Base) {
        for (index = 0; index < sizeof(*owner); ++index)
            if (((const unsigned char *)owner)[index]) return 0;
        return x86_paging_inventory_verify_lapic(context, pool, lapic, inventory);
    }
    if (owner->Self != (BOOT_U64)owner || owner->Ready != 2 || owner->Published != 1 ||
        owner->Context != context || owner->Lapic != lapic || owner->Inventory != inventory ||
        !owner->Window || owner->Window->Pool != pool || !owner->Allocated ||
        owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        owner->Allocated != owner->Authority.Region.TableCount ||
        owner->Pinned != (1U << owner->Allocated) - 1U) return 0;
    return x86_paging_inventory_verify_video(context, pool, lapic, &owner->Authority,
        x86_fb_authority, inventory);
}
