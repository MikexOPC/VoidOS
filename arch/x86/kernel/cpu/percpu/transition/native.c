#include "native.h"
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/sync/internal.h"
#include "../internal.h"
#include "kernel/diagnostics/log.h"
#include "../startup/commit/retention.h"
#ifdef KERNEL_TEST_AP_IMAGE
#include "test/arch/x86/kernel/cpu/percpu/startup/installation/probe/probe.h"
#endif
static X86_AP_TRANSITION transition;
#ifndef KERNEL_TEST_BOOTSTRAP_MEMORY
typedef struct {
    X86_PAGE_WINDOW  *Window;
    X86_PAGING_INVENTORY  *Inventory;
    X86_FB_CPU  Cpu;
    BOOT_U32  Calls, FailAt;
} XT_NATIVE;
static XT_NATIVE native;
typedef struct { BOOT_U64 *Page; int Write; } XT_COPY;
#endif

// ====== x86_ap_transition_early ======

int x86_ap_transition_early(KERNEL_PHYSICAL_ALLOCATOR *pool) {
    const X86_AP_LOW_MEMORY *code = x86_ap_low_current();
    if (!code || !xt_early(pool, code, &transition)) return 0;
    return kernel_log(transition.State == 1 ? 0 : 2,
        transition.State == 1 ? "KERNEL_AP_TRANSITION_DATA_RESERVED" : "KERNEL_AP_TRANSITION_UNAVAILABLE");
}

// ====== safe_private ======

#ifndef KERNEL_TEST_BOOTSTRAP_MEMORY

static int safe_private(void *context) {
    XT_NATIVE *owner = context;
    X86_FB_CPU cpu;
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    return owner == &native && locks && !locks->Depth && !ks_irq_state() && ks_boot_cpu() &&
        owner->Window && x86_page_window_idle_valid(owner->Window) && x86_fb_cpu(&cpu) &&
        cpu.Root == owner->Cpu.Root && cpu.Pat == owner->Cpu.Pat && cpu.PhysicalBits == owner->Cpu.PhysicalBits;
}

// ====== unreferenced_private ======

static int unreferenced_private(BOOT_U64 physical) {
    const X86_PAGING_INVENTORY *inventory = native.Inventory;
    if (!inventory || !inventory->Ready || inventory->Count > X86_BOOT_TABLE_LIMIT) return 0;
    for (BOOT_U32 index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        if (record->Physical == physical) return 0;
        if (record->Level != 3) continue;
        const volatile BOOT_U64 *entries = x86_paging_view(record->View);
        if (!entries) return 0;
        for (BOOT_U32 slot = 0; slot < 512; ++slot)
            if ((entries[slot] & 1) && (entries[slot] & 0x000ffffffffff000ULL) == physical) return 0;
    }
    return 1;
}

// ====== copy_private ======

static int copy_private(void *context) {
    XT_COPY *request = context;
    volatile BOOT_U64 *view = (volatile BOOT_U64 *)x86_window_bytes();
    if (!view) return 0;
    for (BOOT_U32 index = 0; index < 512; ++index) {
        if (request->Write) { view[index] = request->Page[index]; if (view[index] != request->Page[index]) return 0; }
        else request->Page[index] = view[index];
    }
    return 1;
}

// ====== transfer_private ======

static int transfer_private(void *context, BOOT_U64 physical, BOOT_U64 *page, int write) {
    if (write && (xj_retained(&transition) || transition.State == 4 || x86_ap_low_is_frozen(transition.Code, &transition))) return 0;
    if (!safe_private(context) || !page || ++native.Calls == native.FailAt) return 0;
    if (physical == native.Cpu.Root) { if (write) return 0; }
    else if (physical == transition.Code->Lease.Base) {
        if (!x86_ap_low_valid(transition.Code) || !x86_ap_low_is_held(transition.Code, &transition) ||
            (write && transition.State != 3)) return 0;
    }
    else if (physical == transition.Data.Base) {
        if (!kernel_physical_lease_state(transition.Pool, &transition.Data, 1)) return 0;
    } else if (physical >= transition.Tables.Base && !(physical & 4095) &&
        (physical - transition.Tables.Base) / 4096 < 4) {
        if (!kernel_physical_lease_state(transition.Pool, &transition.Tables, 1)) return 0;
    } else return 0;
    if (write && !unreferenced_private(physical)) return 0;
    XT_COPY request = {page, write};
    return x86_apic_borrow(native.Window, physical, 0, write, copy_private, &request) && safe_private(context);
}

// ====== report_private ======

// ====== reject_private ======

static int reject_private(const char *reason) {
    (void)kernel_text("KERNEL_AP_TRANSITION_IO_CALLS: "); (void)kernel_number(native.Calls);
    (void)kernel_text("\n"); (void)kernel_log(3, reason);
    return 0;
}

