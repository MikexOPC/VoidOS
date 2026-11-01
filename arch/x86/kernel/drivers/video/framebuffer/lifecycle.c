#include "transaction/internal.h"
#include "lib/memory/address_ranges.h"

// ====== clear_owner ======

static void clear_owner(X86_FB_MAPPING *owner)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
}

// ====== stable_owner ======

static int stable_owner(const X86_FB_MAPPING *owner)
{
    X86_FB_CPU cpu;
    return owner && !((BOOT_U64)owner & 7) && owner->Self == (BOOT_U64)owner &&
        owner->Ready >= 1 && owner->Ready <= 3 && owner->Window && owner->Window->Pool &&
        owner->Context && owner->Lapic && owner->Inventory &&
        owner->Allocated <= X86_PAGING_VIDEO_TABLES && !(owner->Pinned >> owner->Allocated) &&
        owner->Published <= 1 && x86_fb_cpu(&cpu) && cpu.Root == owner->Authority.Root &&
        cpu.Pat == owner->Authority.Region.Pat && cpu.PhysicalBits == owner->Authority.PhysicalBits &&
        kernel_physical_validate(owner->Window->Pool) && x86_page_window_idle_valid(owner->Window);
}

// ====== x86_fb_unbind ======

int x86_fb_unbind(X86_FB_MAPPING *owner)
{
    BOOT_U32 index;
    volatile BOOT_U64 *root;
    if (!stable_owner(owner)) return 0;
    if (x86_paging_video_owner() == &owner->Authority && !x86_paging_video_register(&owner->Authority, 0, 0)) return 0;
    owner->Ready = 3;
    root = x86_fb_root();
    if (!root) return 0;
    if (owner->Published) {
        if (root[X86_FB_SLOT] && (root[X86_FB_SLOT] & ~0x60ULL) != (owner->Authority.Tables[0].Base | 3)) return 0;
        root[X86_FB_SLOT] = 0;
        if (!x86_fb_flush(owner->Authority.Root) || root[X86_FB_SLOT]) return 0;
        owner->Published = 0;
    } else if (root[X86_FB_SLOT]) return 0;
    if (!x86_paging_inventory_capture_lapic(owner->Context, owner->Window->Pool, owner->Lapic,
        owner->Inventory)) return 0;
    for (index = owner->Allocated; index > 0; --index) {
        KERNEL_PHYSICAL_LEASE *lease = &owner->Authority.Tables[index - 1];
        if (owner->Pinned & (1U << (index - 1))) {
            if (!kernel_physical_pin(owner->Window->Pool, lease, 0)) return 0;
            owner->Pinned &= ~(1U << (index - 1));
        }
        if (!kernel_physical_release(owner->Window->Pool, lease)) return 0;
        --owner->Allocated;
    }
    if (!kernel_physical_budget_matches(owner->Window->Pool, owner->Budget)) return 0;
    clear_owner(owner);
    return 1;
}

// ====== adopt_plan ======

static int adopt_plan(X86_FB_MAPPING *owner, const KERNEL_BOOT_CONTEXT *context,
    X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory,
    const FB_MAPPING_GEOMETRY *geometry, const X86_FB_CPU *cpu)
{
    X86_PAGING_VIDEO_AUTH *auth = &owner->Authority;
    if (!kernel_physical_budget_read(window->Pool, &owner->Budget)) return 0;
    owner->Self = (BOOT_U64)owner;
    owner->Context = context; owner->Window = window; owner->Lapic = lapic; owner->Inventory = inventory;
    owner->Ready = 1;
    auth->Self = (BOOT_U64)auth; auth->Root = cpu->Root; auth->PhysicalBits = cpu->PhysicalBits;
    auth->Region.Virtual = X86_FB_VIRTUAL; auth->Region.Physical = geometry->Physical;
    auth->Region.Pages = geometry->Pages; auth->Region.TableCount = (BOOT_U32)geometry->TablePages;
    auth->Region.AperturePages = (geometry->Offset + context->Info.Framebuffer.Capacity + 4095) / 4096;
    auth->Region.Cache = x86_apic_cache_bits(x86_apic_uc_slot(cpu->Pat) - 1);
    auth->Region.Pat = cpu->Pat; auth->Region.Owner = (BOOT_U64)auth;
    return 1;
}

// ====== prepare_owned ======

static int prepare_owned(X86_FB_MAPPING *owner)
{
    BOOT_U32 index;
    volatile BOOT_U64 *root;
    for (index = 0; index < owner->Authority.Region.TableCount; ++index) {
        KERNEL_PHYSICAL_LEASE *lease = &owner->Authority.Tables[index];
        if (!kernel_physical_allocate(owner->Window->Pool, 1, lease)) return 0;
        ++owner->Allocated;
        if (!kernel_physical_pin(owner->Window->Pool, lease, 1)) return 0;
        owner->Pinned |= 1U << index;
    }
    if (!x86_fb_construct(owner) || !stable_owner(owner)) return 0;
    root = x86_fb_root();
    if (!root || root[X86_FB_SLOT]) return 0;
    owner->Authority.Ready = 1;
    owner->Published = 1;
    root[X86_FB_SLOT] = owner->Authority.Tables[0].Base | 3;
    if (!x86_fb_flush(owner->Authority.Root) ||
        !x86_paging_inventory_capture_video(owner->Context, owner->Window->Pool, owner->Lapic,
            &owner->Authority, x86_fb_authority, owner->Inventory)) return 0;
    return owner->Budget >= owner->Allocated &&
        kernel_physical_budget_matches(owner->Window->Pool, owner->Budget - owner->Allocated);
}

// ====== x86_fb_bind ======

int x86_fb_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory, X86_FB_MAPPING *owner)
{
    FB_MAPPING_GEOMETRY geometry;
    X86_FB_CPU cpu;
    if (!x86_fb_preflight(context, window, lapic, inventory, owner, &geometry, &cpu)) return 0;
    if (!geometry.Pages) return 1;
    if (!adopt_plan(owner, context, window, lapic, inventory, &geometry, &cpu)) return 0;
    if (!prepare_owned(owner)) {
        (void)x86_fb_unbind(owner);
        return 0;
    }
    owner->Ready = 2;
    if (!x86_paging_video_register(0, &owner->Authority, x86_fb_authority)) {
        (void)x86_fb_unbind(owner);
        return 0;
    }
    return 1;
}
