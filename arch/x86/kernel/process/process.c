#include "process.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>

// Intel SDM 3A §§5.5.4, 5.6: P bit 0, R/W bit 1, U/S bit 2; XD bit 63.
#define PTE_PRESENT 1ULL
#define PTE_WRITE 2ULL
#define PTE_USER 4ULL
#define PTE_NX (1ULL << 63)
#define PTE_ADDRESS 0x000ffffffffff000ULL
#define PAGE_BYTES 4096ULL
#define TABLE_PAGES 4U
#define STACK_PAGES 2U
#define STACK_BASE 0x5fe000ULL
#define STACK_TOP 0x600000ULL
#define IMAGE_BASE 0x400000ULL
#define PT_OFFSET(address) (1536U + (BOOT_U32)(((address) - IMAGE_BASE) >> 12))
#define SLOT_IMAGE 511U
#define MAX_IMAGE_PAGES 10U
#define MAX_COPY 256U
#define TIMER_VECTOR 0x41U
#define STACK_BYTES 16384U
#define STACK_CANARY 0x4b36435441434bULL
#define RESULT_RANGE (~0ULL - 1)
#define RESULT_LENGTH (~0ULL - 2)
#define RESULT_UNKNOWN (~0ULL)
#define SYS_EXIT 0ULL
#define SYS_WRITE 1ULL
// Intel SDM 2B, SYSCALL and SYSRET; 3A §2.2, §2.3, §6.8.8: STAR, LSTAR, CSTAR, FMASK, EFER.SCE.
#define MSR_EFER 0xc0000080U
#define MSR_STAR 0xc0000081U
#define MSR_LSTAR 0xc0000082U
#define MSR_CSTAR 0xc0000083U
#define MSR_FMASK 0xc0000084U
#define MSR_FS_BASE 0xc0000100U
#define MSR_GS_BASE 0xc0000101U
#define MSR_KERNEL_GS 0xc0000102U
#define EFER_SCE 1ULL
#define FLAGS_MASK 0x47700ULL
#define STAR_VALUE ((0x23ULL << 48) | (8ULL << 32))
#define FLAG_AC (1ULL << 18)
#define FX_WORDS 64U
#define GPR(frame, index) ((frame)[FX_WORDS + (index)])
#define IRET(frame, index) ((frame)[FX_WORDS + 15U + (index)])
#define GPR_RSI 9U
#define GPR_RDI 8U
#define GPR_RAX 14U

#ifndef KERNEL_K6_NEGATIVE
#define KERNEL_K6_NEGATIVE 0
#endif

typedef struct { BOOT_U64 Entry, Count; struct { BOOT_U64 Virt, FileOffset, FileBytes, Flags; } Pages[MAX_IMAGE_PAGES]; } PROCESS_PLAN;

extern const BOOT_U64 x86_process_fault_targets[32];
extern const unsigned char x86_process_copy_first[], x86_process_copy_last[], x86_process_copy_fixup[];
#define WINDOW_LABELS(number) \
    extern const unsigned char x86_process_window_##number##_first[], x86_process_window_##number##_last[]
WINDOW_LABELS(1); WINDOW_LABELS(2); WINDOW_LABELS(3); WINDOW_LABELS(4); WINDOW_LABELS(5); WINDOW_LABELS(6);
void x86_process_syscall(void);
void x86_process_nmi(void);
void x86_process_irq(void);
__declspec(noreturn) void x86_process_stop(void);
void x86_process_enter(BOOT_U64 entry, BOOT_U64 stack);
BOOT_U64 x86_process_copy(void *target, const void *source, BOOT_U64 bytes);
void x86_process_probe_read(BOOT_U64 address);
BOOT_U32 x86_process_plan(const unsigned char *bytes, BOOT_U64 length, PROCESS_PLAN *plan);
BOOT_U32 x86_process_copy_range(BOOT_U64 pointer, BOOT_U64 length);
BOOT_U32 x86_process_return_valid(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);
BOOT_U32 x86_process_irq_frame_valid(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);
BOOT_U32 x86_process_fault_recoverable(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 error, BOOT_U64 cr2,
    BOOT_U64 first, BOOT_U64 last, BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write);
