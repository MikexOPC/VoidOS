#include "watchdog.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "lib/memory/address_ranges.h"

// ====== source_valid ======

static int source_valid(const volatile BOOT_U32 *source, const volatile BOOT_U32 *stop)
{
    BOOT_U64 address = (BOOT_U64)source, request = (BOOT_U64)stop;
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    if (address < 4096 || (address & 7) || address > (1ULL << 47) - 8 ||
        os_address_ranges_overlap(address,8,(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap(address,8,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap(address,8,X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(address,8,128ULL << 39,1ULL << 39)) { return 0; }
    return !stop || (request >= 4096 && !(request & 3) && request <= (1ULL << 47) - 4 &&
        !os_address_ranges_overlap(request,4,(BOOT_U64)owner,sizeof(*owner)) &&
        !os_address_ranges_overlap(request,4,address,8) &&
        !os_address_ranges_overlap(request,4,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(request,4,X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(request,4,128ULL << 39,1ULL << 39));
}

// ====== x86_watchdog_wait ======

/* v0 waits until the independent one-shot is acknowledged as well, so
 * cancellation cannot strand a watchdog IRQ in IRR or free a live handler.
 * Interrupt context saved by the existing Assembly backend; no C at IF=1.
 * Failure model: missing LAPIC delivery, functioning HPET/controller/BSP. */
BOOT_U32 x86_watchdog_wait(const volatile BOOT_U32 *source, BOOT_U32 goal,
    const volatile BOOT_U32 *stop)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 next, slept, request;
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
    if (!x86_watchdog_device(2)) { return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_ARM); }
    slept = x86_irq_sleep(&owner->Count,next,8);
    if (!slept || slept > 9 || owner->Count != next || owner->Error || owner->Armed ||
        !x86_page_window_idle_valid(owner->Env.Window) || !x86_lapic_idle(owner->Env.Mapping)) {
        return (BOOT_U32)x86_watchdog_fail(X86_WATCHDOG_WAKE);
    }
    request = stop ? *stop : 0;
    owner->Result = x86_watchdog_classify(source[0],source[1],goal,request);
    if (owner->Result == X86_WATCHDOG_TIMEOUT) { x86_watchdog_fail(X86_WATCHDOG_DEADLINE); }
    if (owner->Result == X86_WATCHDOG_FAULT) { x86_watchdog_fail(X86_WATCHDOG_CLASSIFY); }
    return owner->Result == X86_WATCHDOG_EVENT || owner->Result == X86_WATCHDOG_STOP ? slept : 0;
}
