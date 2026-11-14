#include "internal.h"

// ====== process_timer_arm ======

void process_timer_arm(void)
{
    const X86_LAPIC_MAPPING *mapping = process_rt.Timer->Mapping;
    BOOT_U32 vector = K6_NEGATIVE(36) ? STRAY_TEST_VECTOR : TIMER_VECTOR;
    // Intel SDM 3A §13.5.4: divide 3E0h = 3 (by 16), LVT 320h one-shot unmasked, initial count 380h.
    if (!x86_timer_confirm_write(mapping, 0x3e0, 3) || !x86_timer_confirm_write(mapping, 0x320, vector) ||
        !x86_timer_write(mapping, 0x380, process_rt.Timer->Reload)) process_die("PROCESS_TIMER_FAILED");
}

// ====== process_timer_start ======

void process_timer_start(void)
{
    process_rt.BudgetLeft = x86_process_budget_initial();
    BOOT_U32 duration = PROCESS_BUDGET_MS;
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_delay_next_syscall) duration = 100;
#endif
    if (!kernel_deadline_start(process_rt.BudgetClock, kernel_clock_ms(process_rt.BudgetClock, duration), &process_rt.BudgetDeadline))
        process_die("PROCESS_CLOCK_FAILED");
    process_timer_arm();
}

// ====== process_timer_quiesce ======

void process_timer_quiesce(void)
{
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

// ====== x86_process_irq_service ======

int x86_process_irq_service(BOOT_U64 *frame)
{
    BOOT_U32 value, index;
    if ((BOOT_U64)&IRET(frame, 0) != process_stack_top - 40) process_die("PROCESS_IRQ_INVALID");
    // Intel SDM 3A §13.8.4: ISR 100h-170h, one 32-bit bank per 16-byte step, indexed by vector.
    for (index = 0; index < 8; ++index) {
        if (!x86_lapic_native_read(0x100 + index * 16, &value) ||
            value != (index == TIMER_VECTOR / 32 ? 1U << (TIMER_VECTOR % 32) : 0U)) process_die("PROCESS_IRQ_INVALID");
    }
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_lapic_native_eoi()) process_die("PROCESS_IRQ_INVALID");
    ++process_rt.Current->Irqs;
    process_rt.Current->Rip = IRET(frame, 0);
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        process_rt.Current->Kind = X86_PROCESS_BAD_FRAME;
        return 2;
    }
    process_rt.BudgetLeft = x86_process_budget_next(process_rt.BudgetLeft);
    if (process_budget_expired(IRET(frame, 0))) return 2;
    if (!x86_timer_write(process_rt.Timer->Mapping, 0x380, process_rt.Timer->Reload)) process_die("PROCESS_TIMER_FAILED");
    return 1;
}

// ====== input_irq_deliver ======

static int input_irq_deliver(BOOT_U32 vector)
{
    if (vector < 0x40U || vector >= 0x40U + X86_IRQ_CAPACITY) return 0;
    BOOT_U32 index = vector - 0x40U;
    X86_IRQ_REGISTRY *registry = x86_irq_registry();
    return (process_rt.InputIrqMask & (1U << index)) &&
        x86_irq_registry_token_live(registry, &process_rt.InputIrqTokens[index]) &&
        x86_irq_registry_deliver(registry, vector);
}

// ====== x86_process_device_irq_service ======

int x86_process_device_irq_service(BOOT_U64 *frame, BOOT_U32 vector)
{
    if (!process_rt.Current || (BOOT_U64)&IRET(frame, 0) != process_stack_top - 40 ||
        !input_irq_deliver(vector))
        process_die("PROCESS_DEVICE_IRQ_INVALID");
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) {
        if (process_probe_delivered) process_die("PROCESS_DEVICE_IRQ_INVALID");
        process_probe_delivered = 1;
    }
#endif
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        process_rt.Current->Kind = X86_PROCESS_BAD_FRAME;
        return 2;
    }
    return process_budget_expired(IRET(frame, 0)) ? 2 : 1;
}

// ====== x86_process_device_irq_kernel ======

void x86_process_device_irq_kernel(BOOT_U32 vector)
{
    if (!process_rt.Opened || !process_drain || !input_irq_deliver(vector))
        process_die("PROCESS_DEVICE_IRQ_INVALID");
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) {
        if (process_rt.Current || process_probe_delivered != 1) process_die("PROCESS_DEVICE_IRQ_INVALID");
        process_probe_delivered = 2;
    }
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
