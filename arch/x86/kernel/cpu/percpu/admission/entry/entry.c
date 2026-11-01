#include "entry.h"
#include "../group.h"
#include "../../internal.h"
#include "../../panic/native/runtime.h"
#include "../../startup/startup.h"
#include "arch/x86/kernel/stack/main.h"
extern const BOOT_U64 x86_kernel_fault_targets[32];

// ====== same_private ======

static int same_private(const void *a, const void *b, uint32_t bytes) {
    for (uint32_t byte = 0; byte < bytes; ++byte)
        if (((const unsigned char *)a)[byte] != ((const unsigned char *)b)[byte]) return 0;
    return 1;
}

// ====== cpu_private ======

static const XAG_CPU *cpu_private(const XAG_GROUP *group, XS_PRIVATE_FRAME *frame) {
    if (!group || group->Header.Count != x86_percpu_ap_closed() || group->Header.Count < 2 || group->Header.Count > 64 ||
        x86_kernel_root() != group->Header.Root) return NULL;
    uint32_t id = xp_cpu_id();
    for (uint32_t index = 1; index < group->Header.Count; ++index) {
        uint64_t top = X86_AP_BASE + index * X86_AP_STRIDE + 17 * 4096;
        if (group->Cpus[index].ApicId == id && (uint64_t)frame == top - sizeof(*frame)) return &group->Cpus[index];
    }
    return NULL;
}

// ====== idt_private ======

static int idt_private(void) {
    const X86_KERNEL_IDTR *pointer = &xp_panic_native.Idtr;
    uint64_t base = 0;
    for (uint32_t byte = 0; byte < 8; ++byte) base |= (uint64_t)pointer->Bytes[2 + byte] << (byte * 8);
    if (pointer->Bytes[0] != 255 || pointer->Bytes[1] != 1 || base != (uint64_t)&xp_panic_native.Idt) return 0;
    for (uint32_t vector = 0; vector < 32; ++vector) {
        const unsigned char *gate = xp_panic_native.Idt.Gates[vector];
        uint64_t target = x86_kernel_fault_targets[vector];
        unsigned char expected[16] = {(unsigned char)target, (unsigned char)(target >> 8), 8, 0,
            (unsigned char)(vector == 2 ? 2 : vector == 8 ? 3 : 1), 0x8e};
        for (uint32_t byte = 0; byte < 6; ++byte) expected[6 + byte] = (unsigned char)(target >> (16 + byte * 8));
        if (!xp_image_contains(target, 1) || !same_private(gate, expected, 16)) return 0;
    }
    return 1;
}

// ====== wait_private ======

static int wait_private(const XAG_CPU *cpu) {
    const KERNEL_LOCK_WAIT *wait = (const KERNEL_LOCK_WAIT *)cpu->Wait;
    if (!same_private(wait, &cpu->Expected, sizeof(*wait)) ||
        !same_private(cpu->Expected.Clock.Context, &cpu->Source, sizeof(cpu->Source))) return 0;
    uint32_t start, now;
    if (!kernel_clock_now(&cpu->Expected.Clock, &start)) return 0;
    uint32_t limit = cpu->Expected.MaxPolls < 4096 ? cpu->Expected.MaxPolls : 4096;
    for (uint32_t poll = 0; poll < limit; ++poll) {
        if (!kernel_clock_now(&cpu->Expected.Clock, &now)) return 0;
        uint32_t delta = kernel_clock_delta(&cpu->Expected.Clock, start, now);
        if (delta) return delta < cpu->Expected.Clock.Mask / 2;
        ks_pause();
    }
    return 0;
}

// ====== fail_private ======

static uint32_t fail_private(XS_PRIVATE_FRAME *frame, uint64_t token, int claimed, uint32_t error) {
    frame->Error = error; frame->Phase = UINT32_MAX;
    uint32_t index = (uint32_t)(((uint64_t)frame - X86_AP_BASE - 17 * 4096 + sizeof(*frame)) / X86_AP_STRIDE);
    (void)x86_percpu_ap_reject(index);
    if (claimed) (void)xs_quarantine(token);
    return error;
}

// ====== xa_native_admit ======