// ====== report_private ======

static int report_private(void) {
    return kernel_text("KERNEL_AP_TRANSITION_OWNER: ") && kernel_number((BOOT_U64)&transition) &&
        kernel_text("\nKERNEL_AP_TRANSITION_TABLES: ") && kernel_number(transition.Tables.Base) &&
        kernel_text("\nKERNEL_AP_TRANSITION_DATA: ") && kernel_number(transition.Data.Base) &&
        kernel_text("\nKERNEL_AP_TRANSITION_ROOT: ") && kernel_number(transition.Root) &&
        kernel_text("\nKERNEL_AP_TRANSITION_SOURCE: ") && kernel_number((BOOT_U64)transition.Source) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_AP_TRANSITION_PREPARED");
}

// ====== x86_ap_transition_start ======

#endif

int x86_ap_transition_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory) {
#ifdef KERNEL_TEST_BOOTSTRAP_MEMORY
    (void)context; (void)window; (void)lapic; (void)inventory;
    return kernel_log(2, "KERNEL_AP_TRANSITION_DIAGNOSTIC_SKIPPED");
#else
    if (native.Window || !window || transition.Self != (BOOT_U64)&transition || transition.Pool != window->Pool) return 0;
    if (transition.State == 2) {
#ifdef KERNEL_TEST_AP_TRANSITION
        return 0;
#else
        return 1;
#endif
    }
    native.Window = window; native.Inventory = inventory;
    if (!x86_fb_cpu(&native.Cpu) || !safe_private(&native)) return reject_private("AP_TRANSITION_CPU_REJECTED");
    if (!x86_paging_inventory_capture_lapic(context, window->Pool, lapic, inventory)) return reject_private("AP_TRANSITION_INVENTORY_REJECTED");
    if (!unreferenced_private(transition.Code->Lease.Base)) return reject_private("AP_TRANSITION_CODE_ALIASED");
    if (!unreferenced_private(transition.Data.Base)) return reject_private("AP_TRANSITION_DATA_ALIASED");
    XT_IO io = {transfer_private, safe_private, &native};
#if defined(KERNEL_TEST_AP_TRANSITION) && KERNEL_TEST_AP_TRANSITION == 2
    BOOT_U64 budget;
    if (!kernel_physical_budget_read(window->Pool, &budget)) return 0;
    native.FailAt = 2;
    if (xt_prepare(&transition, native.Cpu.Root, native.Cpu.Pat, &io) || transition.Tables.Id ||
        !kernel_physical_budget_matches(window->Pool, budget) || !safe_private(&native)) return 0;
    native.FailAt = 0; native.Calls = 0;
    if (!kernel_log(0, "KERNEL_AP_TRANSITION_ROLLBACK_OK")) return 0;
#endif
    int prepared = xt_prepare(&transition, native.Cpu.Root, native.Cpu.Pat, &io);
    if (prepared == 2) {
        if (!kernel_log(2, "KERNEL_AP_TRANSITION_TABLES_UNAVAILABLE")) return 0;
#ifdef KERNEL_TEST_AP_TRANSITION
        return 0;
#else
        return 1;
#endif
    }
    if (prepared != 1) return reject_private("AP_TRANSITION_CONSTRUCTION_REJECTED");
    if (!x86_paging_inventory_capture_lapic(context, window->Pool, lapic, inventory)) return reject_private("AP_TRANSITION_FINAL_INVENTORY_REJECTED");
    if (!report_private()) return 0;
#ifdef KERNEL_TEST_AP_IMAGE
    if (!xb_installation_probe(&transition, KERNEL_TEST_AP_TRANSITION == 5))
        return reject_private("AP_IMAGE_INSTALLATION_FAILED");
#endif
#if defined(KERNEL_TEST_AP_TRANSITION) && KERNEL_TEST_AP_TRANSITION == 3
    BOOT_U64 frozen_budget;
    if (!kernel_physical_budget_read(window->Pool, &frozen_budget) || !xt_freeze(&transition) ||
        transition.State != 4 || !xt_verify(&transition) || xt_freeze(&transition) ||
        xt_cancel(&transition) || xt_build(&transition) ||
        x86_ap_low_unhold(transition.Code, &transition) ||
        !kernel_physical_budget_matches(window->Pool, frozen_budget) || !safe_private(&native))
        return reject_private("AP_TRANSITION_FREEZE_REJECTED");
    if (!kernel_log(0, "KERNEL_AP_TRANSITION_FROZEN_RETAINED")) return 0;
#endif
#ifdef KERNEL_TEST_AP_TRANSITION
    if (!kernel_log(0, "KERNEL_AP_TRANSITION_TEST_READY")) return 0;
    x86_kernel_stop(0x5452414e53505245ULL);
#else
    return 1;
#endif
#endif
}
