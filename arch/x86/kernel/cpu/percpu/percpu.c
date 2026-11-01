#include "internal.h"
#include "startup/commit/retention.h"
static X86_CPU_LOCAL areas[X86_PERCPU_CAPACITY];
static KERNEL_LOCK_CONTEXT *retired;
static uint32_t ap_sealed;
_Alignas(8) static uint64_t ap_closed, ap_phases[64];
static uint32_t ap_ids[64];
static const KERNEL_LOCK_WAIT *ap_waits[64];

// ====== empty_private ======

static int empty_private(const X86_CPU_LOCAL *owner) {
    for (uint32_t byte = 0; byte < sizeof(*owner); ++byte)
        if (((const unsigned char *)owner)[byte]) return 0;
    return 1;
}

// ====== clock_alias_private ======

static int clock_alias_private(const KERNEL_LOCK_CONTEXT *early, const X86_CPU_LOCAL *owner) {
    if (!early->Wait || !early->Wait->Clock.Context) return 0;
    uintptr_t address = (uintptr_t)early->Wait->Clock.Context;
    return (address >= (uintptr_t)early && address - (uintptr_t)early < sizeof(*early)) ||
        (address >= (uintptr_t)owner && address - (uintptr_t)owner < sizeof(*owner));
}

// ====== promote_private ======

static int promote_private(KERNEL_LOCK_CONTEXT *early) {
    X86_CPU_LOCAL *owner = &areas[0];
    if (!ks_boot_cpu() || !xp_gs_supported() || retired || !empty_private(owner) ||
        !early || early != kernel_lock_current_context() ||
        !xp_image_contains((uintptr_t)owner, sizeof(*owner)) ||
        !xp_image_contains((uintptr_t)early, sizeof(*early)) ||
        !ks_context_valid(early) || early->Depth ||
        ks_overlap(owner, sizeof(*owner), early, sizeof(*early)) ||
        (early->Wait && ks_overlap(owner, sizeof(*owner), early->Wait, sizeof(*early->Wait))) ||
        clock_alias_private(early, owner) ||
        !xp_descriptors_ready() || !ks_context_valid(early) || early->Depth) return 0;
    if (!kernel_lock_context_initialize(&owner->Locks, early->Wait)) return 0;
    owner->Locks.NextToken = early->NextToken;
    owner->Self = (uintptr_t)owner; owner->EarlyContext = (uintptr_t)early;
    owner->Index = 0; owner->State = X86_PERCPU_PREPARED;
    retired = early;
    xp_gs_write(owner->Self);
    if (xp_gs_read() != owner->Self || xp_gs_self() != owner->Self || !ks_context_valid(&owner->Locks))
        ks_fail_stop("PERCPU_GS_VERIFY_FAILED");
    for (uint32_t byte = 0; byte < sizeof(*early); ++byte) ((volatile unsigned char *)early)[byte] = 0;
    owner->State = X86_PERCPU_ACTIVE;
    return 1;
}

// ====== x86_percpu_bsp_promote ======

int x86_percpu_bsp_promote(KERNEL_LOCK_CONTEXT *early) {
    uint64_t saved_if = ks_irq_save();
    int result = promote_private(early);
    ks_irq_restore(saved_if);
    return result;
}

// ====== bsp_private ======

static const X86_CPU_LOCAL *bsp_private(void) {
    const X86_CPU_LOCAL *owner = &areas[0];
    if (owner->State != X86_PERCPU_ACTIVE || owner->Self != (uintptr_t)owner || owner->Index || owner->Reserved ||
        !retired || owner->EarlyContext != (uintptr_t)retired || retired->Self || retired->NextToken ||
        xp_gs_read() != (uintptr_t)owner || xp_gs_self() != (uintptr_t)owner ||
        !ks_context_valid(&owner->Locks)) return NULL;
    return owner;
}

// ====== x86_percpu_bsp ======

const X86_CPU_LOCAL *x86_percpu_bsp(void) {
    uint64_t saved_if = ks_irq_save();
    const X86_CPU_LOCAL *owner = ks_boot_cpu() ? bsp_private() : NULL;
    ks_irq_restore(saved_if);
    return owner;
}

// ====== x86_percpu_current_context ======

KERNEL_LOCK_CONTEXT *x86_percpu_current_context(void) {
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner ? (KERNEL_LOCK_CONTEXT *)&owner->Locks : NULL;
}

