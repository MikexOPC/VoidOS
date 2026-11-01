#include "clock.h"
#include "../internal.h"

typedef struct {
    KERNEL_LOCK_WAIT  Wait;
    X86_PM_CLOCK  Source;
    uint64_t  Context;
} CPU_WAIT;
static CPU_WAIT slots[X86_PERCPU_CAPACITY];

// ====== source_valid_private ======

static int source_valid_private(const X86_PM_CLOCK *source) {
    return source->Ready == 1 && source->Length >= 116 && source->Length <= 4096 &&
        source->Physical >= 4096 && source->Physical <= UINT64_MAX - source->Length &&
        x86_pm_clock_port(source->Port, 0, 4) &&
        (source->Mask == 0xffffffU || source->Mask == UINT32_MAX);
}

// ====== read_private ======

static int read_private(void *context, uint32_t *value) {
    const X86_PM_CLOCK *source = context;
    if (!source || !value || !source_valid_private(source)) return 0;
    // ACPI 6.5 4.8.3.3 Table 4.14: read-only PM_TMR, dword, bits [23:0]/[31:0].
    // Port and width come from validated FADT; no lock, GS access or IRQ dependency.
    *value = x86_pm_read((unsigned short)source->Port) & source->Mask;
    return 1;
}

// ====== progress_private ======

static int progress_private(const KERNEL_CLOCK *clock, uint32_t max_polls) {
    uint32_t start, now;
    if (!kernel_clock_now(clock, &start)) return 0;
    if (ks_irq_state()) ks_fail_stop("PERCPU_CLOCK_IRQ_ENABLED");
    uint32_t limit = max_polls < 4096U ? max_polls : 4096U;
    for (uint32_t poll = 0; poll < limit; ++poll) {
        if (!kernel_clock_now(clock, &now)) return 0;
        if (ks_irq_state()) ks_fail_stop("PERCPU_CLOCK_IRQ_ENABLED");
        uint32_t delta = kernel_clock_delta(clock, start, now);
        if (delta) return delta < clock->Mask / 2;
        ks_pause();
    }
    return 0;
}

// ====== bind_private ======

static int bind_private(const X86_PM_CLOCK *source, uint32_t ticks, uint32_t max_polls) {
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (!owner || !ks_span(source, sizeof(*source)) ||
        ks_overlap(source, sizeof(*source), slots, sizeof(slots)) ||
        ks_overlap(source, sizeof(*source), owner, sizeof(*owner)) ||
        owner->Locks.Depth || owner->Locks.Wait || !source_valid_private(source) ||
        !ticks || ticks >= source->Mask / 2 || !max_polls) return 0;
    CPU_WAIT *slot = &slots[owner->Index];
    for (uint32_t byte = 0; byte < sizeof(*slot); ++byte)
        if (((const unsigned char *)slot)[byte]) return 0;
    slot->Source = *source;
    slot->Wait = (KERNEL_LOCK_WAIT){{read_private, &slot->Source, source->Mask, KERNEL_PM_TIMER_HZ}, ticks, max_polls};
    if (!progress_private(&slot->Wait.Clock, max_polls) ||
        !kernel_lock_context_bind_wait((KERNEL_LOCK_CONTEXT *)&owner->Locks, &slot->Wait)) {
        for (uint32_t byte = 0; byte < sizeof(*slot); ++byte) ((unsigned char *)slot)[byte] = 0;
        return 0;
    }
    slot->Context = (uintptr_t)&owner->Locks;
    return 1;
}

// ====== x86_percpu_wait_bind ======

int x86_percpu_wait_bind(const X86_PM_CLOCK *source, uint32_t ticks, uint32_t max_polls) {
    uint64_t saved_if = ks_irq_save();
    int result = bind_private(source, ticks, max_polls);
    ks_irq_restore(saved_if);
    return result;
}

// ====== x86_percpu_wait_current ======

const KERNEL_LOCK_WAIT *x86_percpu_wait_current(void) {
    uint64_t saved_if = ks_irq_save();
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    const KERNEL_LOCK_WAIT *result = NULL;
    if (owner) {
        const CPU_WAIT *slot = &slots[owner->Index];
        if (slot->Context == (uintptr_t)&owner->Locks && owner->Locks.Wait == &slot->Wait &&
            slot->Wait.Clock.Read == read_private && slot->Wait.Clock.Context == &slot->Source &&
            slot->Wait.Clock.Mask == slot->Source.Mask && slot->Wait.Clock.Hz == KERNEL_PM_TIMER_HZ &&
            slot->Wait.Ticks && slot->Wait.Ticks < slot->Source.Mask / 2 && slot->Wait.MaxPolls &&
            source_valid_private(&slot->Source)) result = &slot->Wait;
    }
    ks_irq_restore(saved_if);
    return result;
}

// ====== x86_percpu_wait_ap_prepare ======

int x86_percpu_wait_ap_prepare(uint32_t index) {
    if (ks_irq_state() || !ks_boot_cpu() || !index || index >= X86_PERCPU_CAPACITY) return 0;
    const KERNEL_LOCK_WAIT *bsp = x86_percpu_wait_current();
    if (!bsp || x86_percpu_bsp()->Locks.Depth) return 0;
    CPU_WAIT *slot = &slots[index];
    for (uint32_t byte = 0; byte < sizeof(*slot); ++byte)
        if (((const unsigned char *)slot)[byte]) return 0;
    slot->Source = slots[0].Source;
    slot->Wait = (KERNEL_LOCK_WAIT){{read_private, &slot->Source, bsp->Clock.Mask, bsp->Clock.Hz}, bsp->Ticks, bsp->MaxPolls};
    if (!progress_private(&slot->Wait.Clock, slot->Wait.MaxPolls) || !x86_percpu_ap_prepare(index, &slot->Wait)) {
        for (uint32_t byte = 0; byte < sizeof(*slot); ++byte) ((unsigned char *)slot)[byte] = 0;
        return 0;
    }
    slot->Context = (uintptr_t)&x86_percpu_ap(index)->Locks;
    return 1;
}

// ====== x86_percpu_wait_ap_valid ======

int x86_percpu_wait_ap_valid(uint32_t index) {
    const X86_CPU_LOCAL *owner = x86_percpu_ap(index);
    const KERNEL_LOCK_WAIT *bsp = x86_percpu_wait_current();
    if (!owner || !bsp) return 0;
    const CPU_WAIT *slot = &slots[index];
    return slot->Context == (uintptr_t)&owner->Locks && owner->Locks.Wait == &slot->Wait &&
        slot->Wait.Clock.Read == read_private && slot->Wait.Clock.Context == &slot->Source &&
        slot->Wait.Clock.Mask == bsp->Clock.Mask && slot->Wait.Clock.Hz == bsp->Clock.Hz &&
        slot->Wait.Ticks == bsp->Ticks && slot->Wait.MaxPolls == bsp->MaxPolls &&
        source_valid_private(&slot->Source) && slot->Source.Physical == slots[0].Source.Physical &&
        slot->Source.Length == slots[0].Source.Length && slot->Source.Port == slots[0].Source.Port &&
        slot->Source.Mask == slots[0].Source.Mask;
}

// ====== x86_percpu_wait_ap_cancel ======

int x86_percpu_wait_ap_cancel(uint32_t index) {
    if (!x86_percpu_wait_ap_valid(index) || !x86_percpu_ap_cancel(index)) return 0;
    CPU_WAIT *slot = &slots[index];
    for (uint32_t byte = 0; byte < sizeof(*slot); ++byte) ((unsigned char *)slot)[byte] = 0;
    return 1;
}
