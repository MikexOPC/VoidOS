#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"

// ====== ks_fail_stop ======

__declspec(noreturn) void ks_fail_stop(const char *reason) {
    (void)ks_irq_save();
    kernel_panic(reason);
}
