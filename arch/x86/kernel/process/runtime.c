#include "internal.h"
#include "arch/x86/kernel/cpu/percpu/resources/geometry.h"

#ifdef KERNEL_TEST_K6_PROCESS
volatile BOOT_U64 process_window, process_window_budget, process_negative = KERNEL_K6_NEGATIVE;
volatile BOOT_U64 process_nmi_armed, process_nmi_count, process_nmi_rip, process_nmi_cs, process_nmi_cr3, process_nmi_gs, process_nmi_flags;
#endif
volatile BOOT_U64 process_drain;
// Intel SDM vol. 2, FXSAVE64 image: FCW 037fh and MXCSR 1f80h, clean x87/SSE data.
_Alignas(16) const unsigned char process_fx_clean[512] = {
    [0] = 0x7f, [1] = 0x03, [24] = 0x80, [25] = 0x1f
};

X86_PROCESS_RUNTIME process_rt;
X86_PROCESS_CPU x86_process_cpus[X86_PERCPU_CAPACITY];
static BOOT_U32 stack_prepared;

// ====== x86_process_stack_prepare ======

int x86_process_stack_prepare(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    if (!x86_user_mode_allowed())
        return 1;
    if (stack_prepared || !pool || !root)
        return 0;
    if (!x86_kernel_stacks_acquire_for(&process_rt.SyscallStack, pool, root, 3))
        return 0;
    if (!x86_kernel_stacks_commit(&process_rt.SyscallStack)) {
        (void)x86_kernel_stacks_release(&process_rt.SyscallStack);
        return 0;
    }
    stack_prepared = 1;
    return 1;
}

// ====== x86_process_cpu_current ======

X86_PROCESS_CPU *x86_process_cpu_current(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner || owner->Index >= X86_PERCPU_CAPACITY)
        return 0;
    return &x86_process_cpus[owner->Index];
}

// ====== x86_process_in_syscall ======

int x86_process_in_syscall(void)
{
    const X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    return cpu && cpu->InSyscall != 0;
}

// ====== x86_process_runtime_ready ======

int x86_process_runtime_ready(void)
{
    return process_rt.Permanent != 0 && process_rt.Opened != 0;
}

// ====== process_die ======

__declspec(noreturn) void process_die(const char *reason)
{
    if (!process_rt.Permanent)
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

// ====== bind_bsp_cpu_private ======

static int bind_bsp_cpu_private(const X86_CPU_LOCAL *owner, const KERNEL_LOCK_WAIT *wait)
{
    X86_PROCESS_CPU *cpu = &x86_process_cpus[0];
    for (BOOT_U32 index = 0; index < sizeof(*cpu); ++index) ((unsigned char *)cpu)[index] = 0;
    cpu->KernelRoot = __readcr3();
    cpu->SyscallTop = X86_KERNEL_SYSCALL_TOP;
    cpu->Owner = (BOOT_U64)owner;
    cpu->BudgetClock = &wait->Clock;
    return x86_percpu_bind_process_cpu(X86_KERNEL_SYSCALL_TOP, (BOOT_U64)cpu);
}

// ====== x86_process_runtime_start ======

int x86_process_runtime_start(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    if (process_rt.Permanent)
        return 1;
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (process_rt.Opened || !window || !timer || !owner || !x86_user_mode_allowed() ||
        !x86_exception_environment() || x86_percpu_current_local() != owner ||
        __readmsr(MSR_GS_BASE) != (BOOT_U64)owner || (__readmsr(MSR_EFER) & EFER_SCE))
        return 0;
    const KERNEL_LOCK_WAIT *wait = x86_percpu_wait_current();
    if (!wait)
        return 0;
    const volatile BOOT_U64 *root = x86_window_entries(0);
    for (BOOT_U32 slot = 0; slot < 256; ++slot)
        if (root[slot]) return 0;
    process_rt.TssField = tss_rsp0();
    if (!process_rt.TssField)
        return 0;
    if (!stack_prepared) {
        if (!x86_kernel_stacks_acquire_for(&process_rt.SyscallStack, window->Pool, __readcr3(), 3))
            return 0;
    }
    if (!bind_bsp_cpu_private(owner, wait)) {
        if (!stack_prepared)
            (void)x86_kernel_stacks_release(&process_rt.SyscallStack);
        return 0;
    }
    process_rt.Window = window;
    process_rt.Timer = timer;
    STACK_WORD = STACK_CANARY;
    process_rt.ExpectedImage = root[SLOT_IMAGE];
    process_rt.ExpectedWindows = root[X86_LAYOUT_WINDOWS_SLOT];
    process_rt.Rsp0Previous = rsp0_swap(X86_KERNEL_SYSCALL_TOP);
    process_rt.AlternateSaved = __readmsr(MSR_KERNEL_GS);
    __writemsr(MSR_KERNEL_GS, 0);
    process_rt.MsrSaved[0] = __readmsr(MSR_STAR);
    process_rt.MsrSaved[1] = __readmsr(MSR_LSTAR);
    process_rt.MsrSaved[2] = __readmsr(MSR_CSTAR);
    process_rt.MsrSaved[3] = __readmsr(MSR_FMASK);
    process_rt.MsrSaved[4] = __readmsr(MSR_EFER);
    // Intel SDM 3A §6.8.8: permanent SYSCALL configuration on the common IDT.
    __writemsr(MSR_STAR, STAR_VALUE);
    __writemsr(MSR_LSTAR, (BOOT_U64)x86_process_syscall);
    __writemsr(MSR_CSTAR, (BOOT_U64)x86_process_stop);
    __writemsr(MSR_FMASK, FLAGS_MASK);
    __writemsr(MSR_EFER, process_rt.MsrSaved[4] | EFER_SCE);
    x86_user_leave_target = (BOOT_U64)x86_process_leave;
    x86_user_copy_recover_target = (BOOT_U64)x86_process_copy_recover;
    x86_user_irq_capture_target = (BOOT_U64)x86_process_irq_capture;
    x86_user_fx_clean_target = (BOOT_U64)process_fx_clean;
    x86_user_fault_active = 1;
    process_rt.Permanent = 1;
    process_rt.Opened = 1;
    return 1;
}

// ====== x86_process_ap_activate ======

int x86_process_ap_activate(void)
{
    if (!x86_user_mode_allowed())
        return 1;
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner || !owner->Index || owner->Index >= X86_PERCPU_CAPACITY || !owner->Locks.Wait ||
        __readmsr(MSR_GS_BASE) != (BOOT_U64)owner || (__readmsr(MSR_EFER) & EFER_SCE))
        return 0;
    BOOT_U64 syscall_base = X86_AP_SYSCALL_BASE(owner->Index);
    BOOT_U64 syscall_top = X86_AP_SYSCALL_TOP(owner->Index);
    X86_PROCESS_CPU *cpu = &x86_process_cpus[owner->Index];
    for (BOOT_U32 index = 0; index < sizeof(*cpu); ++index) ((unsigned char *)cpu)[index] = 0;
    cpu->KernelRoot = __readcr3();
    cpu->SyscallTop = syscall_top;
    cpu->Owner = (BOOT_U64)owner;
    cpu->BudgetClock = &owner->Locks.Wait->Clock;
    *(volatile BOOT_U64 *)syscall_base = STACK_CANARY;
    if (!x86_percpu_bind_process_cpu(syscall_top, (BOOT_U64)cpu))
        return 0;
    // Intel SDM 3A §6.8.8: per-AP SYSCALL MSR setup matching BSP permanent configuration.
    __writemsr(MSR_KERNEL_GS, 0);
    __writemsr(MSR_STAR, STAR_VALUE);
    __writemsr(MSR_LSTAR, (BOOT_U64)x86_process_syscall);
    __writemsr(MSR_CSTAR, (BOOT_U64)x86_process_stop);
    __writemsr(MSR_FMASK, FLAGS_MASK);
    __writemsr(MSR_EFER, __readmsr(MSR_EFER) | EFER_SCE);
    return (__readmsr(MSR_EFER) & EFER_SCE) != 0;
}

