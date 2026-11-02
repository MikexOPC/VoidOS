#include "runtime.h"
#include "kernel/diagnostics/log.h"
XP_PANIC_NATIVE xp_panic_native;
_Alignas(8) uint64_t xp_panic_armed, xp_panic_output, xp_panic_recursion;
static XP_PANIC_RECORD bsp_record;
extern const BOOT_U64 x86_kernel_fault_targets[32];
extern const unsigned char x86_kernel_fault_begin[], x86_kernel_fault_end[];

// ====== rollback_private ======

static int rollback_private(uint32_t initialized) {
    for (uint32_t cpu = initialized; cpu > 0; --cpu)
        if (!xp_panic_record_retire(xp_panic_native.Records[cpu - 1])) xp_panic_quiet_stop(UINT64_C(0x50414e4952424144));
    for (uint32_t byte = 0; byte < sizeof(xp_panic_native); ++byte)
        ((volatile unsigned char *)&xp_panic_native)[byte] = 0;
    return 0;
}

// ====== xp_panic_native_prepare ======

int xp_panic_native_prepare(const X86_LAPIC_MAPPING *lapic) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    const X86_CPU_IDENTITIES *identities = x86_cpu_identities();
    const KERNEL_LOCK_WAIT *wait = x86_percpu_wait_current();
    XP_PANIC_NATIVE *native = &xp_panic_native;
    if (ks_irq_state() || !bsp || bsp->Locks.Depth || !identities || !wait || native->Count ||
        !lapic || !x86_lapic_authority(lapic) || !xp_descriptors_ready() || !x86_exception_environment() ||
        identities->Count < 1 || identities->Count > 64 || xp_panic_value(XP_PANIC_READY)) return 0;
    X86_APIC_RAW raw;
    if (!x86_apic_read_profile(&raw) || !x86_apic_uc_slot(raw.Pat) || !xp_panic_profile_valid(raw.ApicBase, lapic->Physical,
        raw.Pat, raw.Pat, raw.PhysicalBits, lapic->PhysicalBits, 1)) return 0;
    native->Pat = raw.Pat;
    native->Root = x86_kernel_root();
    if (native->Root != lapic->Root) return 0;
    native->Source = *(const X86_PM_CLOCK *)wait->Clock.Context;
    native->Lapic = lapic; native->Count = identities->Count;
    int joint_resources = native->Count > 1 && x86_ap_resources_joint_ready();
    uint32_t initialized = 0;
    native->Transport = (XP_PANIC_TRANSPORT){xp_panic_hw_read, xp_panic_hw_write, xp_panic_hw_now,
        xp_panic_hw_pause, &native->Source, wait->Clock.Mask, wait->Ticks, wait->MaxPolls};
    native->Sink = (XP_PANIC_SINK){xp_panic_hw_port_read, xp_panic_hw_port_write, xp_panic_hw_now,
        xp_panic_hw_pause, &native->Source, wait->Clock.Mask, (KERNEL_PM_TIMER_HZ + 3U) / 4U, 200000, 2048, 0x3f8};
    if (!x86_exception_prepare(&native->Idt, &native->Idtr, x86_kernel_fault_targets, 32,
        (uint64_t)x86_kernel_fault_begin, (uint64_t)x86_kernel_fault_end)) return rollback_private(0);
    native->Idt.Gates[2][4] = 2; native->Idt.Gates[8][4] = 3;
    x86_exception_read_idt(&native->BspIdtr);
    x86_kernel_segments_read(&native->BspGdtr);
    for (uint32_t cpu = 0; cpu < native->Count; ++cpu) {
        native->Ids[cpu] = identities->Cpus[cpu].Id;
        native->Contexts[cpu] = cpu ? x86_percpu_ap(cpu) : bsp;
        if (!native->Contexts[cpu]) return rollback_private(initialized);
        native->Records[cpu] = cpu ? (XP_PANIC_RECORD *)(X86_AP_METADATA(cpu) + X86_AP_RECORD_OFFSET) : &bsp_record;
        if (!xp_panic_record_initialize(native->Records[cpu], cpu)) return rollback_private(initialized);
        ++initialized;
    }
    if (native->Count > 1 && !joint_resources && !x86_ap_resources_commit()) return rollback_private(initialized);
    if (!xp_panic_configure(native->Count) || xp_panic_admit(0) != 1) xp_panic_quiet_stop(UINT64_C(0x50414e4943464149));
    if (!kernel_text("KERNEL_PANIC_STATE: ") || !kernel_number(xp_panic_state_address()) ||
        !kernel_text("\nKERNEL_PANIC_RECORD: ") || !kernel_number((uint64_t)&bsp_record) ||
        !kernel_text("\nKERNEL_PANIC_OUTPUT: ") || !kernel_number((uint64_t)&xp_panic_output) ||
        !kernel_text("\nKERNEL_PANIC_RECURSION: ") || !kernel_number((uint64_t)&xp_panic_recursion) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_PANIC_BACKEND_PREPARED")) xp_panic_quiet_stop(UINT64_C(0x50414e49434c4f47));
    ks_atomic_store_release(&xp_panic_armed, 1);
    return 1;
}

