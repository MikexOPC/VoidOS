#include "../resources.h"
#include "../../identity/identity.h"
#include "../../internal.h"
#include "../../clock/clock.h"
#include "../../startup/commit/retention.h"
#include "kernel/diagnostics/log.h"
static X86_AP_RESOURCES resources;
static BOOT_U32 started;

// ====== verify_private ======

static int verify_private(void) {
    const X86_AP_AUTH *auth = &resources.Authority;
    if (!x86_ap_check(auth) || !resources.Capture(resources.Context, resources.Window->Pool, resources.Lapic, resources.Inventory)) return 0;
    for (BOOT_U32 cpu = 1; cpu < auth->Cpus; ++cpu) {
        BOOT_U64 base = X86_AP_BASE + cpu * X86_AP_STRIDE;
        if (!x86_ap_descriptor_verify((const X86_KERNEL_SEGMENTS *)(base + 48 * 4096), cpu)) return 0;
        for (BOOT_U32 page = 0; page < 64; ++page) {
            if (x86_ap_page(auth->Cpus, cpu * 64ULL + page) == UINT32_MAX) continue;
            const volatile unsigned char *bytes = (const volatile unsigned char *)(base + page * 4096ULL);
            for (BOOT_U32 byte = page == 48 ? sizeof(X86_KERNEL_SEGMENTS) : 0; byte < 4096; ++byte)
                if (bytes[byte]) return 0;
        }
    }
    return 1;
}

// ====== report_private ======

static int report_private(BOOT_U32 cpus) {
    return kernel_text("KERNEL_AP_CPUS: ") && kernel_number(cpus) &&
        kernel_text("\nKERNEL_AP_BASE: ") && kernel_number(X86_AP_BASE) &&
        kernel_text("\nKERNEL_AP_DATA: ") && kernel_number(resources.Authority.Data.Base) &&
        kernel_text("\nKERNEL_AP_DATA_PAGES: ") && kernel_number(resources.Authority.Data.Pages) &&
        kernel_text("\nKERNEL_AP_TABLES: ") && kernel_number(resources.Authority.Tables.Base) &&
        kernel_text("\nKERNEL_AP_TABLE_PAGES: ") && kernel_number(resources.Authority.Tables.Pages) &&
        kernel_text("\nKERNEL_AP_DATA_ID: ") && kernel_number(resources.Authority.Data.Id) &&
        kernel_text("\nKERNEL_AP_TABLE_ID: ") && kernel_number(resources.Authority.Tables.Id) &&
        kernel_text("\nKERNEL_AP_POOL: ") && kernel_number(resources.Window ? (BOOT_U64)resources.Window->Pool : 0) &&
        kernel_text("\nKERNEL_AP_BUDGET_BEFORE: ") && kernel_number(resources.Budget) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_AP_RESOURCES_PREPARED");
}

// ====== contexts_private ======

static int contexts_private(BOOT_U32 cpus) {
    BOOT_U32 index = 1;
    for (; index < cpus; ++index) if (!x86_percpu_wait_ap_prepare(index)) break;
    if (index != cpus) {
        while (index > 1) if (!x86_percpu_wait_ap_cancel(--index)) ks_fail_stop("AP_CONTEXT_ROLLBACK_FAILED");
        return 0;
    }
    for (index = 1; index < cpus; ++index)
        if (!x86_percpu_wait_ap_valid(index)) ks_fail_stop("AP_CONTEXT_VERIFY_FAILED");
    return 1;
}

// ====== contexts_report_private ======

static int contexts_report_private(BOOT_U32 cpus) {
    const X86_CPU_LOCAL *first = x86_percpu_ap(1);
    return first && kernel_text("KERNEL_AP_CONTEXTS: ") && kernel_number((BOOT_U64)first) &&
        kernel_text("\nKERNEL_AP_CONTEXT_COUNT: ") && kernel_number(cpus - 1) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_AP_CONTEXTS_PREPARED");
}

// ====== x86_ap_resources_preflight ======

const X86_AP_RESOURCES *x86_ap_resources_preflight(void) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    const X86_CPU_IDENTITIES *ids = x86_cpu_identities();
    if (ks_irq_state() || !bsp || bsp->Locks.Depth || x86_percpu_ap_closed() || !ids || ids->Count < 2 ||
        ids->Count != resources.Authority.Cpus || resources.Committed || !verify_private()) return NULL;
    for (uint32_t index = 1; index < ids->Count; ++index)
        if (!x86_percpu_wait_ap_valid(index)) return NULL;
    return &resources;
}

// ====== x86_ap_resources_joint_seal ======

