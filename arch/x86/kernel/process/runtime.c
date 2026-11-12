#include "internal.h"

volatile BOOT_U64 k6_owner, k6_kernel_root, k6_user_root, k6_stack_top, k6_user_rsp, k6_saved_rsp;
#ifdef KERNEL_TEST_K6_PROCESS
volatile BOOT_U64 k6_window, k6_window_budget, k6_negative = KERNEL_K6_NEGATIVE;
volatile BOOT_U64 k6_nmi_armed, k6_nmi_count, k6_nmi_rip, k6_nmi_cs, k6_nmi_cr3, k6_nmi_gs, k6_nmi_flags;
#endif
volatile BOOT_U64 k6_fault[7], k6_fault_cr2, k6_exit_kind, k6_drain;
_Alignas(16) unsigned char k6_fx_saved[512];
// Intel SDM vol. 2, FXSAVE64 image: FCW 037fh and MXCSR 1f80h, clean x87/SSE data.
_Alignas(16) const unsigned char k6_fx_clean[512] = {
    [0] = 0x7f, [1] = 0x03, [24] = 0x80, [25] = 0x1f
};

X86_PROCESS_RUNTIME process_rt;

// ====== process_die ======

__declspec(noreturn) void process_die(const char *reason)
{
    x86_exception_load_idt(&process_rt.IdtPrevious);
    (void)kernel_log(3, reason);
    kernel_panic(reason);
}

// ====== gate ======

static void gate(BOOT_U32 vector, BOOT_U64 target, unsigned char ist)
{
    // Intel SDM 3A §7.14.1: selector 8, IST in byte 4, present DPL0 interrupt gate 8eh.
    unsigned char *entry = process_rt.Idt.Gates[vector];
    for (BOOT_U32 index = 0; index < 16; ++index) entry[index] = 0;
    entry[0] = (unsigned char)target;
    entry[1] = (unsigned char)(target >> 8);
    entry[2] = 8;
    entry[4] = ist;
    entry[5] = 0x8e;
    for (BOOT_U32 index = 6; index < 12; ++index) entry[index] = (unsigned char)(target >> ((index - 4) * 8));
}

// ====== idt_prepare ======

static void idt_prepare(void)
{
    BOOT_U64 base = (BOOT_U64)&process_rt.Idt;
    for (BOOT_U32 vector = 0; vector < 32; ++vector) gate(vector, (BOOT_U64)x86_process_stop, 3);
    for (BOOT_U32 vector = 32; vector < 256; ++vector) gate(vector, (BOOT_U64)x86_process_stray, 3);
    for (BOOT_U32 vector = 0; vector < 32; ++vector) {
        if (x86_process_fault_targets[vector]) gate(vector, x86_process_fault_targets[vector], 1);
    }
    gate(2, (BOOT_U64)x86_process_nmi, 2);
    gate(TIMER_VECTOR, (BOOT_U64)x86_process_irq, 0);
    gate(SPURIOUS_VECTOR, (BOOT_U64)x86_process_spurious, 0);
#ifdef KERNEL_TEST_K6_PROCESS
    // Intel SDM 3A §7.14.1: DPL 3 in the type byte lets the diagnostic program reach this gate with INT.
    process_rt.Idt.Gates[SPURIOUS_VECTOR][5] = 0xee;
#endif
    process_rt.IdtProbe.Bytes[0] = 255;
    process_rt.IdtProbe.Bytes[1] = 15;
    for (BOOT_U32 index = 0; index < 8; ++index) process_rt.IdtProbe.Bytes[index + 2] = (unsigned char)(base >> (index * 8));
}

// ====== tss_rsp0 ======

static volatile unsigned char *tss_rsp0(void)
{
    // Intel SDM 3A §10.2.3: 64-bit busy TSS descriptor, base 15:0, 23:16, 31:24, 63:32. §10.7: RSP0 at +4.
    X86_GDT_POINTER pointer;
    BOOT_U64 table = 0;
    x86_kernel_segments_read(&pointer);
    for (BOOT_U32 index = 0; index < 8; ++index) table |= (BOOT_U64)pointer.Bytes[index + 2] << (8 * index);
    if (pointer.Bytes[0] != X86_KERNEL_GDT_LIMIT || pointer.Bytes[1] || !table) return 0;
    BOOT_U64 low = ((const BOOT_U64 *)table)[3], high = ((const BOOT_U64 *)table)[4];
    BOOT_U64 base = ((low >> 16) & 0xffffffULL) | (((low >> 56) & 0xffULL) << 24) | ((high & 0xffffffffULL) << 32);
    if (((low >> 40) & 0x9f) != 0x8b || (base & 15)) return 0;
    return (volatile unsigned char *)(base + 4);
}

// ====== rsp0_swap ======

static BOOT_U64 rsp0_swap(BOOT_U64 value)
{
    BOOT_U64 previous = 0;
    for (BOOT_U32 index = 0; index < 8; ++index) previous |= (BOOT_U64)process_rt.TssField[index] << (8 * index);
    for (BOOT_U32 index = 0; index < 8; ++index) process_rt.TssField[index] = (unsigned char)(value >> (8 * index));
    return previous;
}

// ====== x86_process_open ======