// ====== xp_panic_native_admit ======

int xp_panic_native_admit(uint32_t index) {
    const XP_PANIC_NATIVE *native = &xp_panic_native;
    if (ks_irq_state() || !ks_atomic_load_acquire(&xp_panic_armed) || index >= native->Count ||
        xp_panic_apic_id() != native->Ids[index] || !x86_exception_environment()) return 0;
    const X86_CPU_LOCAL *owner = native->Contexts[index];
    if (xp_gs_read() != (uint64_t)owner || xp_gs_self() != (uint64_t)owner || owner->Self != (uint64_t)owner ||
        owner->Index != index || owner->State != X86_PERCPU_ACTIVE || owner->Locks.Depth ||
        !ks_context_valid(&owner->Locks)) return 0;
    X86_APIC_RAW raw;
    if (!x86_apic_read_profile(&raw) || !xp_panic_profile_valid(raw.ApicBase, native->Lapic->Physical,
        raw.Pat, native->Pat, raw.PhysicalBits, native->Lapic->PhysicalBits, index == 0)) return 0;
    X86_GDT_POINTER actual_gdt;
    x86_kernel_segments_read(&actual_gdt);
    const X86_GDT_POINTER *expected_gdt = &native->BspGdtr;
    if (index) {
        const X86_KERNEL_SEGMENTS *segments = (const X86_KERNEL_SEGMENTS *)X86_AP_METADATA(index);
        X86_KERNEL_SEGMENTS expected_segments;
        if (!x86_ap_descriptor_prepare(&expected_segments, index)) return 0;
        expected_segments.Descriptor.Bytes[5] = 0x8b;
        for (uint32_t byte = 0; byte < sizeof(expected_segments); ++byte)
            if (((const unsigned char *)segments)[byte] != ((const unsigned char *)&expected_segments)[byte]) return 0;
        expected_gdt = &segments->Pointer;
    }
    for (uint32_t byte = 0; byte < sizeof(actual_gdt); ++byte) if (actual_gdt.Bytes[byte] != expected_gdt->Bytes[byte]) return 0;
    X86_KERNEL_IDTR actual;
    x86_exception_read_idt(&actual);
    const X86_KERNEL_IDTR *expected = index ? &native->Idtr : &native->BspIdtr;
    for (uint32_t byte = 0; byte < sizeof(actual); ++byte) if (actual.Bytes[byte] != expected->Bytes[byte]) return 0;
    uint32_t result = xp_panic_admit(index);
    if (result == 2) xp_panic_voluntary(UINT64_C(0x4c41544553544f50));
    return result == 1;
}