BOOT_U32 x86_process_nmi_valid(BOOT_U64 window, BOOT_U32 user_root, BOOT_U32 kernel_gs, BOOT_U32 ac, BOOT_U64 cs);
void x86_process_console_filter(unsigned char *bytes, BOOT_U64 length);
BOOT_U32 x86_process_state_next(BOOT_U32 state, BOOT_U32 next);

volatile BOOT_U64 k6_owner, k6_kernel_root, k6_user_root, k6_stack_top, k6_user_rsp, k6_saved_rsp;
volatile BOOT_U64 k6_window, k6_window_budget, k6_negative = KERNEL_K6_NEGATIVE;
volatile BOOT_U64 k6_nmi_armed, k6_nmi_count, k6_nmi_rip, k6_nmi_cs, k6_nmi_cr3, k6_nmi_gs, k6_nmi_flags;
volatile BOOT_U64 k6_fault[7], k6_fault_cr2, k6_exit_kind;
_Alignas(16) unsigned char k6_fx_saved[512];
// Intel SDM vol. 2, FXSAVE64 image: FCW 037fh and MXCSR 1f80h, clean x87/SSE data.
_Alignas(16) const unsigned char k6_fx_clean[512] = {
    [0] = 0x7f, [1] = 0x03, [24] = 0x80, [25] = 0x1f
};

static _Alignas(4096) BOOT_U64 kernel_stack[STACK_BYTES / 8];
static X86_IRQ_IDT idt;
static X86_KERNEL_IDTR idt_previous, idt_probe;
static X86_PAGE_WINDOW *owner_window;
static X86_LAPIC_TIMER *owner_timer;
static volatile unsigned char *tss_field;
static BOOT_U64 rsp0_previous, msr_saved[5], alternate_saved;
static BOOT_U64 expected_image, expected_windows;
static KERNEL_PHYSICAL_LEASE held;
static BOOT_U32 state, opened;
static X86_PROCESS_RESULT *current;
static volatile BOOT_U64 window_pointer, window_length, window_write;
static _Alignas(16) unsigned char buffer[MAX_COPY + 1];

// ====== advance ======

static void advance(BOOT_U32 next)
{
    if (!x86_process_state_next(state, next)) x86_process_stop();
    state = next;
}

// ====== gate ======

static void gate(BOOT_U32 vector, BOOT_U64 target, unsigned char ist)
{
    // Intel SDM 3A §7.14.1: selector 8, IST in byte 4, present DPL0 interrupt gate 8eh.
    unsigned char *entry = idt.Gates[vector];
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
    BOOT_U64 base = (BOOT_U64)&idt;
    for (BOOT_U32 vector = 0; vector < 256; ++vector) gate(vector, (BOOT_U64)x86_process_stop, 3);
    for (BOOT_U32 vector = 0; vector < 32; ++vector) {
        if (x86_process_fault_targets[vector]) gate(vector, x86_process_fault_targets[vector], 1);
    }
    gate(2, (BOOT_U64)x86_process_nmi, 2);
    gate(TIMER_VECTOR, (BOOT_U64)x86_process_irq, 0);
    idt_probe.Bytes[0] = 255;
    idt_probe.Bytes[1] = 15;
    for (BOOT_U32 index = 0; index < 8; ++index) idt_probe.Bytes[index + 2] = (unsigned char)(base >> (index * 8));
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
    for (BOOT_U32 index = 0; index < 8; ++index) previous |= (BOOT_U64)tss_field[index] << (8 * index);
    for (BOOT_U32 index = 0; index < 8; ++index) tss_field[index] = (unsigned char)(value >> (8 * index));
    return previous;
}

// ====== x86_process_open ======

