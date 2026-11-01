#include "resources.h"
#include "../internal.h"
#include "../startup/commit/retention.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"


// ====== x86_ap_check ======

int x86_ap_check(const X86_AP_AUTH *auth)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root = x86_fb_root();

    return ks_boot_cpu() && auth && auth->Self == (BOOT_U64)auth && auth->Ready == 1 &&
        x86_fb_cpu(&cpu) && cpu.Root == auth->Root && cpu.Pat == auth->Pat &&
        cpu.PhysicalBits == auth->PhysicalBits && root &&
        (root[X86_AP_SLOT] & ~0x60ULL) == (auth->Tables.Base | 3);
}

// ====== x86_ap_unbind ======

// Intel SDM 078US 4.10.4.1: remove the root link and flush CR3 on the BSP before releasing frames.
// Rollback is allowed only before commit; a startup timeout never authorizes release.
int x86_ap_unbind(X86_AP_RESOURCES *owner)
{
    X86_FB_CPU cpu;

    if (ks_irq_state() || !ks_boot_cpu() || !owner || xj_retained(owner) || owner->Self != (BOOT_U64)owner || owner->Committed || !owner->Window || !owner->Window->Pool ||
        !x86_fb_cpu(&cpu) || cpu.Root != owner->Authority.Root || cpu.Pat != owner->Authority.Pat ||
        !x86_page_window_idle_valid(owner->Window)
    )
    return 0;

    volatile BOOT_U64 *root = x86_fb_root();
    KERNEL_PHYSICAL_ALLOCATOR *pool = owner->Window->Pool;

    if (!root) return 0;

    if (owner->Published) {
        if (root[X86_AP_SLOT] && (root[X86_AP_SLOT] & ~0x60ULL) != (owner->Authority.Tables.Base | 3)) return 0;

        root[X86_AP_SLOT] = 0;

        if (!x86_fb_flush(cpu.Root) || root[X86_AP_SLOT]) return 0;

        owner->Published = 0;
    }
    else if (root[X86_AP_SLOT]) return 0;

    if (owner->Registered) {
        if (!x86_ap_register(&owner->Authority, 0, 0)) return 0;

        owner->Registered = 0;
    }
    else if (x86_ap_owner() == &owner->Authority) return 0;

    owner->Authority.Ready = 0;

    if (!owner->Capture(owner->Context, pool, owner->Lapic, owner->Inventory)) return 0;

    if (owner->TablesAllocated) {
        int released = owner->TablesPinned ? kernel_physical_release_after_unmap(pool, &owner->Authority.Tables) :
            kernel_physical_release(pool, &owner->Authority.Tables);
        if (!released) return 0;
        owner->TablesPinned = 0; owner->TablesAllocated = 0;
    }
    if (owner->DataAllocated) {
        int released = owner->DataPinned ? kernel_physical_release_after_unmap(pool, &owner->Authority.Data) :
            kernel_physical_release(pool, &owner->Authority.Data);
        if (!released) return 0;
        owner->DataPinned = 0; owner->DataAllocated = 0;
    }

    if (!kernel_physical_budget_matches(pool, owner->Budget)) return 0;

    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;

    return 1;
}

// ====== buffers_private ======

static int buffers_private(const KERNEL_BOOT_CONTEXT *context, const X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, const X86_PAGING_INVENTORY *inventory, const X86_AP_RESOURCES *owner) {
    BOOT_U64 target = (BOOT_U64)owner;
    if (!context || !window || !inventory || !owner || (target & 7) ||
        !X86_LAYOUT_RANGE(target, sizeof(*owner)) || !X86_LAYOUT_RANGE((BOOT_U64)context, sizeof(*context)) ||
        !X86_LAYOUT_RANGE((BOOT_U64)window, sizeof(*window)) || !X86_LAYOUT_RANGE((BOOT_U64)inventory, sizeof(*inventory)) ||
        !window->Pool || !X86_LAYOUT_RANGE((BOOT_U64)window->Pool, sizeof(*window->Pool))) return 0;
    return !os_address_ranges_overlap(target, sizeof(*owner), (BOOT_U64)context, sizeof(*context)) &&
        !os_address_ranges_overlap(target, sizeof(*owner), (BOOT_U64)window, sizeof(*window)) &&
        !os_address_ranges_overlap(target, sizeof(*owner), (BOOT_U64)window->Pool, sizeof(*window->Pool)) &&
        !os_address_ranges_overlap(target, sizeof(*owner), (BOOT_U64)inventory, sizeof(*inventory)) &&
        (!lapic || (X86_LAYOUT_RANGE((BOOT_U64)lapic, sizeof(*lapic)) &&
        !os_address_ranges_overlap(target, sizeof(*owner), (BOOT_U64)lapic, sizeof(*lapic))));
}