int x86_process_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (process_rt.Opened || !window || !timer || !owner || !x86_user_mode_allowed() ||
        !x86_exception_environment() || x86_percpu_current_local() != owner ||
        __readmsr(MSR_GS_BASE) != (BOOT_U64)owner || (__readmsr(MSR_EFER) & EFER_SCE)) return 0;
    const KERNEL_LOCK_WAIT *wait = x86_percpu_wait_current();
    if (!wait) return 0;
    const volatile BOOT_U64 *root = x86_window_entries(0);
    for (BOOT_U32 slot = 0; slot < 256; ++slot) if (root[slot]) return 0;
    process_rt.TssField = tss_rsp0();
    if (!process_rt.TssField || !x86_timer_read(timer->Mapping, 0x320, &process_rt.TimerLvtSaved) ||
        !x86_timer_read(timer->Mapping, 0x3e0, &process_rt.TimerDivideSaved) ||
        !x86_kernel_stacks_acquire_for(&process_rt.SyscallStack, window->Pool, __readcr3(), 3)) return 0;
    process_rt.Window = window;
    process_rt.Timer = timer;
    process_rt.BudgetClock = &wait->Clock;
    k6_owner = (BOOT_U64)owner;
    k6_kernel_root = __readcr3();
    k6_stack_top = X86_KERNEL_SYSCALL_TOP;
    STACK_WORD = STACK_CANARY;
    process_rt.ExpectedImage = root[SLOT_IMAGE];
    process_rt.ExpectedWindows = root[X86_LAYOUT_WINDOWS_SLOT];
    if (K6_NEGATIVE(33)) process_rt.ExpectedWindows ^= 0x1000;
    idt_prepare();
    x86_exception_read_idt(&process_rt.IdtPrevious);
    x86_exception_load_idt(&process_rt.IdtProbe);
    process_rt.Rsp0Previous = rsp0_swap(k6_stack_top);
    process_rt.AlternateSaved = __readmsr(MSR_KERNEL_GS);
    __writemsr(MSR_KERNEL_GS, 0);
    process_rt.MsrSaved[0] = __readmsr(MSR_STAR);
    process_rt.MsrSaved[1] = __readmsr(MSR_LSTAR);
    process_rt.MsrSaved[2] = __readmsr(MSR_CSTAR);
    process_rt.MsrSaved[3] = __readmsr(MSR_FMASK);
    process_rt.MsrSaved[4] = __readmsr(MSR_EFER);
    __writemsr(MSR_STAR, STAR_VALUE);
    __writemsr(MSR_LSTAR, (BOOT_U64)x86_process_syscall);
    __writemsr(MSR_CSTAR, (BOOT_U64)x86_process_stop);
    __writemsr(MSR_FMASK, FLAGS_MASK);
    __writemsr(MSR_EFER, process_rt.MsrSaved[4] | EFER_SCE);
    process_rt.Opened = 1;
    return 1;
}

// ====== x86_process_close ======

int x86_process_close(void)
{
    if (!process_rt.Opened) return 0;
    if (!x86_timer_stop(process_rt.Timer->Mapping) ||
        !x86_timer_confirm_write(process_rt.Timer->Mapping, 0x3e0, process_rt.TimerDivideSaved) ||
        !x86_timer_confirm_write(process_rt.Timer->Mapping, 0x320, process_rt.TimerLvtSaved)) process_die("PROCESS_TIMER_RETAINED");
    __writemsr(MSR_EFER, process_rt.MsrSaved[4]);
    __writemsr(MSR_STAR, process_rt.MsrSaved[0]);
    __writemsr(MSR_LSTAR, process_rt.MsrSaved[1]);
    __writemsr(MSR_CSTAR, process_rt.MsrSaved[2]);
    __writemsr(MSR_FMASK, process_rt.MsrSaved[3]);
    __writemsr(MSR_KERNEL_GS, process_rt.AlternateSaved);
    if (rsp0_swap(process_rt.Rsp0Previous) != k6_stack_top) process_die("PROCESS_RSP0_CHANGED");
    if (!x86_kernel_stacks_release(&process_rt.SyscallStack)) process_die("PROCESS_STACK_RETAINED");
    x86_exception_load_idt(&process_rt.IdtPrevious);
    process_rt.InputIrqMask = 0;
    k6_owner = 0;
    process_rt.Opened = 0;
    return !(__readmsr(MSR_EFER) & EFER_SCE);
}

// ====== x86_process_input_irqs ======

int x86_process_input_irqs(BOOT_U32 mask)
{
    static void (*const entries[X86_IRQ_CAPACITY])(void) = {
        x86_process_device_irq40, x86_process_device_irq41,
        x86_process_device_irq42, x86_process_device_irq43,
    };
    X86_IRQ_TOKEN tokens[X86_IRQ_CAPACITY] = {0};
    X86_IRQ_REGISTRY *registry = x86_irq_registry();
    if (!process_rt.Opened || process_rt.Current || !registry || registry->Busy ||
        x86_percpu_current_local() != x86_percpu_bsp() || (mask & ~15U)) return 0;
    for (BOOT_U32 index = 0; index < X86_IRQ_CAPACITY; ++index) {
        if (!(mask & (1U << index))) continue;
        const X86_IRQ_SUBSCRIPTION *source = &registry->Slots[index];
        tokens[index] = (X86_IRQ_TOKEN){source->Id, 0x40U + index, 0};
        if (!x86_irq_registry_token_live(registry, &tokens[index])) return 0;
    }
    for (BOOT_U32 index = 0; index < X86_IRQ_CAPACITY; ++index) {
        int enabled = (mask & (1U << index)) != 0;
        process_rt.InputIrqTokens[index] = tokens[index];
        gate(0x40U + index, (BOOT_U64)(enabled ? entries[index] : x86_process_stray), enabled ? 0 : 3);
    }
    process_rt.InputIrqMask = mask;
    return 1;
}
