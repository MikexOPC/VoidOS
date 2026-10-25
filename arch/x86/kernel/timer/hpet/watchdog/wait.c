#include "watchdog.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "lib/memory/address_ranges.h"
static BOOT_U32 wake_detail;

// ====== x86_watchdog_wake_detail ======

BOOT_U32 x86_watchdog_wake_detail(void) { return wake_detail; }

// ====== source_valid ======

static int source_valid(const volatile BOOT_U32 *source, const volatile BOOT_U32 *stop)
{
    BOOT_U64 address = (BOOT_U64)source, request = (BOOT_U64)stop;
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    if (address < 4096 || (address & 7) || address > (1ULL << 47) - 8 ||
        os_address_ranges_overlap(address,8,(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap(address,8,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap(address,8,X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(address,8,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES)) { return 0; }
    return !stop || (request >= 4096 && !(request & 3) && request <= (1ULL << 47) - 4 &&
        !os_address_ranges_overlap(request,4,(BOOT_U64)owner,sizeof(*owner)) &&
        !os_address_ranges_overlap(request,4,address,8) &&
        !os_address_ranges_overlap(request,4,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(request,4,X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(request,4,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES));
}

// ====== wake_lapic_valid ======

// Only vectors41h/42h with a live registry token may remain pending in IRR.
static int wake_lapic_valid(const X86_HPET_WATCHDOG *owner)
{
    const X86_IRQ_REGISTRY *registry = x86_irq_registry();
    BOOT_U32 allowed = 0, index;
    if (!x86_irq_registry_validate(registry) || registry->Busy) return 0;
    for (index = 1; index <= 2; ++index) {
        const X86_IRQ_SUBSCRIPTION *item = &registry->Slots[index];
        X86_IRQ_TOKEN token = {item->Id, item->Vector, 0};
        if (item->Id) {
            if (item->Vector != 0x40 + index || !x86_irq_registry_token_live(registry, &token)) return 0;
            allowed |= 1U << index;
        }
    }
    return x86_lapic_idle_except(owner->Env.Mapping, allowed);
}

// ====== wait_private ======

/* v0 waits until the independent one-shot is acknowledged as well, so
 * cancellation cannot strand a watchdog IRQ in IRR or free a live handler.
 * Interrupt context saved by the existing Assembly backend; no C at IF=1.
 * Failure model: missing LAPIC delivery, functioning HPET/controller/BSP. */
static BOOT_U32 wait_private(const volatile BOOT_U32 *source, BOOT_U32 goal,
    const volatile BOOT_U32 *stop, BOOT_U32 idle)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 next, slept, request;
    wake_detail = 0;
    if (!x86_watchdog_valid() || !owner->Ready || owner->Armed || owner->Error || owner->Count == ~0U ||
        !x86_irq_registry_token_live(x86_irq_registry(),&owner->Token)) {
        return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_WAIT_STATE);
    }
    if (!source_valid(source,stop) || !goal) { return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_SOURCE); }
    owner->Result = X86_WATCHDOG_FAULT;
    if (source[1] || source[0] > goal || (stop && *stop > 1)) {
        return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_SOURCE);
    }
    next = owner->Count + 1;
    owner->Ticks = x86_watchdog_ticks(owner->Period, idle ? 5 : 100);
    if (!owner->Ticks) return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_ARM);
    if (!x86_watchdog_device(2)) { return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_ARM); }
    slept = x86_irq_sleep(&owner->Count,next,X86_WATCHDOG_WAKE_BUDGET);
    if (!slept || slept > X86_WATCHDOG_WAKE_LIMIT) wake_detail = 1;
    else if (owner->Count != next) wake_detail = 2;
    else if (owner->Error) wake_detail = 3;
    else if (owner->Armed) wake_detail = 4;
    else if (!x86_page_window_idle_valid(owner->Env.Window)) wake_detail = 5;
    else if (!wake_lapic_valid(owner)) wake_detail = 6;
    if (wake_detail) {
        return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_WAKE);
    }
    request = stop ? *stop : 0;
    owner->Result = x86_watchdog_classify(source[0],source[1],goal,request);
    if (idle && owner->Result == X86_WATCHDOG_TIMEOUT) {
        owner->Result = X86_WATCHDOG_IDLE;
        return slept;
    }
    if (owner->Result == X86_WATCHDOG_TIMEOUT) { x86_watchdog_fail(X86_WATCHDOG_DEADLINE); }
    if (owner->Result == X86_WATCHDOG_FAULT) { x86_watchdog_fail(X86_WATCHDOG_CLASSIFY); }
    return owner->Result == X86_WATCHDOG_EVENT || owner->Result == X86_WATCHDOG_STOP ? slept : 0;
}

// ====== x86_watchdog_wait ======

BOOT_U32 x86_watchdog_wait(const volatile BOOT_U32 *source, BOOT_U32 goal,
    const volatile BOOT_U32 *stop)
{
    return wait_private(source, goal, stop, 0);
}

// ====== x86_watchdog_idle ======

BOOT_U32 x86_watchdog_idle(const volatile BOOT_U32 *source)
{
    return wait_private(source, 1, NULL, 1);
}
