#include "internal.h"
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/scheduler/irq/preempt.h"
#endif

// ====== process_timer_arm ======

void process_timer_arm(void)
{
    if (process_rt.Permanent)
        return;
    const X86_LAPIC_MAPPING *mapping = process_rt.Timer->Mapping;
    BOOT_U32 vector = K6_NEGATIVE(36) ? STRAY_TEST_VECTOR : TIMER_VECTOR;
    // Intel SDM 3A §13.5.4: divide 3E0h = 3 (by 16), LVT 320h one-shot unmasked, initial count 380h.
    if (!x86_timer_confirm_write(mapping, 0x3e0, 3) || !x86_timer_confirm_write(mapping, 0x320, vector) ||
        !x86_timer_write(mapping, 0x380, process_rt.Timer->Reload)) process_die("PROCESS_TIMER_FAILED");
}

// ====== process_timer_start ======

void process_timer_start(void)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu) process_die("PROCESS_CPU_INVALID");
    cpu->BudgetLeft = x86_process_budget_initial();
    BOOT_U32 duration = PROCESS_BUDGET_MS;
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_delay_next_syscall) duration = 100;
#endif
    if (!kernel_deadline_start(cpu->BudgetClock, kernel_clock_ms(cpu->BudgetClock, duration), &cpu->BudgetDeadline))
        process_die("PROCESS_CLOCK_FAILED");
    process_timer_arm();
}

// ====== process_timer_quiesce ======

void process_timer_quiesce(void)
{
    if (process_rt.Permanent)
        return;
    BOOT_U32 pending = 0;
    BOOT_U32 mask = IRR_TIMER_BIT;
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) mask |= IRR_DEVICE_BIT;
#endif
    if (!x86_timer_stop(process_rt.Timer->Mapping)) process_die("PROCESS_TIMER_RETAINED");
    for (BOOT_U32 attempt = 0; attempt < DRAIN_ATTEMPTS; ++attempt) {
        if (!x86_lapic_native_read(IRR_TIMER_BANK, &pending)) process_die("PROCESS_TIMER_RETAINED");
        if (!(pending & mask)) break;
        process_drain = 1;
        _enable();
        __nop();
        __nop();
        _disable();
        process_drain = 0;
    }
    if (!x86_lapic_idle_except(process_rt.Timer->Mapping, process_rt.InputIrqMask)) process_die("PROCESS_TIMER_RETAINED");
}

// ====== x86_process_on_preempt_tick ======

void x86_process_on_preempt_tick(const X86_IRQ_FRAME *frame)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu || !cpu->Current)
        process_die("PROCESS_PREEMPT_CPU_INVALID");
    ++cpu->Current->Irqs;
    cpu->Current->Rip = frame->Rip;
    if (!x86_process_irq_frame_valid(frame->Rip, frame->Cs, frame->Flags, frame->Rsp, frame->Ss)) {
        cpu->Current->Kind = X86_PROCESS_BAD_FRAME;
        cpu->LeaveRequested = 2;
    } else {
        cpu->BudgetLeft = x86_process_budget_next(cpu->BudgetLeft);
        if (process_budget_expired(frame->Rip)) {
            cpu->LeaveRequested = 2;
        } else if (cpu->YieldFrame) {
            cpu->LeaveRequested = 1;
        }
    }
#ifdef KERNEL_PS2_INPUT
    if (!x86_preempt_rearm())
        kernel_panic("PROCESS_PREEMPT_REARM_FAILED");
#endif
}

// ====== x86_process_irq_capture ======

void x86_process_irq_capture(const BOOT_U64 *fx, const BOOT_U64 *gprs, const BOOT_U64 *iret)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu)
        process_die("PROCESS_CPU_INVALID");
    BOOT_U32 reason = cpu->LeaveRequested;
    cpu->LeaveRequested = 0;
    if (reason == 1 && cpu->YieldFrame) {
        for (BOOT_U32 index = 0; index < FX_WORDS; ++index)
            cpu->YieldFrame[index] = fx[index];
        for (BOOT_U32 index = 0; index < 15U; ++index)
            cpu->YieldFrame[FX_WORDS + index] = gprs[index];
        for (BOOT_U32 index = 0; index < 5U; ++index)
            cpu->YieldFrame[FX_WORDS + 15U + index] = iret[index];
        cpu->Yielded = 1;
    }
}

