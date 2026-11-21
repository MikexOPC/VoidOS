#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#include "arch/x86/kernel/scheduler/admission/admission.h"

XP_PANIC_NATIVE xp_panic_native;
uint64_t xp_panic_armed, xp_panic_output, xp_panic_recursion;
static X86_KERNEL_IDTR shared_descriptor;
static X86_CPU_LOCAL owner;
static X86_LAPIC_MAPPING lapic;
static uint32_t irq, bad_table;
static jmp_buf stopped;
static uint64_t reason;

// ====== modeled_privileged_dependencies ======

uint64_t ks_atomic_load_acquire(const uint64_t *value) { return *value; }
uint64_t ks_atomic_load_relaxed(const uint64_t *value) { return *value; }
void ks_atomic_store_release(uint64_t *value, uint64_t next) { *value = next; }
uint64_t ks_irq_state(void) { return irq; }
uint32_t ks_boot_cpu(void) { return owner.Index == 0; }
uint64_t x86_kernel_root(void) { return 0x10000; }
uint64_t x86_kernel_stack_pointer(void) { return 0; }
const X86_CPU_LOCAL *x86_percpu_current_local(void) { return &owner; }
uint32_t xp_panic_apic_id(void) { return owner.Index; }
int x86_lapic_authority(const X86_LAPIC_MAPPING *mapping) { return mapping == &lapic; }
int x86_irq_cpu_state(void) { return 1; }
BOOT_U32 x86_apic_lapic_version(BOOT_U32 value) { (void)value; return 1; }
BOOT_U32 x86_irq_lvt(BOOT_U32 maximum, BOOT_U32 index) { (void)maximum; (void)index; return 0; }
int xp_image_contains(uintptr_t address, size_t bytes) { (void)address; (void)bytes; return 1; }
void x86_exception_load_idt(const X86_KERNEL_IDTR *pointer) { (void)pointer; assert(0); }
void x86_exception_read_idt(X86_KERNEL_IDTR *pointer) { (void)pointer; assert(0); }
uint32_t xis_mmio_read(uint32_t offset) { (void)offset; assert(0); return 0; }
void xis_mmio_write(uint32_t offset, uint32_t value) { (void)offset; (void)value; assert(0); }
int xis_native_now(void *context, uint32_t *now) { (void)context; (void)now; assert(0); return 0; }
__declspec(noreturn) void xp_panic_voluntary(uint64_t value) { reason = value; longjmp(stopped, 1); }

// ====== kernel_deadline_start ======

int kernel_deadline_start(const KERNEL_CLOCK *clock, uint32_t ticks, KERNEL_DEADLINE *deadline) {
    (void)clock;
    (void)ticks;
    (void)deadline;
    abort();
}

// ====== kernel_deadline_expired ======

int kernel_deadline_expired(const KERNEL_CLOCK *clock, const KERNEL_DEADLINE *deadline) {
    (void)clock;
    (void)deadline;
    abort();
}

// ====== ks_atomic_compare ======

uint32_t ks_atomic_compare(uint64_t *value, uint64_t expected, uint64_t next) {
    (void)value;
    (void)expected;
    (void)next;
    abort();
}

// ====== ks_pause ======

void ks_pause(void) {
    abort();
}

// ====== modeled_table_builder ======

const X86_KERNEL_IDTR *x86_irq_descriptor(void) { return &shared_descriptor; }
int x86_irq_shared_valid(void) { return !bad_table; }
int x86_irq_cpu_activate(void) { assert(0); return 0; }
const X86_IRQ_FRAME *x86_irq_current_frame(void) { assert(0); return NULL; }
int x86_irq_registry_bind(X86_IRQ_REGISTRY *registry, BOOT_U32 vector,
    X86_IRQ_CALLBACK handle, X86_IRQ_CALLBACK quiet, X86_IRQ_TOKEN *token) {
    (void)registry; (void)vector; (void)handle; (void)quiet; (void)token;
    assert(0); return 0;
}

// ====== rejected_private ======

