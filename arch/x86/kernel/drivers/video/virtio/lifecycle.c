#include "mapping.h"
#include <intrin.h>
#ifdef KERNEL_AP_RUNTIME
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif
#include "arch/x86/mm/window/access.h"

// ====== x86_vgpu_authority ======

int x86_vgpu_authority(const X86_PAGING_GPU_AUTH *owner)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root;
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 1) return 0;
    BOOT_U64 flags = __readeflags();
    _disable();
    int cpu_ok = x86_fb_cpu(&cpu);
    if (flags & 512ULL) _enable();
    if (!cpu_ok || cpu.Root != owner->Root || cpu.Pat != owner->Pat ||
        cpu.PhysicalBits != owner->PhysicalBits) return 0;
    root=x86_fb_root();
    return root && (root[X86_GPU_SLOT]&~0x60ULL)==(owner->Tables[0].Base|3);
}

// ====== x86_vgpu_unbind ======

int x86_vgpu_unbind(X86_VGPU_MAPPING *owner)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root;
    if (!owner || owner->Self != (BOOT_U64)owner || !owner->Context || !owner->Window ||
        !owner->Window->Pool || !owner->Inventory || !owner->Capture || !owner->Lapic ||
        owner->Allocated>3 || (owner->Pinned>>owner->Allocated) || owner->Published>1 || owner->Registered>1 ||
        !x86_fb_cpu(&cpu) || cpu.Root != owner->Authority.Root || cpu.Pat != owner->Authority.Pat ||
        cpu.PhysicalBits != owner->Authority.PhysicalBits || !x86_page_window_idle_valid(owner->Window) ||
        !kernel_physical_validate(owner->Window->Pool)) return 0;
    root=x86_fb_root();
    if (!root) return 0;
    if (owner->Published) {
        if (root[X86_GPU_SLOT] && (root[X86_GPU_SLOT]&~0x60ULL) != (owner->Authority.Tables[0].Base|3)) return 0;
        root[X86_GPU_SLOT]=0;
#ifdef KERNEL_AP_RUNTIME
        if (xis_native_shootdown()) x86_paging_generation_bump();
        else
#endif
        if (!x86_fb_flush(owner->Authority.Root)) return 0;
        if (root[X86_GPU_SLOT]) return 0;
        owner->Published=0;
    } else if (root[X86_GPU_SLOT]) return 0;
    if (owner->Registered) {
        if (!x86_paging_gpu_register(&owner->Authority,0,0)) return 0;
        owner->Registered=0;
    } else if (x86_paging_gpu_owner()) return 0;
    owner->Authority.Ready=0;
    if (!owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory)) return 0;
    for (BOOT_U32 count=owner->Allocated; count>0; --count) {
        KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[count-1];
        if (owner->Pinned & (1U<<(count-1))) {
            if (!kernel_physical_pin(owner->Window->Pool,lease,0)) return 0;
            owner->Pinned &= ~(1U<<(count-1));
        }
        if (!kernel_physical_release(owner->Window->Pool,lease)) return 0;
        --owner->Allocated;
    }
    if (!kernel_physical_budget_matches(owner->Window->Pool, owner->Budget)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}

// ====== x86_vgpu_bind ======

int x86_vgpu_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory,
    X86_VGPU_CAPTURE capture, X86_VGPU_MAPPING *owner)
{
    X86_FB_CPU cpu;
    KERNEL_VGPU_PAGE_PLAN plan;
    volatile BOOT_U64 *root;
    if (!x86_vgpu_preflight(context,window,lapic,inventory,capture,owner,&cpu,&plan)) return 0;
    if (!plan.Count) return 1;
    owner->Self=(BOOT_U64)owner; if (!kernel_physical_budget_read(window->Pool, &owner->Budget)) return 0;
    owner->Context=context; owner->Window=window; owner->Lapic=lapic; owner->Inventory=inventory; owner->Capture=capture;
    owner->Authority.Self=(BOOT_U64)&owner->Authority; owner->Authority.Root=cpu.Root;
    owner->Authority.Pat=cpu.Pat; owner->Authority.PhysicalBits=cpu.PhysicalBits;
    owner->Authority.Cache=x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat)-1); owner->Authority.Plan=plan;
    for (BOOT_U32 index=0; index<3; ++index) {
        KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[index];
        if (!kernel_physical_allocate(window->Pool,1,lease)) goto rollback;
        ++owner->Allocated;
        if (!kernel_physical_pin(window->Pool,lease,1)) goto rollback;
        owner->Pinned |= 1U<<index;
    }
    if (!x86_vgpu_construct(owner) || !x86_vgpu_pci_match(&context->Info.Gpu,cpu.PhysicalBits)) goto rollback;
    root=x86_fb_root();
    if (!root || root[X86_GPU_SLOT]) goto rollback;
    owner->Published=1;
    root[X86_GPU_SLOT]=owner->Authority.Tables[0].Base|3;
    if (!x86_fb_flush(cpu.Root)) goto rollback;
    owner->Authority.Ready=1;
    if (!x86_paging_gpu_register(0,&owner->Authority,x86_vgpu_authority)) goto rollback;
    owner->Registered=1;
    if (!capture(context,window->Pool,lapic,inventory)) goto rollback;
    return 1;
rollback:
    (void)x86_vgpu_unbind(owner);
    return 0;
}
