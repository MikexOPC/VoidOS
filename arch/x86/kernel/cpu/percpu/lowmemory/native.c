#include "lowmemory.h"
#include "kernel/diagnostics/log.h"
static X86_AP_LOW_MEMORY startup_page;

// ====== x86_ap_low_current ======

const X86_AP_LOW_MEMORY *x86_ap_low_current(void) {
    return x86_ap_low_valid(&startup_page) ? &startup_page : 0;
}

// ====== x86_ap_low_start ======

int x86_ap_low_start(KERNEL_PHYSICAL_ALLOCATOR *pool) {
    if (!x86_ap_low_reserve(pool, &startup_page)) return 0;
    if (!kernel_text("KERNEL_AP_LOW_OWNER: ") || !kernel_number((BOOT_U64)&startup_page) ||
        !kernel_text("\nKERNEL_AP_LOW_POOL: ") || !kernel_number((BOOT_U64)pool) ||
        !kernel_text("\nKERNEL_AP_LOW_BASE: ") || !kernel_number(startup_page.Lease.Base) ||
        !kernel_text("\nKERNEL_AP_LOW_ID: ") || !kernel_number(startup_page.Lease.Id) ||
        !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    return kernel_log(startup_page.State == 2 ? 0 : 2,
        startup_page.State == 2 ? "KERNEL_AP_LOW_RESERVED" : "KERNEL_AP_LOW_UNAVAILABLE");
}
