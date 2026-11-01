#include "runtime.h"
#include "../platform.h"
#include "arch/x86/kernel/cpu/percpu/clock/clock.h"
#include <assert.h>
#include <stdio.h>

// ====== runtime_private ======

void runtime_private(uint32_t count, int accept, int sparse) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp(), *aps[64] = {0};
    uint32_t ids[64];
    for (uint32_t index = 0; index < count; ++index) ids[index] = index;
    if (sparse) { assert(count == 4); ids[1] = 7; ids[2] = 42; ids[3] = 254; }
    for (uint32_t index = 1; index < count; ++index) aps[index] = x86_percpu_ap(index);
    assert(bsp && !model_if && !x86_percpu_ap_closed() && !x86_percpu_ap_phase(1));
    ids[1] = 0;
    assert(!x86_percpu_ap_access_close(count, ids) && !x86_percpu_ap_closed()); ids[1] = sparse ? 7 : 1;
    assert(!x86_percpu_ap_access_close(count, NULL) && !x86_percpu_ap_access_close(count - 1, ids));
    assert(x86_percpu_ap_access_close(count, ids) && !x86_percpu_ap_access_close(count, ids));
    assert(x86_percpu_ap_closed() == count && !x86_percpu_ap(1) && !x86_percpu_wait_ap_valid(1));
    for (uint32_t index = 1; index < count; ++index) {
        model_bsp = 0; model_id = ids[index]; model_gs = model_target = (uint64_t)aps[index];
        const KERNEL_LOCK_WAIT *wait = aps[index]->Locks.Wait;
        assert(!kernel_lock_current_context() && !x86_percpu_wait_current() && !x86_percpu_ap_accept(index));
        uint32_t reads = model_self_reads;
        model_gs = 8;
        assert(!x86_percpu_ap_enter(index, ids[index], (uint64_t)aps[index], wait) && model_self_reads == reads);
        model_gs = model_target;
        assert(!x86_percpu_ap_enter(index, 255, (uint64_t)aps[index], wait));
        assert(!x86_percpu_ap_enter(index, ids[index], (uint64_t)aps[index] + 64, wait));
        assert(!x86_percpu_ap_enter(index, ids[index], (uint64_t)aps[index], NULL));
        assert(x86_percpu_ap_enter(index, ids[index], (uint64_t)aps[index], wait));
        assert(x86_percpu_ap_phase(index) == 1 && !kernel_lock_current_context() && !x86_percpu_wait_current());
        assert(!x86_percpu_ap_enter(index, ids[index], (uint64_t)aps[index], wait));
        if (!accept) break;
        X86_CPU_LOCAL *owner = (X86_CPU_LOCAL *)aps[index];
        owner->Locks.Frames[7].Token = 1;
        assert(!x86_percpu_ap_accept(index)); owner->Locks.Frames[7].Token = 0;
        owner->Locks.Wait = NULL;
        assert(!x86_percpu_ap_accept(index)); owner->Locks.Wait = wait;
        model_id = 0; reads = model_self_reads;
        assert(!x86_percpu_ap_accept(index) && model_self_reads == reads); model_id = ids[index];
        assert(x86_percpu_ap_accept(index) && !x86_percpu_ap_accept(index) && x86_percpu_ap_phase(index) == 2);
        assert(kernel_lock_current_context() == &owner->Locks && x86_percpu_wait_current() == wait);
        assert(!x86_percpu_wait_ap_prepare(index));
        uint32_t now;
        assert(kernel_clock_now(&wait->Clock, &now));
        KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
        assert(kernel_lock_initialize(&lock, 100));
        model_if = 0x200;
        assert(kernel_lock_acquire(&lock, kernel_lock_current_context(), &guard) && !model_if);
        assert(kernel_lock_release(&owner->Locks, &guard) && model_if == 0x200);
        model_if = 0;
        assert(owner->Locks.NextToken == 2 && !owner->Locks.Depth && kernel_lock_current_context() == &owner->Locks);
    }
    if (accept) {
        model_id = ids[1]; model_gs = model_target = (uint64_t)aps[2];
        uint32_t reads = model_self_reads;
        assert(!kernel_lock_current_context() && model_self_reads == reads);
        if (accept == 2) {
            assert(!x86_percpu_ap_reject(2));
            model_gs = model_target = (uint64_t)aps[1];
            assert(x86_percpu_ap_reject(1) && !x86_percpu_ap_reject(1));
            assert(!kernel_lock_current_context() && !x86_percpu_wait_current() && !x86_percpu_ap_accept(1));
        }
    }
    model_bsp = 1; model_id = 0; model_gs = model_target = (uint64_t)bsp;
    assert(kernel_lock_current_context() == &bsp->Locks && !x86_percpu_ap(1) && !x86_percpu_wait_ap_cancel(1));
    assert(x86_percpu_ap_phase(1) == (accept == 2 ? 3U : accept ? 2U : 1U));
    printf("PERCPU_RUNTIME_PASS: %u prepared contexts, staged provider %s, IRQ/ID/GS/clock/lock and remote getter gates; CPU modeled\n",
        count, accept ? "accepted" : "closed");
}
