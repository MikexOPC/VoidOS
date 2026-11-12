#include "internal.h"

#ifdef KERNEL_TEST_K6_PROCESS

BOOT_U32 process_delay_next_syscall, process_probe_armed, process_probe_delivered;
static X86_IRQ_TOKEN device_probe_token;
static int (*device_probe_inject)(void);

// ====== process_probe_send ======

void process_probe_send(void)
{
    BOOT_U32 value;
    if (!device_probe_inject || !device_probe_inject())
        process_die("PROCESS_DEVICE_IRQ_ARM_FAILED");
    // Intel SDM §12.8.4: IRR bank +200h + 16*(vector/32), bit vector%32.
    for (BOOT_U32 attempt = 0; attempt < DRAIN_ATTEMPTS; ++attempt) {
        if (!x86_lapic_native_read(IRR_TIMER_BANK, &value)) break;
        if (value & IRR_DEVICE_BIT) return;
        _mm_pause();
    }
    process_die("PROCESS_DEVICE_IRQ_ARM_FAILED");
}

// ====== device_probe_quiet ======

static int device_probe_quiet(void)
{
    return !process_probe_armed && x86_lapic_idle(process_rt.Timer->Mapping);
}

// ====== x86_process_device_irq_check ======

int x86_process_device_irq_check(const X86_PROCESS_IMAGE *image, int (*inject)(void), int (*handle)(void))
{
    X86_PROCESS_RESULT result;
    X86_IRQ_REGISTRY *registry = x86_irq_registry();
    if (!process_rt.Opened || process_rt.Current || !registry || !inject || !handle ||
        x86_process_input_irqs(16) ||
        x86_process_input_irqs(1U << (DEVICE_PROBE_VECTOR - 0x40U)) ||
        !x86_irq_registry_bind(registry, DEVICE_PROBE_VECTOR, handle,
            device_probe_quiet, &device_probe_token)) return 0;
    if (x86_process_input_irqs(1U << (DEVICE_PROBE_VECTOR - 0x40U)) != 1) {
        if (!x86_irq_registry_unbind(registry, &device_probe_token)) process_die("PROCESS_DEVICE_IRQ_RETAINED");
        return 0;
    }
    process_probe_delivered = 0;
    process_probe_armed = 1;
    device_probe_inject = inject;
    int ran = x86_process_run(image, &result);
    int resumed = ran && process_probe_delivered == 1 &&
        result.Kind == X86_PROCESS_EXIT && result.Code == 0 && result.Writes == 1;
    int drained_ok = resumed && x86_process_drain_check();
    process_probe_armed = 0;
    device_probe_inject = 0;
    BOOT_U64 deliveries = x86_irq_registry_deliveries(registry, &device_probe_token);
    if (!x86_process_input_irqs(0) || !x86_irq_registry_unbind(registry, &device_probe_token))
        process_die("PROCESS_DEVICE_IRQ_RETAINED");
    return drained_ok && process_probe_delivered == 2 && deliveries == 2;
}

// ====== x86_process_nmi_arm ======

void x86_process_nmi_arm(BOOT_U64 window)
{
    k6_nmi_count = 0;
    k6_nmi_rip = 0;
    k6_window_budget = 0x4000000ULL;
    k6_nmi_armed = 1;
    k6_window = window;
}

// ====== x86_process_nmi_report ======

int x86_process_nmi_report(BOOT_U64 window)
{
    BOOT_U32 user_root = k6_nmi_cr3 != k6_kernel_root;
    BOOT_U32 kernel_gs = k6_nmi_gs == k6_owner;
    BOOT_U32 ac = (k6_nmi_flags & FLAG_AC) != 0;
    k6_nmi_armed = 0;
    return k6_nmi_count == 1 && k6_window == 0 &&
        k6_nmi_rip >= x86_process_window_first(window) && k6_nmi_rip < x86_process_window_last(window) &&
        x86_process_nmi_valid(window, user_root, kernel_gs, ac, k6_nmi_cs);
}

// ====== x86_process_window_first ======

BOOT_U64 x86_process_window_first(BOOT_U64 window)
{
    const unsigned char *first[6] = {x86_process_window_1_first, x86_process_window_2_first,
        x86_process_window_3_first, x86_process_window_4_first, x86_process_window_5_first, x86_process_window_6_first};
    return window >= 1 && window <= 6 ? (BOOT_U64)first[window - 1] : 0;
}

// ====== x86_process_window_last ======

BOOT_U64 x86_process_window_last(BOOT_U64 window)
{
    const unsigned char *last[6] = {x86_process_window_1_last, x86_process_window_2_last,
        x86_process_window_3_last, x86_process_window_4_last, x86_process_window_5_last, x86_process_window_6_last};
    return window >= 1 && window <= 6 ? (BOOT_U64)last[window - 1] : 0;
}

// ====== x86_process_copy_instruction ======

BOOT_U64 x86_process_copy_instruction(void)
{
    return (BOOT_U64)x86_process_copy_first;
}

// ====== x86_process_budget_ticks ======

BOOT_U64 x86_process_budget_ticks(void)
{
    return x86_process_budget_initial();
}

// ====== x86_process_delay_next_syscall ======

void x86_process_delay_next_syscall(void)
{
    process_delay_next_syscall = 1;
}

// ====== x86_process_drain_check ======

int x86_process_drain_check(void)
{
    const X86_LAPIC_MAPPING *mapping = process_rt.Timer->Mapping;
    BOOT_U64 before = process_rt.Drained;
    BOOT_U32 pending = 0;
    if (!x86_timer_confirm_write(mapping, 0x3e0, 3) || !x86_timer_confirm_write(mapping, 0x320, TIMER_VECTOR) ||
        !x86_timer_write(mapping, 0x380, 1)) return 0;
    for (BOOT_U32 attempt = 0; attempt < 0x1000000U && !(pending & IRR_TIMER_BIT); ++attempt) {
        if (!x86_lapic_native_read(IRR_TIMER_BANK, &pending)) return 0;
    }
    if (!(pending & IRR_TIMER_BIT)) return 0;
    if (process_probe_armed) process_probe_send();
    process_timer_quiesce();
    return process_rt.Drained == before + 1;
}

#endif
