#include "transport.h"
#include "arch/x86/kernel/interrupts/apic/icr/icr.h"
typedef struct { uint32_t Start, Last, Polls; uint64_t Outcome; } WAIT;

// ====== poll_private ======

static int poll_private(const XP_PANIC_TRANSPORT *t, volatile WAIT *wait) {
    uint32_t now;
    if (wait->Polls >= t->MaxPolls) {
        wait->Outcome |= XP_PANIC_POLL_LIMIT;
        if (wait->Last == wait->Start) wait->Outcome |= XP_PANIC_CLOCK_FAILED;
        return 0;
    }
    ++wait->Polls;
    if (!t->Now(t->Context, &now)) { wait->Outcome |= XP_PANIC_CLOCK_FAILED; return 0; }
    now &= t->Mask; wait->Last = now;
    if (((now - wait->Start) & t->Mask) >= t->Ticks) { wait->Outcome |= XP_PANIC_DEADLINE; return 0; }
    return 1;
}

// ====== budget_private ======

typedef struct { const XP_PANIC_TRANSPORT *Transport; volatile WAIT *Wait; } BUDGET;

static int budget_private(void *budget) {
    const BUDGET *b = budget;
    return poll_private(b->Transport, b->Wait);
}

// ====== send_private ======

// No ESR access and no wait after the send: the panic path only needs the ICR idle
// before each NMI, and the ACK mask (not the APIC) decides whether a CPU stopped.
static int send_private(const XP_PANIC_TRANSPORT *t, volatile WAIT *wait, uint32_t id) {
    uint64_t bits = xp_panic_nmi_bits(id);
    if (bits == UINT64_MAX) { wait->Outcome |= XP_PANIC_TRANSPORT_FAILED; return 0; }
    BUDGET budget = {t, wait};
    X86_ICR_PORT port = {t->Read, t->Write, budget_private, t->Pause, t->Context, &budget};
    uint32_t status = x86_icr_send(&port, (uint32_t)(bits >> 56), (uint32_t)bits, 0);
    if (status == X86_ICR_OK) return 1;
    if (status != X86_ICR_STOPPED) wait->Outcome |= XP_PANIC_TRANSPORT_FAILED;
    return 0;
}

// ====== xp_panic_transport_run ======

int xp_panic_transport_run(uint32_t index, const uint32_t *ids, uint32_t count, const XP_PANIC_TRANSPORT *t) {
    if (!ids || !t || !count || count > 64 || index >= count ||
        !t->Read || !t->Write || !t->Now || !t->Pause ||
        (t->Mask != 0xffffffU && t->Mask != UINT32_MAX) || !t->Ticks ||
        t->Ticks >= t->Mask / 2 || (!t->MaxPolls || t->MaxPolls > 1000000U) ||
        xp_panic_value(XP_PANIC_KNOWN) != (count == 64 ? UINT64_MAX : (1ULL << count) - 1) ||
        xp_panic_value(XP_PANIC_OWNER) != (uint64_t)index + 1 || xp_panic_value(XP_PANIC_PHASE) != 1) return 0;
    for (uint32_t cpu = 0; cpu < count; ++cpu) {
        if (ids[cpu] >= 255) return 0;
        for (uint32_t prior = 0; prior < cpu; ++prior) if (ids[prior] == ids[cpu]) return 0;
    }
    uint64_t targets = xp_panic_value(XP_PANIC_TARGETS);
    if (!targets) return (int)xp_panic_finish(index, 0);
    uint32_t start;
    if (!t->Now(t->Context, &start)) return (int)xp_panic_finish(index, XP_PANIC_CLOCK_FAILED);
    volatile WAIT wait;
    wait.Start = start & t->Mask; wait.Last = wait.Start; wait.Polls = 0; wait.Outcome = 0;
    for (uint32_t cpu = 0; cpu < count; ++cpu) {
        uint64_t bit = 1ULL << cpu;
        if ((targets & bit) && !(xp_panic_value(XP_PANIC_ACK) & bit) && !send_private(t, &wait, ids[cpu]))
            return (int)xp_panic_finish(index, wait.Outcome);
    }
    while ((xp_panic_value(XP_PANIC_ACK) & targets) != targets && poll_private(t, &wait)) t->Pause(t->Context);
    return (int)xp_panic_finish(index, wait.Outcome);
}
