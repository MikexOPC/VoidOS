#include "control.h"
#include "kernel/diagnostics/log.h"
#include "../persistent/persistent.h"

// ====== x86_scheduler_report ======

int x86_scheduler_report(uint64_t root, uint64_t before, const KERNEL_PHYSICAL_ALLOCATOR *pool, uint32_t steps, uint32_t profile)
{
    BOOT_U64 after;
    if (!kernel_physical_budget_read(pool, &after)) return 0;
    if (!kernel_text("SCHEDULER_ROOT: ") || !kernel_number(root) ||
        !kernel_text("\nSCHEDULER_PTE: ") || !kernel_number((BOOT_U64)&x86_stack_entries(3)[320]) ||
        !kernel_text("\nSCHEDULER_BUDGET_BEFORE: ") || !kernel_number(before) ||
        !kernel_text("\nSCHEDULER_BUDGET_AFTER: ") || !kernel_number(after) ||
        !kernel_text("\nSCHEDULER_DISPATCHES: ") || !kernel_number(steps) || !kernel_text("\n")) return 0;
    if (!profile) return 1;
    if (profile == 5) return kernel_log(0,"KERNEL_INPUT_TASKS_COLLECTED");
    if (profile == 3) return kernel_text("SCHEDULER_PERSISTENT_DISPATCHES: ") && kernel_number(steps) &&
        kernel_text("\nSCHEDULER_PERSISTENT_CYCLES: ") && kernel_number(x86_scheduler_persistent_cycles()) &&
        kernel_text("\nSCHEDULER_PERSISTENT_BUDGET_BEFORE: ") && kernel_number(before) &&
        kernel_text("\nSCHEDULER_PERSISTENT_BUDGET_AFTER: ") && kernel_number(after) && kernel_text("\n") &&
        kernel_log(0,"KERNEL_SCHEDULER_PERSISTENT_OK");
    if (profile == 4) return kernel_text("SCHEDULER_IDLE_REPEAT_DISPATCHES: ") && kernel_number(steps) &&
        kernel_text("\nSCHEDULER_IDLE_REPEAT_BUDGET_BEFORE: ") && kernel_number(before) &&
        kernel_text("\nSCHEDULER_IDLE_REPEAT_BUDGET_AFTER: ") && kernel_number(after) && kernel_text("\n");
    return profile <= 2 &&
        kernel_text(profile == 1 ? "SCHEDULER_IRQ_DISPATCHES: " : "SCHEDULER_IDLE_DISPATCHES: ") &&
        kernel_number(steps) &&
        kernel_text(profile == 1 ? "\nSCHEDULER_IRQ_BUDGET_BEFORE: " : "\nSCHEDULER_IDLE_BUDGET_BEFORE: ") &&
        kernel_number(before) &&
        kernel_text(profile == 1 ? "\nSCHEDULER_IRQ_BUDGET_AFTER: " : "\nSCHEDULER_IDLE_BUDGET_AFTER: ") &&
        kernel_number(after) && kernel_text("\n");
}
