#include "batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"

XAT_RECORD xat_second_record;
XAT_PREEMPT xat_preempt_second;
uint64_t xat_arrived[XAT_APS];
#ifdef KERNEL_TEST_AP_FOUR
static XAT_RECORD more_records[2];
static XAT_PREEMPT more_preempt[2];
#endif

// ====== xat_record_at ======

XAT_RECORD *xat_record_at(uint32_t cpu)
{
    if (cpu < 1 || cpu > XAT_APS) xat_abort();
    if (cpu == 1) return &xat_record;
    if (cpu == 2) return &xat_second_record;
#ifdef KERNEL_TEST_AP_FOUR
    return &more_records[cpu - 3];
#else
    xat_abort();
#endif
}

// ====== xat_preemption_at ======

XAT_PREEMPT *xat_preemption_at(uint32_t cpu)
{
    if (cpu < 1 || cpu > XAT_APS) xat_abort();
    if (cpu == 1) return &xat_preempt;
    if (cpu == 2) return &xat_preempt_second;
#ifdef KERNEL_TEST_AP_FOUR
    return &more_preempt[cpu - 3];
#else
    xat_abort();
#endif
}

// ====== xat_cpu_record ======

XAT_RECORD *xat_cpu_record(void) { return xat_record_at(xis_native_index()); }

// ====== xat_preemption_record ======

XAT_PREEMPT *xat_preemption_record(void) { return xat_preemption_at(xis_native_index()); }

// ====== xat_concurrent_barrier ======

void xat_concurrent_barrier(uint32_t index)
{
    if (index >= XAT_TASKS || xis_native_index() != XAT_CPU(index) || !xat_task_valid()) xat_abort();
    if (index & 1) return;
    uint32_t start, now, slot = XAT_CPU(index) - 1;
    if (ks_atomic_load_acquire(&xat_arrived[slot]) || !xis_native_now(NULL,&start)) xat_abort();
    ks_atomic_store_release(&xat_arrived[slot],1);
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        uint32_t ready = 0;
        for (uint32_t peer = 0; peer < XAT_APS; ++peer)
            ready += ks_atomic_load_acquire(&xat_arrived[peer]) == 1;
        if (ready == XAT_APS) return;
        if (!xis_native_now(NULL,&now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
            xat_abort();
        ks_pause();
    }
    xat_abort();
}