int x86_process_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (opened || !window || !timer || !owner || !x86_user_mode_allowed() ||
        !x86_exception_environment() || x86_percpu_current_local() != owner ||
        __readmsr(MSR_GS_BASE) != (BOOT_U64)owner || (__readmsr(MSR_EFER) & EFER_SCE)) return 0;
    const volatile BOOT_U64 *root = x86_window_entries(0);
    for (BOOT_U32 slot = 0; slot < 256; ++slot) if (root[slot]) return 0;
    tss_field = tss_rsp0();
    if (!tss_field) return 0;
    owner_window = window;
    owner_timer = timer;
    k6_owner = (BOOT_U64)owner;
    k6_kernel_root = __readcr3();
    k6_stack_top = (BOOT_U64)kernel_stack + STACK_BYTES;
    kernel_stack[0] = STACK_CANARY;
    expected_image = root[SLOT_IMAGE];
    expected_windows = root[X86_LAYOUT_WINDOWS_SLOT];
    if (k6_negative == 33) expected_windows ^= 0x1000;
    idt_prepare();
    x86_exception_read_idt(&idt_previous);
    x86_exception_load_idt(&idt_probe);
    rsp0_previous = rsp0_swap(k6_stack_top);
    alternate_saved = __readmsr(MSR_KERNEL_GS);
    __writemsr(MSR_KERNEL_GS, 0);
    msr_saved[0] = __readmsr(MSR_STAR);
    msr_saved[1] = __readmsr(MSR_LSTAR);
    msr_saved[2] = __readmsr(MSR_CSTAR);
    msr_saved[3] = __readmsr(MSR_FMASK);
    msr_saved[4] = __readmsr(MSR_EFER);
    __writemsr(MSR_STAR, STAR_VALUE);
    __writemsr(MSR_LSTAR, (BOOT_U64)x86_process_syscall);
    __writemsr(MSR_CSTAR, (BOOT_U64)x86_process_stop);
    __writemsr(MSR_FMASK, FLAGS_MASK);
    __writemsr(MSR_EFER, msr_saved[4] | EFER_SCE);
    opened = 1;
    return 1;
}

// ====== x86_process_close ======

int x86_process_close(void)
{
    if (!opened) return 0;
    __writemsr(MSR_EFER, msr_saved[4]);
    __writemsr(MSR_STAR, msr_saved[0]);
    __writemsr(MSR_LSTAR, msr_saved[1]);
    __writemsr(MSR_CSTAR, msr_saved[2]);
    __writemsr(MSR_FMASK, msr_saved[3]);
    __writemsr(MSR_KERNEL_GS, alternate_saved);
    if (rsp0_swap(rsp0_previous) != k6_stack_top) x86_process_stop();
    x86_exception_load_idt(&idt_previous);
    k6_owner = 0;
    opened = 0;
    return !(__readmsr(MSR_EFER) & EFER_SCE);
}

// ====== roots_stable ======

static int roots_stable(void)
{
    const volatile BOOT_U64 *root = x86_window_entries(0);
    return root[SLOT_IMAGE] == expected_image && root[X86_LAYOUT_WINDOWS_SLOT] == expected_windows;
}

// ====== map_image ======

static int map_image(const X86_PROCESS_IMAGE *image, const PROCESS_PLAN *plan)
{
    BOOT_U64 pages = TABLE_PAGES + plan->Count + STACK_PAGES;
    if (!x86_page_window_idle_valid(owner_window) || !x86_page_window_acquire(owner_window, pages)) return 0;
    volatile unsigned char *alias = x86_window_bytes();
    volatile BOOT_U64 *tables = (volatile BOOT_U64 *)alias;
    BOOT_U64 base = owner_window->Lease.Base;
    BOOT_U64 branch = PTE_PRESENT | PTE_WRITE | PTE_USER;
    for (BOOT_U64 index = 0; index < pages * PAGE_BYTES; ++index) alias[index] = 0;
    tables[0] = (base + PAGE_BYTES) | branch;
    tables[SLOT_IMAGE] = x86_window_entries(0)[SLOT_IMAGE];
    tables[X86_LAYOUT_WINDOWS_SLOT] = x86_window_entries(0)[X86_LAYOUT_WINDOWS_SLOT];
    tables[512] = (base + 2 * PAGE_BYTES) | branch;
    tables[1024 + 2] = (base + 3 * PAGE_BYTES) | branch;
    for (BOOT_U64 index = 0; index < plan->Count; ++index) {
        BOOT_U64 lease = TABLE_PAGES + index;
        BOOT_U64 flags = PTE_PRESENT | PTE_USER;
        if (plan->Pages[index].Flags & 1) flags |= PTE_WRITE;
        if (!(plan->Pages[index].Flags & 2)) flags |= PTE_NX;
        tables[PT_OFFSET(plan->Pages[index].Virt)] = (base + lease * PAGE_BYTES) | flags;
        for (BOOT_U64 byte = 0; byte < plan->Pages[index].FileBytes; ++byte)
            alias[lease * PAGE_BYTES + byte] = image->Bytes[plan->Pages[index].FileOffset + byte];
    }
    for (BOOT_U64 index = 0; index < STACK_PAGES; ++index) {
        BOOT_U64 lease = TABLE_PAGES + plan->Count + index;
        tables[PT_OFFSET(STACK_BASE + index * PAGE_BYTES)] = (base + lease * PAGE_BYTES) | branch | PTE_NX;
    }
    if (!x86_page_window_park(owner_window, &held)) x86_process_stop();
    k6_user_root = base;
    return 1;
}