static void rejected_private(uint32_t operation) {
    if (operation == 2 || operation == 4 || operation == 5) {
        uintptr_t frame = operation == 2 ? 0 : operation == 4 ?
            X86_AP_STACK_FLOOR(1) + 8 : X86_AP_FRAME(1) - 40;
        assert(!xis_irq_frame_valid((const X86_IRQ_FRAME *)frame));
        return;
    }
    reason = 0;
    if (!setjmp(stopped)) {
        if (operation == 0) (void)xis_native_index();
        else if (operation == 1) (void)xis_native_enter(NULL);
        else if (operation == 3) xis_native_verified(NULL);
        else xad_native_poll();
        assert(0);
    }
    assert(reason);
    if (operation == 6) assert(reason == 0x4b3441444d464149ULL);
}

// ====== main ======

int main(void) {
    for (uint32_t diagnostic = 0; diagnostic < 2; ++diagnostic) {
        uint64_t top = X86_AP_FRAME(1), rsp = top - XIS_IDLE_FRAME_BYTES;
        uint64_t frame = (rsp & ~15ULL) - sizeof(X86_IRQ_FRAME);
        uint64_t flags = diagnostic ? 0x602 : 0x202;
        assert(x86_irq_saved_area(frame,X86_AP_STACK_FLOOR(1),top) == 1);
        assert(xis_idle_return_valid(frame,top,rsp,8,flags,16,diagnostic) == 1);
        assert(!xis_idle_return_valid(frame + 8,top,rsp,8,flags,16,diagnostic));
        assert(!xis_idle_return_valid(frame,top,rsp,8,flags,16,2));
        assert(!x86_irq_saved_area(X86_AP_STACK_FLOOR(1) + 8,X86_AP_STACK_FLOOR(1),top));
    }
    rejected_private(0);
    assert(!xis_native_prepare());
    xp_panic_armed = 1; xp_panic_native.Count = 4; xp_panic_native.Root = 0x10000;
    xp_panic_native.Lapic = &lapic;
    for (uint32_t index = 0; index < 4; ++index) {
        xp_panic_native.Contexts[index] = &owner;
        xp_panic_native.Ids[index] = index;
    }
    owner.Self = (uint64_t)&owner; owner.State = X86_PERCPU_ACTIVE;
    xp_panic_native.Count = 1; assert(!xis_native_prepare());
    xp_panic_native.Count = 65; assert(!xis_native_prepare());
    xp_panic_native.Count = 4;
    irq = 1; assert(!xis_native_prepare()); irq = 0;
    bad_table = 1; assert(!xis_native_prepare()); bad_table = 0;
    assert(xis_native_prepare());
    assert(!xis_native_prepare());
    assert(xis_native_index() == 0);
    owner.Index = 1; assert(xis_native_index() == 1);
    owner.Index = 4; rejected_private(0);
    owner.Index = 1; owner.State = 0; rejected_private(0);
    owner.State = X86_PERCPU_ACTIVE; owner.Self = 0; rejected_private(0);
    owner.Self = (uint64_t)&owner; owner.Locks.Depth = 1; rejected_private(0);
    owner.Locks.Depth = 0;
    xis_native.Count = 65; rejected_private(0); xis_native.Count = 4;
    assert(xis_native.Count == xp_panic_native.Count);
    xis_native.Count = 3; rejected_private(0); xis_native.Count = 1; rejected_private(0); xis_native.Count = 4;
    irq = 1; rejected_private(1); irq = 0;
    rejected_private(1);
    rejected_private(2);
    rejected_private(3);
    rejected_private(4);
    rejected_private(5);
    assert(xad_configure(4,0x10000,0));
    assert(xad_offer(1,0,1,0x10000));
    rejected_private(6);
    assert(xad_value(1,0x10000) == (1ULL << 3 | XAD_FAILED) && !xad_native_record.Self);
    assert(xis_value(1, XIS_PHASE) == XIS_PREPARED);
    puts("IPI_NATIVE_HOST_PASS: preparation and rejected CPU/stack contexts; hardware modeled");
    return 0;
}
