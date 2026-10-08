#include "../watchdog.h"
#include "arch/x86/kernel/interrupts/return/table.h"

// ====== x86_watchdog_recovery_status ======

BOOT_U32 x86_watchdog_recovery_status(void)
{
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 flags = 0, failure = x86_watchdog_failure();
    if ((owner->Self && owner->Self != (BOOT_U64)owner) || owner->Closed > 1 ||
        owner->Armed > 1 || owner->Error > 1 || owner->Acquired > 1 || owner->RouteOwned > 1 ||
        owner->Ready > 1 || owner->Token.Reserved || (!owner->Token.Id && owner->Token.Vector) ||
        (owner->Token.Id && owner->Token.Vector != 0x40)) { return X86_WATCHDOG_RETAIN; }
    if (!owner->Closed && (((owner->Acquired || owner->RouteOwned || owner->Token.Id) &&
        owner->Timer >= ((owner->Id >> 8) & 31)+1) ||
        ((owner->RouteOwned || owner->Ready || owner->Token.Id) && (owner->Pin < 16 || owner->Pin >= 24)))) {
        return X86_WATCHDOG_RETAIN;
    }
    flags = (owner->Self ? 1u : 0u) | (owner->Closed << 1) | (owner->Armed << 2) |
        (owner->Error << 3) | (owner->Acquired << 4) | (owner->RouteOwned << 5) |
        (owner->Ready << 6) | (owner->Token.Id ? 128u : 0u);
    /* Closed is the verified release record. Env lifetime ends at close:
     * never dereference its pointees for a released/unowned status query. */
    if (owner->Self && !owner->Closed && (!x86_watchdog_valid() ||
        !x86_page_window_idle_valid(owner->Env.Window) ||
        (owner->Token.Id && !x86_irq_registry_token_live(x86_irq_registry(),&owner->Token)))) {
        return X86_WATCHDOG_RETAIN;
    }
    if (!owner->Closed && owner->Count == ~0u && !failure) { failure = X86_WATCHDOG_WAIT_STATE; }
    return x86_watchdog_recovery_policy(flags,failure,
        x86_watchdog_cleanup_failure(),x86_watchdog_recovery_attempts());
}

// ====== x86_watchdog_recover ======

/* Explicit one-attempt API. No IRQ ack, mask override, arm, HLT or reopen.
 * The existing close performs all restoration and release readbacks. */
BOOT_U32 x86_watchdog_recover(void)
{
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 status = x86_watchdog_recovery_status();
    if (status != X86_WATCHDOG_RETRY_CANDIDATE) { return status; }
    if (!x86_watchdog_recovery_claim()) { return X86_WATCHDOG_EXHAUSTED; }
    if (!x86_lapic_idle(owner->Env.Mapping) ||
        !x86_paging_inventory_verify_lapic(owner->Env.Context,owner->Env.Window->Pool,
            owner->Env.Mapping,owner->Env.Inventory) ||
        ((owner->Acquired || owner->RouteOwned || owner->Token.Id) && !x86_watchdog_device(7)) ||
        (owner->RouteOwned && !x86_watchdog_route(4)) ||
        !x86_page_window_idle_valid(owner->Env.Window) || !x86_lapic_idle(owner->Env.Mapping)) {
        return x86_watchdog_recovery_attempts() == X86_WATCHDOG_RECOVERY_LIMIT ?
            X86_WATCHDOG_EXHAUSTED : X86_WATCHDOG_RETAIN;
    }
    if (!x86_watchdog_finish_cleanup()) {
        return x86_watchdog_recovery_attempts() == X86_WATCHDOG_RECOVERY_LIMIT ?
            X86_WATCHDOG_EXHAUSTED : X86_WATCHDOG_RETAIN;
    }
    return x86_watchdog_recovery_status();
}