// ====== reap ======

static int reap(BOOT_U64 pages, BOOT_U64 budget)
{
    if (__readcr3() != k6_kernel_root || !roots_stable() || kernel_stack[0] != STACK_CANARY) return 0;
    if (!x86_page_window_resume(owner_window, &held)) x86_process_stop();
    volatile unsigned char *alias = x86_window_bytes();
    for (BOOT_U64 index = 0; index < pages * PAGE_BYTES; ++index) alias[index] = 0;
    if (!x86_page_window_release(owner_window)) x86_process_stop();
    k6_user_root = 0;
    return x86_page_window_idle_valid(owner_window) && kernel_physical_budget_matches(owner_window->Pool, budget);
}

// ====== x86_process_run ======

int x86_process_run(const X86_PROCESS_IMAGE *image, X86_PROCESS_RESULT *result)
{
    static PROCESS_PLAN plan;
    BOOT_U64 budget;
    if (!opened || !image || !result) return 0;
    for (BOOT_U32 index = 0; index < 8; ++index) ((BOOT_U64 *)result)[index] = 0;
    state = 0;
    advance(1);
    if (!kernel_physical_budget_read(owner_window->Pool, &budget)) return 0;
    BOOT_U32 rejected = x86_process_plan(image->Bytes, image->Length, &plan);
    if (rejected) {
        result->Kind = X86_PROCESS_REJECTED;
        result->Code = rejected;
        advance(5);
        return kernel_physical_budget_matches(owner_window->Pool, budget);
    }
    if (!map_image(image, &plan)) return 0;
    advance(2);
    current = result;
    k6_exit_kind = 0;
    advance(3);
    x86_process_enter(plan.Entry, STACK_TOP);
    advance(4);
    if (k6_exit_kind == X86_PROCESS_FAULT) {
        result->Kind = X86_PROCESS_FAULT;
        result->Vector = k6_fault[0];
        result->Error = k6_fault[1];
        result->Rip = k6_fault[2];
        result->Cr2 = k6_fault_cr2;
    }
    __writemsr(MSR_FS_BASE, 0);
    __writemsr(MSR_KERNEL_GS, 0);
    current = 0;
    if (!reap(TABLE_PAGES + plan.Count + STACK_PAGES, budget)) {
        const char *reason = roots_stable() ? "PROCESS_REAP_FAILED" : "PROCESS_ROOT_CHANGED";
        (void)kernel_log(3, reason);
        kernel_panic(reason);
    }
    advance(5);
    return result->Kind != 0;
}

// ====== copy_checked ======

static BOOT_U64 copy_checked(void *target, const void *source, BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write)
{
    window_pointer = pointer;
    window_length = length;
    window_write = write;
    BOOT_U64 failed = x86_process_copy(target, source, length);
    window_length = 0;
    if (__readeflags() & FLAG_AC) x86_process_stop();
    return failed;
}

// ====== sys_write ======