// ====== x86_process_irq_service ======

int x86_process_irq_service(BOOT_U64 *frame)
{
    BOOT_U32 value, index;
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu || !cpu->Current || (BOOT_U64)&IRET(frame, 0) != cpu->SyscallTop - 40) process_die("PROCESS_IRQ_INVALID");
    // Intel SDM 3A §13.8.4: ISR 100h-170h, one 32-bit bank per 16-byte step, indexed by vector.
    for (index = 0; index < 8; ++index) {
        if (!x86_lapic_native_read(0x100 + index * 16, &value) ||
            value != (index == TIMER_VECTOR / 32 ? 1U << (TIMER_VECTOR % 32) : 0U)) process_die("PROCESS_IRQ_INVALID");
    }
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_lapic_native_eoi()) process_die("PROCESS_IRQ_INVALID");
    ++cpu->Current->Irqs;
    cpu->Current->Rip = IRET(frame, 0);
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        cpu->Current->Kind = X86_PROCESS_BAD_FRAME;
        return 2;
    }
    cpu->BudgetLeft = x86_process_budget_next(cpu->BudgetLeft);
    if (process_budget_expired(IRET(frame, 0))) return 2;
    if (!x86_timer_write(process_rt.Timer->Mapping, 0x380, process_rt.Timer->Reload)) process_die("PROCESS_TIMER_FAILED");
    return 1;
}

// ====== input_irq_deliver ======

static int input_irq_deliver(BOOT_U32 vector)
{
    if (vector < 0x40U || vector >= 0x40U + X86_IRQ_DEVICE_SLOTS) return 0;
    BOOT_U32 index = vector - 0x40U;
    X86_IRQ_REGISTRY *registry = x86_irq_registry();
    return (process_rt.InputIrqMask & (1U << index)) &&
        x86_irq_registry_token_live(registry, &process_rt.InputIrqTokens[index]) &&
        x86_irq_registry_deliver(registry, vector);
}

// ====== x86_process_device_irq_service ======

int x86_process_device_irq_service(BOOT_U64 *frame, BOOT_U32 vector)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu || !cpu->Current || (BOOT_U64)&IRET(frame, 0) != cpu->SyscallTop - 40 ||
        !input_irq_deliver(vector))
        process_die("PROCESS_DEVICE_IRQ_INVALID");
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) {
        if (process_probe_delivered) process_die("PROCESS_DEVICE_IRQ_INVALID");
        process_probe_delivered = 1;
    }
#endif
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        cpu->Current->Kind = X86_PROCESS_BAD_FRAME;
        return 2;
    }
    return process_budget_expired(IRET(frame, 0)) ? 2 : 1;
}

// ====== x86_process_device_irq_kernel ======

void x86_process_device_irq_kernel(BOOT_U32 vector)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !process_drain || !input_irq_deliver(vector))
        process_die("PROCESS_DEVICE_IRQ_INVALID");
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) {
        if ((cpu && cpu->Current) || process_probe_delivered != 1) process_die("PROCESS_DEVICE_IRQ_INVALID");
        process_probe_delivered = 2;
    }
#else
    (void)cpu;
#endif
}

// ====== x86_process_irq_drained ======

void x86_process_irq_drained(void)
{
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_lapic_native_eoi()) process_die("PROCESS_TIMER_RETAINED");
    ++process_rt.Drained;
}

// ====== x86_process_kernel_fatal ======

void x86_process_kernel_fatal(const BOOT_U64 *frame, BOOT_U64 cr2)
{
    if (!process_rt.Permanent)
        x86_exception_load_idt(&process_rt.IdtPrevious);
    (void)kernel_text("PROCESS_FAULT_VECTOR: ");
    (void)kernel_number(frame[0]);
    (void)kernel_text("\nPROCESS_FAULT_RIP: ");
    (void)kernel_number(frame[2]);
    (void)kernel_text("\nPROCESS_FAULT_CR2: ");
    (void)kernel_number(cr2);
    (void)kernel_text("\n");
    const char *reason = frame[0] == VECTOR_STRAY ? "PROCESS_STRAY_IRQ" :
        frame[0] == 2 ? "PROCESS_STRAY_NMI" : "PROCESS_KERNEL_FAULT";
    (void)kernel_log(3, reason);
    kernel_panic(reason);
}