// ====== x86_ap_bind ======

int x86_ap_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory, X86_AP_CAPTURE capture, BOOT_U32 cpus, X86_AP_RESOURCES *owner)
{
    X86_FB_CPU cpu;
    BOOT_U64 self = (BOOT_U64)owner;

    if (ks_irq_state() || !ks_boot_cpu() || cpus < 2 || cpus > 64 || !buffers_private(context,window,lapic,inventory,owner) || !capture || x86_ap_owner() ||
        os_address_ranges_overlap(self, sizeof(*owner), (BOOT_U64)inventory, sizeof(*inventory)) ||
        os_address_ranges_overlap(self, sizeof(*owner), X86_AP_BASE, 64ULL * X86_AP_STRIDE) ||
        !x86_page_window_idle_valid(window) || !x86_fb_cpu(&cpu) || (cpu.Pat & 255) != 6 ||
        cpu.Root != context->Info.PageTableRoot || !x86_fb_root() || x86_fb_root()[X86_AP_SLOT]
    )
    return 0;

    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;

    if (!capture(context, window->Pool, lapic, inventory) || !kernel_physical_budget_at_least(window->Pool, ((cpus - 1U) * 29U) + x86_ap_tables(cpus))) return 0;

    KERNEL_PHYSICAL_ALLOCATOR *pool = window->Pool;

    BOOT_U64 budget; if (!kernel_physical_budget_read(pool, &budget)) return 0;
    owner->Self = self; owner->Budget = budget;
    owner->Context = context; owner->Window = window; owner->Lapic = lapic; owner->Inventory = inventory; owner->Capture = capture;
    owner->Authority.Self = (BOOT_U64)&owner->Authority; owner->Authority.Root = cpu.Root;
    owner->Authority.Pat = cpu.Pat; owner->Authority.PhysicalBits = cpu.PhysicalBits; owner->Authority.Cpus = cpus;

    if (!kernel_physical_allocate(pool, ((cpus - 1U) * 29U), &owner->Authority.Data)) goto rollback;
    owner->DataAllocated = 1;
    if (!kernel_physical_pin(pool, &owner->Authority.Data, 1)) goto rollback;
    owner->DataPinned = 1;
    if (!kernel_physical_allocate(pool, x86_ap_tables(cpus), &owner->Authority.Tables)) goto rollback;
    owner->TablesAllocated = 1;
    if (!kernel_physical_pin(pool, &owner->Authority.Tables, 1)) goto rollback;
    owner->TablesPinned = 1;

    if (!x86_ap_construct(owner)) goto rollback;

    owner->Published = 1;
    x86_fb_root()[X86_AP_SLOT] = owner->Authority.Tables.Base | 3;

    if (!x86_fb_flush(cpu.Root)) goto rollback;

    owner->Authority.Ready = 1;

    if (!x86_ap_register(0, &owner->Authority, x86_ap_check)) goto rollback;

    owner->Registered = 1;

    if (!capture(context, pool, lapic, inventory)) goto rollback;

    return 1;

rollback:
    if (!x86_ap_unbind(owner)) ks_fail_stop("AP_RESOURCES_ROLLBACK_FAILED");
    return 0;
}


// ====== x86_ap_commit ======

int x86_ap_commit(X86_AP_RESOURCES *owner) {
    if (ks_irq_state() || !ks_boot_cpu() || !owner || owner->Self != (BOOT_U64)owner || owner->Committed ||
        !owner->Published || !owner->Registered || !owner->DataAllocated || !owner->DataPinned ||
        !owner->TablesAllocated || !owner->TablesPinned || !owner->Window ||
        !x86_page_window_idle_valid(owner->Window) || x86_ap_owner() != &owner->Authority ||
        !x86_ap_check(&owner->Authority) || !owner->Capture ||
        !owner->Capture(owner->Context, owner->Window->Pool, owner->Lapic, owner->Inventory)) return 0;
    owner->Committed = 1;
    return 1;
}