static BOOT_U64 sys_write(BOOT_U64 pointer, BOOT_U64 length)
{
    BOOT_U32 range = x86_process_copy_range(pointer, length);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    if (!length) return 0;
    if (copy_checked(buffer, (const void *)pointer, pointer, length, 0)) {
        for (BOOT_U32 index = 0; index <= MAX_COPY; ++index) ((volatile unsigned char *)buffer)[index] = 0;
        return RESULT_RANGE;
    }
    x86_process_console_filter(buffer, length);
    buffer[length] = 0;
    if (!kernel_text("USER: ") || !kernel_text((const char *)buffer)) x86_process_stop();
    if (buffer[length - 1] != '\n') {
        if (!kernel_text("\n")) x86_process_stop();
    }
    ++current->Writes;
    return length;
}

// ====== x86_process_syscall_dispatch ======

int x86_process_syscall_dispatch(BOOT_U64 *frame)
{
    if (k6_negative == 31) (void)*(const volatile unsigned char *)0x10ULL;
    if (k6_negative == 32) x86_process_probe_read(X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_HEAP_SLOT));
    if (!x86_process_return_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        current->Kind = X86_PROCESS_BAD_FRAME;
        current->Rip = IRET(frame, 0);
        return 2;
    }
    BOOT_U64 number = GPR(frame, GPR_RAX);
    if (number == SYS_EXIT) {
        current->Kind = X86_PROCESS_EXIT;
        current->Code = GPR(frame, GPR_RDI);
        return 2;
    }
    GPR(frame, GPR_RAX) = number == SYS_WRITE ? sys_write(GPR(frame, GPR_RDI), GPR(frame, GPR_RSI)) : RESULT_UNKNOWN;
    return 1;
}

// ====== x86_process_irq_service ======

int x86_process_irq_service(BOOT_U64 *frame)
{
    BOOT_U32 value, index;
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4)) ||
        (BOOT_U64)&IRET(frame, 0) != k6_stack_top - 40) return 0;
    // Intel SDM 3A §13.8.4: ISR 100h-170h, one 32-bit bank per 16-byte step, indexed by vector.
    for (index = 0; index < 8; ++index) {
        if (!x86_lapic_native_read(0x100 + index * 16, &value) ||
            value != (index == TIMER_VECTOR / 32 ? 1U << (TIMER_VECTOR % 32) : 0U)) return 0;
    }
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_lapic_native_eoi()) return 0;
    ++current->Irqs;
    return 1;
}

// ====== x86_process_copy_recover ======

int x86_process_copy_recover(BOOT_U64 *frame, BOOT_U64 cr2)
{
    enum { VECTOR, ERROR_CODE, RIP, CS, FLAGS };
    if (!x86_process_fault_recoverable(frame[RIP], frame[CS], frame[FLAGS], frame[ERROR_CODE], cr2,
        (BOOT_U64)x86_process_copy_first, (BOOT_U64)x86_process_copy_last,
        window_pointer, window_length, window_write)) return 0;
    frame[RIP] = (BOOT_U64)x86_process_copy_fixup;
    frame[FLAGS] &= ~FLAG_AC;
    return 1;
}

// ====== x86_process_kernel_fatal ======

void x86_process_kernel_fatal(const BOOT_U64 *frame, BOOT_U64 cr2)
{
    x86_exception_load_idt(&idt_previous);
    (void)kernel_text("PROCESS_FAULT_VECTOR: ");
    (void)kernel_number(frame[0]);
    (void)kernel_text("\nPROCESS_FAULT_RIP: ");
    (void)kernel_number(frame[2]);
    (void)kernel_text("\nPROCESS_FAULT_CR2: ");
    (void)kernel_number(cr2);
    (void)kernel_text("\n");
    (void)kernel_log(3, "PROCESS_KERNEL_FAULT");
    kernel_panic("PROCESS_KERNEL_FAULT");
}

// ====== x86_process_timer_arm ======

int x86_process_timer_arm(void)
{
    const X86_LAPIC_MAPPING *mapping = owner_timer->Mapping;
    // Intel SDM 3A §13.5.4: divide 3E0h = 3 (by 16), LVT 320h one-shot unmasked, initial count 380h.
    return x86_timer_confirm_write(mapping, 0x3e0, 3) &&
        x86_timer_confirm_write(mapping, 0x320, TIMER_VECTOR) &&
        x86_timer_write(mapping, 0x380, owner_timer->Reload);
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
