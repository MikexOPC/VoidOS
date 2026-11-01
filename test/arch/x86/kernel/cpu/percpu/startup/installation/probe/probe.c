#include "probe.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include "arch/x86/kernel/cpu/percpu/identity/identity.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_AP_LAUNCH
#include "arch/x86/kernel/cpu/percpu/startup/startup.h"
#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#endif
#if KERNEL_TEST_AP_TRANSITION >= 6
#include "test/arch/x86/kernel/cpu/percpu/startup/commit/probe/probe.h"
#endif
static XB_INSTALLATION installation;
extern const unsigned char xs_blob_template[4096];
extern const unsigned char xs_installation_terminal[];

// ====== xb_installation_probe ======

int xb_installation_probe(X86_AP_TRANSITION *t, int rollback) {
    const X86_CPU_IDENTITIES *ids = x86_cpu_identities();
    const X86_CPU_LOCAL *local = x86_percpu_ap(1);
    uint64_t budget;
    XS_ENTRY_RECORD record;
    if (!ids || ids->Count < 2 || !local || !kernel_physical_budget_read(t->Pool, &budget)) return 0;
    XS_ENTRY_INPUT input = {1, ids->Cpus[1].Id, t->Pool->PhysicalBits, 0, 520,
        t->Code->Lease.Base, t->Data.Base, t->Tables.Base, t->Root, (uint64_t)local,
        (uint64_t)xs_installation_terminal, t->Pat};
#ifdef KERNEL_TEST_AP_LAUNCH
    if (!xs_configure(ids->Count) || !(input.Token = xs_reserve(1))) return 0;
    input.Entry = (uint64_t)xs_private_entry;
#endif
    if (!xs_entry_build(&record, input) || !xb_install(&installation, t, xs_blob_template, record) ||
        !xb_installed(&installation) || xt_verify(t) || !xt_verify_tables(t) ||
        !kernel_physical_budget_matches(t->Pool, budget)) return 0;
    if (!kernel_text("KERNEL_AP_IMAGE_OWNER: ") || !kernel_number((uint64_t)&installation) ||
        !kernel_text("\nKERNEL_AP_IMAGE_CODE_EXPECTED: ") || !kernel_number((uint64_t)installation.Code) ||
        !kernel_text("\nKERNEL_AP_IMAGE_DATA_EXPECTED: ") || !kernel_number((uint64_t)installation.Data) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_AP_IMAGE_INSTALLED_INACTIVE")) return 0;
    if (rollback && (!xb_erase(&installation) || installation.Self || !xt_verify(t) ||
        !kernel_physical_budget_matches(t->Pool, budget) ||
        !kernel_log(0, "KERNEL_AP_IMAGE_ERASED_VERIFIED"))) return 0;
#if KERNEL_TEST_AP_TRANSITION >= 6
    if (!xj_probe(&installation)) return 0;
#endif
    return 1;
}