// ====== x86_percpu_ap ======

const X86_CPU_LOCAL *x86_percpu_ap(uint32_t index) {
    if (ks_irq_state() || !x86_percpu_bsp() || x86_percpu_ap_closed() || !index || index >= X86_PERCPU_CAPACITY) return NULL;
    const X86_CPU_LOCAL *owner = &areas[index];
    if (owner->Self != (uintptr_t)owner || owner->EarlyContext || owner->Index != index ||
        owner->State != X86_PERCPU_PREPARED || owner->Reserved || !ks_context_valid(&owner->Locks) ||
        owner->Locks.Depth || owner->Locks.NextToken != 1 || !owner->Locks.Wait) return NULL;
    for (uint32_t byte = 0; byte < sizeof(owner->Locks.Frames); ++byte)
        if (((const unsigned char *)owner->Locks.Frames)[byte]) return NULL;
    return owner;
}

// ====== x86_percpu_ap_prepare ======

int x86_percpu_ap_prepare(uint32_t index, const KERNEL_LOCK_WAIT *wait) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    if (ks_irq_state() || !bsp || bsp->Locks.Depth || ap_sealed || !index ||
        index >= X86_PERCPU_CAPACITY || !ks_span(wait, sizeof(*wait)) ||
        ks_overlap(wait, sizeof(*wait), areas, sizeof(areas)) ||
        !empty_private(&areas[index])) return 0;
    X86_CPU_LOCAL *owner = &areas[index];
    if (!kernel_lock_context_initialize(&owner->Locks, wait)) return 0;
    owner->Self = (uintptr_t)owner; owner->Index = index; owner->State = X86_PERCPU_PREPARED;
    return 1;
}

// ====== x86_percpu_ap_cancel ======

int x86_percpu_ap_cancel(uint32_t index) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    const X86_CPU_LOCAL *owner = x86_percpu_ap(index);
    if (!bsp || bsp->Locks.Depth || ap_sealed || xj_context_retained(index) || !owner) return 0;
    X86_CPU_LOCAL *target = &areas[index];
    target->Locks.Wait = NULL;
    for (uint32_t byte = 0; byte < sizeof(*target); ++byte) ((unsigned char *)target)[byte] = 0;
    return 1;
}

// ====== x86_percpu_ap_seal ======

int x86_percpu_ap_seal(uint32_t count) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    if (ks_irq_state() || !bsp || bsp->Locks.Depth || ap_sealed || count < 2 || count > X86_PERCPU_CAPACITY) return 0;
    for (uint32_t index = 1; index < count; ++index) if (!x86_percpu_ap(index)) return 0;
    for (uint32_t index = count; index < X86_PERCPU_CAPACITY; ++index) if (!empty_private(&areas[index])) return 0;
    ap_sealed = count;
    return 1;
}

// ====== x86_percpu_ap_sealed ======

int x86_percpu_ap_sealed(uint32_t count) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    if (ks_irq_state() || !bsp || bsp->Locks.Depth || ap_sealed != count || count < 2 || count > 64) return 0;
    for (uint32_t index = 1; index < count; ++index) if (!x86_percpu_ap(index)) return 0;
    return 1;
}

// ====== x86_percpu_ap_closed ======

uint32_t x86_percpu_ap_closed(void) { return (uint32_t)ks_atomic_load_acquire(&ap_closed); }

// ====== x86_percpu_ap_access_close ======

int x86_percpu_ap_access_close(uint32_t count, const uint32_t *ids) {
    if (ks_irq_state() || !ks_boot_cpu() || x86_percpu_ap_closed() || !x86_percpu_ap_sealed(count) ||
        !ids || ((uintptr_t)ids & 3) || (uintptr_t)ids > UINTPTR_MAX - count * sizeof(*ids) ||
        ids[0] != xp_cpu_id()) return 0;
    for (uint32_t index = 0; index < count; ++index) {
        if (ids[index] >= 255) return 0;
        for (uint32_t previous = 0; previous < index; ++previous) if (ids[index] == ids[previous]) return 0;
    }
    for (uint32_t index = 0; index < count; ++index) ap_ids[index] = ids[index];
    ks_atomic_store_release(&ap_closed, count);
    return 1;
}

// ====== x86_percpu_ap_phase ======

