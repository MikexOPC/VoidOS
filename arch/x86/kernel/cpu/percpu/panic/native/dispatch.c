#include "runtime.h"
#include "arch/x86/kernel/descriptors/stacks.h"

// ====== current_private ======

static uint32_t current_private(uint64_t root) {
    const XP_PANIC_NATIVE *native = &xp_panic_native;
    if (!ks_atomic_load_acquire(&xp_panic_armed) || xp_panic_value(XP_PANIC_READY) != 2 ||
        !native->Count || native->Count > 64 || root != native->Root)
        xp_panic_quiet_stop(UINT64_C(0x50414e4943524f4f));
    uint32_t id = xp_panic_apic_id();
    for (uint32_t cpu = 0; cpu < native->Count; ++cpu) if (native->Ids[cpu] == id) return cpu;
    xp_panic_quiet_stop(UINT64_C(0x50414e4943494446));
}

// ====== zero_private ======

static void zero_private(XP_PANIC_INPUT *data) {
    for (uint32_t byte = 0; byte < sizeof(*data); ++byte) ((volatile unsigned char *)data)[byte] = 0;
}

// ====== registers_private ======

static void registers_private(volatile XP_PANIC_INPUT *input, const volatile uint64_t *saved) {
    for (uint32_t reg = 0; reg < 16; ++reg)
        input->Gpr[reg] = reg == 4 ? input->Rsp : saved[reg < 4 ? 14 - reg : 15 - reg];
}

// ====== terminal_private ======

__declspec(noreturn) static void terminal_private(uint32_t index, const XP_PANIC_INPUT *input) {
    (void)xp_panic_record_capture(xp_panic_native.Records[index], input);
    uint32_t role = xp_panic_elect(index);
    if (role == 3) {
        uint64_t value = ks_atomic_load_acquire(&xp_panic_recursion);
        (void)ks_atomic_compare(&xp_panic_recursion, value, value | (1ULL << index));
        xp_panic_quiet_stop(UINT64_C(0x50414e4943524543));
    }
    if (role == 2) {
        (void)xp_panic_acknowledge(index);
        xp_panic_quiet_stop(UINT64_C(0x50414e494353544f));
    }
    if (role != 1) xp_panic_quiet_stop(UINT64_C(0x50414e49434e4f57));
#ifdef KERNEL_TEST_SMP_PANIC
    extern volatile uint64_t xp_panic_test_mode;
    if (xp_panic_test_mode == 8) xp_panic_voluntary(UINT64_C(0x5245435552534956));
#endif
    X86_APIC_RAW cpu;
    if (!x86_apic_read_profile(&cpu) || !xp_panic_profile_valid(cpu.ApicBase, xp_panic_native.Lapic->Physical,
        cpu.Pat, xp_panic_native.Pat, cpu.PhysicalBits, xp_panic_native.Lapic->PhysicalBits, index == 0) ||
        !xp_panic_transport_run(index, xp_panic_native.Ids, xp_panic_native.Count, &xp_panic_native.Transport))
        (void)xp_panic_finish(index, XP_PANIC_TRANSPORT_FAILED);
    xp_panic_report(index);
    xp_panic_quiet_stop(UINT64_C(0x50414e4943444f4e));
}

// ====== xp_panic_fault ======

__declspec(noreturn) void xp_panic_fault(const XP_PANIC_RAW *raw, uint64_t cr2, uint64_t cr3) {
    uint32_t index = current_private(cr3), stack = 0;
    uint64_t address = (uint64_t)raw;
    for (uint32_t ist = 1; ist <= 3; ++ist) {
        uint64_t base = index ? X86_AP_BASE + index * X86_AP_STRIDE + (ist == 1 ? 19ULL : ist == 2 ? 25ULL : 31ULL) * 4096 : x86_ist_base(ist);
        if (!(address & 7) && address >= base && address <= base + 16384 - sizeof(*raw)) stack = ist;
    }
    if (!stack) xp_panic_quiet_stop(UINT64_C(0x50414e494353504e));
    const volatile X86_EXCEPTION_FRAME *frame = &raw->Frame;
    if (frame->Vector >= 32 || stack != (frame->Vector == 2 ? 2U : frame->Vector == 8 ? 3U : 1U))
        xp_panic_quiet_stop(UINT64_C(0x50414e494353544b));
    XP_PANIC_INPUT data;
    volatile XP_PANIC_INPUT *input = &data;
    zero_private(&data);
    input->Kind = frame->Vector == 2 && xp_panic_value(XP_PANIC_OWNER) ? 3 : 2;
    input->Frame = (uint64_t)frame; input->Cr2 = cr2; input->Cr3 = cr3;
    input->Vector = frame->Vector; input->Error = frame->Error; input->Rip = frame->Rip;
    input->Cs = frame->Cs; input->Flags = frame->Flags; input->Rsp = frame->Rsp; input->Ss = frame->Ss;
    registers_private(input, raw->Saved);
    terminal_private(index, &data);
}

// ====== xp_panic_call ======

__declspec(noreturn) void xp_panic_call(const XP_PANIC_CALL *raw, uint64_t cr2, uint64_t cr3, uint32_t selectors) {
    uint32_t index = current_private(cr3);
    if (!raw || ((uint64_t)raw & 7) || !X86_LAYOUT_RANGE((uint64_t)raw, sizeof(*raw)))
        xp_panic_quiet_stop(UINT64_C(0x50414e494343414c));
    XP_PANIC_INPUT data;
    volatile XP_PANIC_INPUT *input = &data;
    zero_private(&data);
    input->Kind = 1; input->Frame = (uint64_t)raw; input->Reason = raw->Saved[13];
    input->Cr2 = cr2; input->Cr3 = cr3; input->Rip = raw->Return;
    input->Cs = selectors >> 16; input->Ss = selectors & 0xffffU;
    input->Flags = raw->Flags; input->Rsp = (uint64_t)raw + sizeof(*raw);
    registers_private(input, raw->Saved);
    terminal_private(index, &data);
}