int x86_ap_resources_joint_seal(const X86_AP_RESOURCES *expected) {
    return expected == &resources && xj_retained(expected) && x86_ap_resources_preflight() == expected &&
        x86_ap_commit(&resources) && x86_percpu_ap_seal(resources.Authority.Cpus);
}

// ====== x86_ap_resources_committed_valid ======

int x86_ap_resources_committed_valid(const X86_AP_RESOURCES *expected) {
    return expected == &resources && !ks_irq_state() && ks_boot_cpu() && !x86_percpu_ap_closed() &&
        xj_retained(expected) && resources.Committed == 1 &&
        verify_private() && x86_percpu_ap_sealed(resources.Authority.Cpus);
}

// ====== x86_ap_resources_joint_ready ======

int x86_ap_resources_joint_ready(void) { return x86_ap_resources_committed_valid(&resources); }

// ====== x86_ap_resources_commit ======

int x86_ap_resources_commit(void) {
    uint64_t saved_if = ks_irq_save();
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    const X86_CPU_IDENTITIES *identities = x86_cpu_identities();
    if (!bsp || bsp->Locks.Depth || !identities || identities->Count < 2 ||
        identities->Count != resources.Authority.Cpus || resources.Committed) {
        ks_irq_restore(saved_if);
        return 0;
    }
    for (BOOT_U32 index = 1; index < identities->Count; ++index) {
        if (!x86_percpu_wait_ap_valid(index)) {
            ks_irq_restore(saved_if);
            return 0;
        }
    }
    if (!x86_ap_commit(&resources)) { ks_irq_restore(saved_if); return 0; }
    if (!x86_percpu_ap_seal(identities->Count)) ks_fail_stop("AP_COMMIT_FAILED");
    ks_irq_restore(saved_if);
    return 1;
}

// ====== start_private ======

static int start_private(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory) {
    const X86_CPU_IDENTITIES *identities = x86_cpu_identities();
    if (started || !identities || !context || !window || !inventory) return 0;
    BOOT_U32 cpus = identities->Count;
    if (cpus == 1) { started = 1; return report_private(cpus); }
    if (!x86_ap_bind(context, window, lapic, inventory, x86_paging_inventory_capture_lapic, cpus, &resources) || !verify_private()) return 0;
#ifdef KERNEL_TEST_SYNC
    if (!x86_ap_unbind(&resources) || x86_ap_owner() ||
        !kernel_log(0, "KERNEL_AP_ROLLBACK_OK") ||
        !x86_ap_bind(context, window, lapic, inventory, x86_paging_inventory_capture_lapic, cpus, &resources) || !verify_private()) return 0;
#endif
    if (!contexts_private(cpus)) {
        if (!x86_ap_unbind(&resources)) ks_fail_stop("AP_RESOURCES_ROLLBACK_FAILED");
        return 0;
    }
#ifdef KERNEL_TEST_SYNC
    for (BOOT_U32 index = cpus; index > 1; --index)
        if (!x86_percpu_wait_ap_cancel(index - 1)) ks_fail_stop("AP_CONTEXT_ROLLBACK_FAILED");
    if (!kernel_log(0, "KERNEL_AP_CONTEXT_ROLLBACK_OK") || !contexts_private(cpus)) return 0;
#endif
    if (!contexts_report_private(cpus) || !report_private(cpus)) return 0;
#ifdef KERNEL_TEST_AP_COMMIT
    if (!x86_ap_resources_commit() || x86_ap_resources_commit()) ks_fail_stop("AP_COMMIT_FAILED");
    if (x86_ap_unbind(&resources) || x86_percpu_wait_ap_cancel(1) ||
        !verify_private() || !kernel_log(0, "KERNEL_AP_COMMIT_RETAINED")) ks_fail_stop("AP_COMMIT_RELEASED");
#endif
#ifdef KERNEL_TEST_AP_RESOURCE_FAULT
    if (!kernel_log(1, "KERNEL_AP_FAULT_ARMED")) return 0;
#if KERNEL_TEST_AP_RESOURCE_FAULT == 1
    volatile unsigned char value = *(volatile unsigned char *)(X86_AP_BASE + X86_AP_STRIDE);
    (void)value;
#else
    ((void (*)(void))(X86_AP_BASE + X86_AP_STRIDE + 4096))();
#endif
    ks_fail_stop("AP_EXPECTED_FAULT_MISSING");
#else
    started = 1;
    return 1;
#endif
}

// ====== x86_ap_resources_start ======

int x86_ap_resources_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory) {
    uint64_t saved_if = ks_irq_save();
    int result = start_private(context, window, lapic, inventory);
    ks_irq_restore(saved_if);
    return result;
}
