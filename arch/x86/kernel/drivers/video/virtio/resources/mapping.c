#include "mapping.h"
#include "lib/memory/address_ranges.h"

// ====== x86_vgpu_surface_check ======

int x86_vgpu_surface_check(const X86_SURFACE_AUTH *auth)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root=x86_fb_root();
    return auth && auth->Self==(BOOT_U64)auth && auth->Ready==1 &&
        x86_fb_cpu(&cpu) && cpu.Root==auth->Root && cpu.Pat==auth->Pat &&
        cpu.PhysicalBits==auth->PhysicalBits && root &&
        (root[X86_SURFACE_SLOT]&~0x60ULL)==(auth->Tables[0].Base|3);
}

// ====== x86_vgpu_surface_unbind ======

int x86_vgpu_surface_unbind(X86_SURFACE_MAPPING *owner)
{
    X86_FB_CPU cpu;
    if (!owner || owner->Self!=(BOOT_U64)owner || !owner->Gpu || !owner->Gpu->Window ||
        owner->Allocated>11 || (owner->Pinned>>owner->Allocated) || owner->Published>1 || owner->Registered>1 ||
        !x86_fb_cpu(&cpu) || cpu.Root!=owner->Authority.Root || cpu.Pat!=owner->Authority.Pat ||
        !x86_page_window_idle_valid(owner->Gpu->Window)) return 0;
    volatile BOOT_U64 *root=x86_fb_root();
    KERNEL_PHYSICAL_ALLOCATOR *pool=owner->Gpu->Window->Pool;
    if (!root) return 0;
    if (owner->Published) {
        if (root[X86_SURFACE_SLOT] && (root[X86_SURFACE_SLOT]&~0x60ULL)!=(owner->Authority.Tables[0].Base|3)) return 0;
        root[X86_SURFACE_SLOT]=0;
        if (!x86_fb_flush(cpu.Root) || root[X86_SURFACE_SLOT]) return 0;
        owner->Published=0;
    } else if (root[X86_SURFACE_SLOT]) return 0;
    if (owner->Registered) {
        if (!x86_surface_register(&owner->Authority,0,0)) return 0;
        owner->Registered=0;
    } else if (x86_surface_owner()) return 0;
    owner->Authority.Ready=0;
    if (!owner->Gpu->Capture(owner->Gpu->Context,pool,owner->Gpu->Lapic,owner->Gpu->Inventory)) return 0;
    for (BOOT_U32 index=owner->Allocated; index; --index) {
        KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[index-1];
        if (owner->Pinned&(1U<<(index-1))) {
            if (!kernel_physical_pin(pool,lease,0)) return 0;
            owner->Pinned&=~(1U<<(index-1));
        }
        if (!kernel_physical_release(pool,lease)) return 0;
        --owner->Allocated;
    }
    if (owner->DataPinned) {
        if (!kernel_physical_pin(pool,&owner->Authority.Data,0)) return 0;
        owner->DataPinned=0;
    }
    if (owner->DataAllocated) {
        if (!kernel_physical_release(pool,&owner->Authority.Data)) return 0;
        owner->DataAllocated=0;
    }
    if (!kernel_physical_budget_matches(pool, owner->Budget)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}

// ====== x86_vgpu_surface_bind ======

int x86_vgpu_surface_bind(X86_VGPU_MAPPING *gpu,uint32_t width,uint32_t height,X86_SURFACE_MAPPING *owner)
{
    X86_FB_CPU cpu;
    BOOT_U64 bytes=kernel_vgpu_surface_bytes(width,height);
    if (!owner || ((BOOT_U64)owner&7) || !gpu || gpu->Self!=(BOOT_U64)gpu || !bytes || x86_surface_owner() ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)gpu,sizeof(*gpu)) ||
        !gpu->Window || !gpu->Window->Pool || !gpu->Capture ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)gpu->Window,sizeof(*gpu->Window)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)gpu->Window->Pool,sizeof(*gpu->Window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)gpu->Context,sizeof(*gpu->Context)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)gpu->Inventory,sizeof(*gpu->Inventory)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_SURFACE_VIRTUAL-4096,4098ULL*4096) ||
        !x86_page_window_idle_valid(gpu->Window) || !x86_fb_cpu(&cpu) ||
        cpu.Root!=gpu->Authority.Root || cpu.Pat!=gpu->Authority.Pat || (cpu.Pat&255)!=6 ||
        !x86_vgpu_authority(&gpu->Authority) || !x86_fb_root() || x86_fb_root()[X86_SURFACE_SLOT]) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    if (!gpu->Capture(gpu->Context,gpu->Window->Pool,gpu->Lapic,gpu->Inventory)) return 0;
    KERNEL_PHYSICAL_ALLOCATOR *pool=gpu->Window->Pool;
    if (!kernel_physical_budget_at_least(pool, (bytes+4095)/4096+2+((bytes+4095)/4096+2+511)/512)) return 0;
    owner->Self=(BOOT_U64)owner; owner->Gpu=gpu; if (!kernel_physical_budget_read(pool, &owner->Budget)) return 0;
    owner->Authority=(X86_SURFACE_AUTH){0};
    owner->Authority.Self=(BOOT_U64)&owner->Authority; owner->Authority.Root=cpu.Root;
    owner->Authority.Pat=cpu.Pat; owner->Authority.PhysicalBits=cpu.PhysicalBits; owner->Authority.Bytes=bytes;
    if (!kernel_physical_allocate(pool,(bytes+4095)/4096,&owner->Authority.Data)) goto rollback;
    owner->DataAllocated=1;
    if (!kernel_physical_pin(pool,&owner->Authority.Data,1)) goto rollback;
    owner->DataPinned=1;
    owner->Authority.Count=(BOOT_U32)(2+(owner->Authority.Data.Pages+2+511)/512);
    for (BOOT_U32 index=0; index<owner->Authority.Count; ++index) {
        if (!kernel_physical_allocate(pool,1,&owner->Authority.Tables[index])) goto rollback;
        ++owner->Allocated;
        if (!kernel_physical_pin(pool,&owner->Authority.Tables[index],1)) goto rollback;
        owner->Pinned|=1U<<index;
    }
    if (!x86_vgpu_surface_construct(owner)) goto rollback;
    owner->Published=1;
    x86_fb_root()[X86_SURFACE_SLOT]=owner->Authority.Tables[0].Base|3;
    if (!x86_fb_flush(cpu.Root)) goto rollback;
    owner->Authority.Ready=1;
    if (!x86_surface_register(0,&owner->Authority,x86_vgpu_surface_check)) goto rollback;
    owner->Registered=1;
    if (!gpu->Capture(gpu->Context,pool,gpu->Lapic,gpu->Inventory)) goto rollback;
    return 1;
rollback:
    (void)x86_vgpu_surface_unbind(owner);
    return 0;
}

// ====== x86_vgpu_surface_pixels ======

volatile uint32_t *x86_vgpu_surface_pixels(X86_SURFACE_MAPPING *owner)
{
    if (!owner || owner->Self!=(BOOT_U64)owner || x86_surface_owner()!=&owner->Authority ||
        !x86_vgpu_surface_check(&owner->Authority) ||
        !owner->Gpu->Capture(owner->Gpu->Context,owner->Gpu->Window->Pool,owner->Gpu->Lapic,owner->Gpu->Inventory)) return 0;
    return (volatile uint32_t *)X86_SURFACE_VIRTUAL;
}
