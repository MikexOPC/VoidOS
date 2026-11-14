#include "bridge.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"

extern const unsigned char kernel_user_bridge[];
extern const BOOT_U64 kernel_user_bridge_length;

static X86_PAGE_WINDOW *owner;
static BOOT_U64 budget;
static int pending, ready;

// ====== x86_shell_bridge_open ======

int x86_shell_bridge_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    const X86_PROCESS_IMAGE image = {kernel_user_bridge, kernel_user_bridge_length};
    if (!x86_user_mode_allowed()) return 1;
    if (owner || !kernel_physical_budget_read(window->Pool, &budget) ||
        !x86_process_resident_open(window, timer, &image)) return 0;
    owner = window;
    return 1;
}

// ====== kernel_shell_bridge_status ======

int kernel_shell_bridge_status(void)
{
    if (!owner || pending || x86_scheduler_index() != 2 || !x86_scheduler_driver_task_valid()) return 0;
    pending = 1;
    x86_scheduler_yield();
    if (pending) kernel_scheduler_fail_stop();
    return ready;
}

// ====== x86_shell_bridge_dispatch ======

void x86_shell_bridge_dispatch(void)
{
    if (!pending) return;
    const unsigned char request[2] = {1, 1};
    unsigned char response[2];
    X86_PROCESS_RESULT result;
    const X86_IRQ_REGISTRY *registry = x86_irq_registry();
    BOOT_U32 mask = 0;
    if (!owner || !x86_irq_ready() || !registry) kernel_scheduler_fail_stop();
    for (BOOT_U32 index = 0; index < X86_IRQ_DEVICE_SLOTS; ++index) {
        if (registry->Slots[index].Id) mask |= 1U << index;
    }
    ready = x86_process_resident_request(request, response, mask, &result, 1) &&
        response[0] == 1 && response[1] == 0;
    if (!x86_irq_ready()) kernel_scheduler_fail_stop();
    pending = 0;
}

// ====== x86_shell_bridge_close ======

int x86_shell_bridge_close(void)
{
    if (!owner) return 1;
    if (pending || !x86_process_resident_close() ||
        !kernel_physical_budget_matches(owner->Pool, budget)) return 0;
    owner = 0;
    ready = 0;
    return 1;
}