uint32_t xa_native_admit(XS_PRIVATE_FRAME *frame, uint64_t token) {
    if (ks_irq_state() || ks_boot_cpu()) return 1;
    const XAG_GROUP *group = xag_group();
    const XAG_CPU *cpu = cpu_private(group, frame);
    if (!cpu) return 2;
    uint64_t top = X86_AP_BASE + cpu->Index * X86_AP_STRIDE + 17 * 4096;
    uint64_t rsp = x86_kernel_stack_pointer();
    if (rsp < top - 65536 + 4096 || rsp >= (uint64_t)frame) return 2;
    XS_ENTRY_RECORD r = frame->Record;
    if (!xs_entry_valid(r, group->Header.PhysicalBits) || r.Index != cpu->Index || r.ApicId != cpu->ApicId ||
        r.Token != token || r.Local != cpu->Local || r.StackTop != top ||
        r.Descriptors != X86_AP_BASE + cpu->Index * X86_AP_STRIDE + 48 * 4096 ||
        r.Entry != (uint64_t)xs_private_entry || r.KernelRoot != group->Header.Root || r.Pat != group->Header.Pat ||
        frame->Phase != 2 || frame->Error)
        return fail_private(frame, token, 0, 3);
    X86_APIC_RAW raw;
    if (!ks_atomic_load_acquire(&xp_panic_armed) || xp_panic_native.Count != group->Header.Count ||
        xp_panic_native.Root != group->Header.Root || xp_panic_native.Pat != group->Header.Pat || !xp_panic_native.Lapic ||
        xp_panic_native.Contexts[cpu->Index] != (const X86_CPU_LOCAL *)cpu->Local ||
        xp_panic_native.Ids[cpu->Index] != cpu->ApicId || x86_kernel_root() != group->Header.Root ||
        !x86_apic_read_profile(&raw) || raw.PhysicalBits != group->Header.PhysicalBits ||
        (raw.Rflags & 0x7600) || !x86_exception_environment() ||
        !xp_panic_profile_valid(raw.ApicBase, xp_panic_native.Lapic->Physical, raw.Pat,
            group->Header.Pat, raw.PhysicalBits, group->Header.PhysicalBits, 0) ||
        !idt_private() || !wait_private(cpu)) return fail_private(frame, token, 0, 4);
    if (!xs_return_build(&frame->Return, r, (uint64_t)xs_private_park, (uint64_t)&xp_panic_native.Idtr,
        group->Header.PhysicalBits)) return fail_private(frame, token, 0, 5);
    if (!xs_entry_claim(r, group->Header.PhysicalBits, cpu->ApicId)) return fail_private(frame, token, 0, 6);
    if (!x86_percpu_ap_enter(cpu->Index, cpu->ApicId, cpu->Local, (const KERNEL_LOCK_WAIT *)cpu->Wait))
        return fail_private(frame, token, 1, 7);
    x86_exception_load_idt(&xp_panic_native.Idtr);
    if (!xp_panic_native_admit(cpu->Index) || !x86_percpu_ap_accept(cpu->Index)) return fail_private(frame, token, 1, 8);
    if (x86_percpu_wait_current() != (const KERNEL_LOCK_WAIT *)cpu->Wait ||
        kernel_lock_current_context() != &((X86_CPU_LOCAL *)cpu->Local)->Locks ||
        !xs_advance(token, XS_ENTERED)) return fail_private(frame, token, 1, 9);
    frame->Phase = 5;
    return 0;
}

// ====== xa_native_park ======

uint32_t xa_native_park(XS_PRIVATE_FRAME *frame, uint64_t token) {
    if (ks_irq_state() || ks_boot_cpu()) return 10;
    const XAG_GROUP *group = xag_group();
    const XAG_CPU *cpu = cpu_private(group, frame);
    if (!cpu) return 11;
    if (frame->Record.Token != token || !xs_entry_valid(frame->Record, group->Header.PhysicalBits) ||
        frame->Record.Index != cpu->Index) return fail_private(frame, token, 0, 12);
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner || (uint64_t)owner != cpu->Local || frame->Phase != 5 ||
        x86_kernel_root() != group->Header.Root || owner->Locks.Depth ||
        x86_percpu_wait_current() != (const KERNEL_LOCK_WAIT *)cpu->Wait) return fail_private(frame, token, 1, 12);
    frame->Phase = 6;
    if (!xs_advance(token, XS_PRIVATE)) return fail_private(frame, token, 1, 13);
    return 0;
}