uint64_t x86_percpu_ap_phase(uint32_t index) {
    if (!index || index >= x86_percpu_ap_closed()) return 0;
    return ks_atomic_load_acquire(&ap_phases[index]);
}

// ====== ap_local_private ======

static X86_CPU_LOCAL *ap_local_private(uint32_t index) {
    if (!index || index >= x86_percpu_ap_closed() || ks_boot_cpu() || xp_cpu_id() != ap_ids[index]) return NULL;
    X86_CPU_LOCAL *owner = &areas[index];
    if (xp_gs_read() != (uint64_t)owner || xp_gs_self() != (uint64_t)owner ||
        owner->Self != (uint64_t)owner || owner->Index != index || owner->EarlyContext || owner->Reserved ||
        !ks_context_valid(&owner->Locks)) return NULL;
    return owner;
}

// ====== x86_percpu_ap_enter ======

int x86_percpu_ap_enter(uint32_t index, uint32_t id, uint64_t local, const KERNEL_LOCK_WAIT *wait) {
    if (ks_irq_state() || !index || index >= x86_percpu_ap_closed() || id >= 255 || xp_cpu_id() != id || id != ap_ids[index] ||
        local != (uint64_t)&areas[index]) return 0;
    X86_CPU_LOCAL *owner = ap_local_private(index);
    if (!owner || owner->State != X86_PERCPU_PREPARED || !wait || owner->Locks.Wait != wait ||
        owner->Locks.Depth || owner->Locks.NextToken != 1) return 0;
    for (uint32_t byte = 0; byte < sizeof(owner->Locks.Frames); ++byte)
        if (((const unsigned char *)owner->Locks.Frames)[byte]) return 0;
    if (!ks_atomic_compare(&ap_phases[index], 0, 1)) return 0;
    ap_waits[index] = wait;
    owner->State = X86_PERCPU_ACTIVE;
    return 1;
}

// ====== x86_percpu_ap_accept ======

int x86_percpu_ap_accept(uint32_t index) {
    if (ks_irq_state() || !index || index >= x86_percpu_ap_closed() ||
        ks_atomic_load_acquire(&ap_phases[index]) != 1) return 0;
    X86_CPU_LOCAL *owner = ap_local_private(index);
    if (!owner || owner->State != X86_PERCPU_ACTIVE || xp_cpu_id() != ap_ids[index] ||
        owner->Locks.Wait != ap_waits[index] || owner->Locks.Depth || owner->Locks.NextToken != 1) return 0;
    for (uint32_t byte = 0; byte < sizeof(owner->Locks.Frames); ++byte)
        if (((const unsigned char *)owner->Locks.Frames)[byte]) return 0;
    ks_atomic_store_release(&ap_phases[index], 2);
    return 1;
}

// ====== x86_percpu_ap_reject ======

int x86_percpu_ap_reject(uint32_t index) {
    if (ks_irq_state() || ks_boot_cpu() || !index || index >= x86_percpu_ap_closed() ||
        xp_cpu_id() != ap_ids[index] || ks_atomic_load_acquire(&ap_phases[index]) == 3) return 0;
    ks_atomic_store_release(&ap_phases[index], 3);
    return 1;
}

// ====== x86_percpu_current_local ======

const X86_CPU_LOCAL *x86_percpu_current_local(void) {
    uint64_t saved = ks_irq_save();
    const X86_CPU_LOCAL *owner = NULL;
    if (ks_boot_cpu()) owner = bsp_private();
    else if (x86_percpu_ap_closed()) {
        uint64_t base = (uint64_t)&areas[0], actual = xp_gs_read();
        if (actual >= base + sizeof(X86_CPU_LOCAL) && actual - base < sizeof(areas) &&
            !((actual - base) % sizeof(X86_CPU_LOCAL))) {
            uint32_t index = (uint32_t)((actual - base) / sizeof(X86_CPU_LOCAL));
            if (index < x86_percpu_ap_closed() && ks_atomic_load_acquire(&ap_phases[index]) == 2 &&
                xp_cpu_id() == ap_ids[index]) {
                owner = ap_local_private(index);
                if (owner && (owner->State != X86_PERCPU_ACTIVE || owner->Locks.Wait != ap_waits[index])) owner = NULL;
            }
        }
    }
    ks_irq_restore(saved);
    return owner;
}