// ====== x86_process_open ======

int x86_process_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    if (process_rt.Permanent)
        return 1;
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
    if (!bind_bsp_cpu_private(owner, wait)) {
        (void)x86_kernel_stacks_release(&process_rt.SyscallStack);
        return 0;
    }
    process_rt.Window = window;
    process_rt.Timer = timer;
    STACK_WORD = STACK_CANARY;
    process_rt.ExpectedImage = root[SLOT_IMAGE];
    process_rt.ExpectedWindows = root[X86_LAYOUT_WINDOWS_SLOT];
    if (K6_NEGATIVE(33)) process_rt.ExpectedWindows ^= 0x1000;
    idt_prepare();
    x86_exception_read_idt(&process_rt.IdtPrevious);
    x86_exception_load_idt(&process_rt.IdtProbe);
    process_rt.Rsp0Previous = rsp0_swap(X86_KERNEL_SYSCALL_TOP);
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
    if (process_rt.Permanent)
        return 1;
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
    if (rsp0_swap(process_rt.Rsp0Previous) != X86_KERNEL_SYSCALL_TOP) process_die("PROCESS_RSP0_CHANGED");
    if (!x86_kernel_stacks_release(&process_rt.SyscallStack)) process_die("PROCESS_STACK_RETAINED");
    x86_exception_load_idt(&process_rt.IdtPrevious);
    (void)x86_percpu_bind_process_cpu(0, 0);
    process_rt.InputIrqMask = 0;
    process_rt.Opened = 0;
    return !(__readmsr(MSR_EFER) & EFER_SCE);
}

// ====== x86_process_input_irqs ======

int x86_process_input_irqs(BOOT_U32 mask)
{
    if (process_rt.Permanent)
        return 1;
    static void (*const entries[X86_IRQ_DEVICE_SLOTS])(void) = {
        x86_process_device_irq40, x86_process_device_irq41,
        x86_process_device_irq42, x86_process_device_irq43,
    };
    X86_IRQ_TOKEN tokens[X86_IRQ_DEVICE_SLOTS] = {0};
    X86_IRQ_REGISTRY *registry = x86_irq_registry();
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !cpu || cpu->Current || !registry || registry->Busy ||
        x86_percpu_current_local() != x86_percpu_bsp() || (mask & ~15U)) return 0;
    for (BOOT_U32 index = 0; index < X86_IRQ_DEVICE_SLOTS; ++index) {
        if (!(mask & (1U << index))) continue;
        const X86_IRQ_SUBSCRIPTION *source = &registry->Slots[index];
        tokens[index] = (X86_IRQ_TOKEN){source->Id, 0x40U + index, 0};
        if (!x86_irq_registry_token_live(registry, &tokens[index])) return 0;
    }
    for (BOOT_U32 index = 0; index < X86_IRQ_DEVICE_SLOTS; ++index) {
        int enabled = (mask & (1U << index)) != 0;
        process_rt.InputIrqTokens[index] = tokens[index];
        gate(0x40U + index, (BOOT_U64)(enabled ? entries[index] : x86_process_stray), enabled ? 0 : 3);
    }
    process_rt.InputIrqMask = mask;
    return 1;
}
