#include "probe.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>

// Intel SDM 3A §§2.5, 5.5.4, 5.6; vol. 4, table 2-2.
#define K5_PAGE 4096ULL
#define K5_PAGES 7ULL
#define K5_USER_CODE 0x400000ULL
#define K5_USER_STACK 0x402000ULL
#define K5_RSP0_PAGE 0x404000ULL
#define K5_TIMER_VECTOR 0x41U
#define K5_PAGE_PRESENT 1ULL
#define K5_PAGE_WRITE 2ULL
#define K5_PAGE_USER 4ULL
#define K5_PAGE_NX (1ULL << 63)
#define K5_GS_BASE 0xc0000101U
#define K5_KERNEL_GS_BASE 0xc0000102U
#define K5_FS_BASE 0xc0000100U
#define K5_EFER 0xc0000080U

extern const unsigned char x86_k5_user[], x86_k5_user_end[];
extern const unsigned char x86_k5_user_first[], x86_k5_user_last[], x86_k5_user_ud[];
extern const unsigned char x86_k5_user_cli[], x86_k5_user_read[];
extern const unsigned char x86_k5_wait_first[], x86_k5_wait_last[];
extern const unsigned char x86_k5_user_irq_first[], x86_k5_user_irq_last[];
extern const unsigned char x86_k5_irq_wait_first[], x86_k5_irq_wait_last[];
void x86_k5_wait(BOOT_U32 swapped);
void x86_k5_enter(BOOT_U64 entry, BOOT_U32 mode, BOOT_U64 supervisor_address);
void x86_k5_nmi(void);
void x86_k5_ud(void);
void x86_k5_gp(void);
void x86_k5_pf(void);
void x86_k5_irq(void);
__declspec(noreturn) void x86_k5_stop(void);
BOOT_U32 x86_k5_frame_valid(BOOT_U64 mode, BOOT_U64 vector, BOOT_U64 error,
    BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);
BOOT_U32 x86_k5_irq_frame_valid(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp,
    BOOT_U64 ss, BOOT_U64 entry, BOOT_U64 first, BOOT_U64 last);

volatile BOOT_U64 k5_owner, k5_phase, k5_first, k5_last;
volatile BOOT_U64 k5_nmi_count, k5_nmi_rip, k5_nmi_gs, k5_cr2, k5_live_root;
volatile BOOT_U64 k5_nmi_stack, k5_fault_stack;
volatile X86_EXCEPTION_FRAME k5_fault;
volatile BOOT_U64 k5_irq_count, k5_irq_goal, k5_irq_stack, k5_irq_frame[5];
static BOOT_U64 k5_irq_last;
#ifndef KERNEL_K5_IRQ_NEGATIVE
#define KERNEL_K5_IRQ_NEGATIVE 0
#endif
volatile BOOT_U64 k5_negative = KERNEL_K5_IRQ_NEGATIVE;
BOOT_U64 k5_stack, k5_root;
_Alignas(16) unsigned char k5_fx_saved[512];
// Intel SDM vol. 2, FXSAVE64 image: FCW 037fh and MXCSR 1f80h, clean x87/SSE data.
_Alignas(16) const unsigned char k5_fx_clean[512] = {
    [0] = 0x7f, [1] = 0x03, [24] = 0x80, [25] = 0x1f
};

static _Alignas(16) BOOT_U64 gdt[7];
static X86_IRQ_IDT idt;
static X86_GDT_POINTER gdt_previous, gdt_probe;
static X86_KERNEL_IDTR idt_previous, idt_probe;
static KERNEL_PHYSICAL_LEASE held;
static X86_LAPIC_TIMER *k5_timer;
static BOOT_U32 admitted;

void x86_k5_load_gdt(const X86_GDT_POINTER *pointer);
int x86_k5_selectors(void);

// ====== descriptor_base ======

static BOOT_U64 descriptor_base(const unsigned char *bytes)
{
    BOOT_U64 base = 0;
    for (BOOT_U32 index = 0; index < 8; ++index) base |= (BOOT_U64)bytes[index + 2] << (8 * index);
    return base;
}

// ====== descriptor_pointer ======

static void descriptor_pointer(unsigned char *bytes, BOOT_U32 limit, BOOT_U64 base)
{
    bytes[0] = (unsigned char)limit;
    bytes[1] = (unsigned char)(limit >> 8);
    for (BOOT_U32 index = 0; index < 8; ++index) bytes[index + 2] = (unsigned char)(base >> (8 * index));
}

// ====== gate_private ======

static void gate_private(BOOT_U32 vector, BOOT_U64 target, unsigned char ist)
{
    // Intel SDM 3A §7.14.1: selector 8, IST in byte 4, present DPL0 interrupt gate 8eh.
    unsigned char *gate = idt.Gates[vector];
    gate[0] = (unsigned char)target;
    gate[1] = (unsigned char)(target >> 8);
    gate[2] = 8;
    gate[4] = ist;
    gate[5] = 0x8e;
    for (BOOT_U32 index = 6; index < 12; ++index)
        gate[index] = (unsigned char)(target >> ((index - 4) * 8));
}

// ====== descriptors_prepare ======

static int descriptors_prepare(void)
{
    x86_kernel_segments_read(&gdt_previous);
    x86_exception_read_idt(&idt_previous);
    BOOT_U64 base = descriptor_base(gdt_previous.Bytes);
    if (gdt_previous.Bytes[0] != 39 || gdt_previous.Bytes[1] ||
        !X86_KERNEL_IMAGE_CONTAINS(base, 40) || (base & 7) ||
        !x86_kernel_segments_retire_ready()) return 0;
    for (BOOT_U32 index = 0; index < 5; ++index) gdt[index] = ((const BOOT_U64 *)base)[index];
    // Intel SDM 3A §3.4.5: P=1, DPL=3, S=1, accessed flat RW data / 64-bit RX code.
    gdt[5] = 0x00cff3000000ffffULL;
    gdt[6] = 0x00affb000000ffffULL;
    descriptor_pointer(gdt_probe.Bytes, sizeof(gdt) - 1, (BOOT_U64)gdt);
    descriptor_pointer(idt_probe.Bytes, sizeof(idt) - 1, (BOOT_U64)&idt);
    for (BOOT_U32 vector = 0; vector < 32; ++vector) gate_private(vector, (BOOT_U64)x86_k5_stop, 3);
    gate_private(2, (BOOT_U64)x86_k5_nmi, 2);
    gate_private(6, (BOOT_U64)x86_k5_ud, 1);
    gate_private(13, (BOOT_U64)x86_k5_gp, 1);
    gate_private(14, (BOOT_U64)x86_k5_pf, 1);
    gate_private(K5_TIMER_VECTOR, (BOOT_U64)x86_k5_irq, 0);
    return 1;
}

// ====== address_space_prepare ======

static int address_space_prepare(X86_PAGE_WINDOW *window)
{
    BOOT_U64 bytes = (BOOT_U64)x86_k5_user_end - (BOOT_U64)x86_k5_user;
    const volatile BOOT_U64 *current = x86_window_entries(0);
    if (!bytes || bytes > K5_PAGE || !x86_page_window_idle_valid(window)) return 0;
    for (BOOT_U32 slot = 0; slot < 512; ++slot) {
        if (slot < 256 ? current[slot] != 0 : (current[slot] & K5_PAGE_USER) != 0) return 0;
    }
    if (!x86_page_window_acquire(window, K5_PAGES)) return 0;
    volatile unsigned char *alias = x86_window_bytes();
    volatile BOOT_U64 *tables = (volatile BOOT_U64 *)alias;
    BOOT_U64 base = window->Lease.Base;
    BOOT_U64 branch = K5_PAGE_PRESENT | K5_PAGE_WRITE | K5_PAGE_USER;
    // 0x400000 indexes PML4[0], PDPT[0], PD[2], PT[0]. PT[1] is the stack guard.
    for (BOOT_U32 slot = 256; slot < 512; ++slot) tables[slot] = current[slot];
    tables[X86_LAYOUT_RECURSIVE_SLOT] = 0;
    tables[0] = (base + K5_PAGE) | branch;
    tables[512] = (base + 2 * K5_PAGE) | branch;
    tables[1024 + 2] = (base + 3 * K5_PAGE) | branch;
    tables[1536] = (base + 4 * K5_PAGE) | K5_PAGE_PRESENT | K5_PAGE_USER;
    tables[1536 + 2] = (base + 5 * K5_PAGE) | branch | K5_PAGE_NX;
    // PT[3] is the RSP0 guard; PT[4] is the supervisor RSP0 stack, never user-accessible.
    tables[1536 + 4] = (base + 6 * K5_PAGE) | K5_PAGE_PRESENT | K5_PAGE_WRITE | K5_PAGE_NX;
    for (BOOT_U64 index = 0; index < bytes; ++index) alias[4 * K5_PAGE + index] = x86_k5_user[index];
    for (BOOT_U64 index = 0; index < bytes; ++index) {
        if (alias[4 * K5_PAGE + index] != x86_k5_user[index]) {
            if (!x86_page_window_release(window)) x86_k5_stop();
            return 0;
        }
    }
    if (!x86_page_window_park(window, &held)) x86_k5_stop();
    k5_root = base;
    k5_stack = K5_USER_STACK + K5_PAGE;
    return 1;
}

// ====== trace_field ======

static int trace_field(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== nmi_arm ======

static int nmi_arm(BOOT_U64 phase, BOOT_U64 first, BOOT_U64 last)
{
    if (last <= first) return 0;
    k5_nmi_count = 0;
    k5_nmi_gs = ~0ULL;
    k5_nmi_rip = 0;
    k5_phase = phase;
    k5_first = first;
    k5_last = last;
    return trace_field("K5_NMI_FIRST: ", first) && trace_field("K5_NMI_LAST: ", last) &&
        kernel_log(0, "KERNEL_K5_NMI_ARMED");
}

// ====== nmi_report ======

static int nmi_report(BOOT_U64 active)
{
    return k5_nmi_count == 1 && k5_nmi_rip >= k5_first && k5_nmi_rip < k5_last &&
        k5_nmi_stack == x86_ist_top(2) - 5 * sizeof(BOOT_U64) &&
        k5_nmi_gs == active && trace_field("K5_NMI_RIP: ", k5_nmi_rip) &&
        trace_field("K5_NMI_GS: ", k5_nmi_gs) && kernel_log(0, "KERNEL_K5_NMI_RESUMED");
}

// ====== user_address ======

static BOOT_U64 user_address(const unsigned char *label)
{
    return K5_USER_CODE + ((BOOT_U64)label - (BOOT_U64)x86_k5_user);
}

// ====== user_case ======

static int user_case(BOOT_U32 mode, BOOT_U64 root)
{
    const BOOT_U64 supervisor_code = (BOOT_U64)x86_k5_enter;
    BOOT_U64 expected_rip, expected_address;
    if (mode == 1) expected_rip = user_address(x86_k5_user_cli);
    else if (mode == 2) expected_rip = user_address(x86_k5_user_read);
    else if (mode == 3) expected_rip = k5_stack - 8;
    else expected_rip = user_address(x86_k5_user_ud);
    expected_address = mode == 2 ? supervisor_code : expected_rip;
    x86_k5_enter(K5_USER_CODE, mode, supervisor_code);
    return x86_kernel_root() == root && __readmsr(K5_GS_BASE) == k5_owner &&
        __readmsr(K5_KERNEL_GS_BASE) == 0 && k5_live_root == k5_root &&
        k5_fault_stack == x86_ist_top(1) - sizeof(X86_EXCEPTION_FRAME) &&
        x86_k5_frame_valid(mode, k5_fault.Vector, k5_fault.Error, k5_fault.Cs,
            k5_fault.Flags, k5_fault.Rsp, k5_fault.Ss) && k5_fault.Rip == expected_rip &&
        (k5_fault.Vector != 14 || k5_cr2 == expected_address) &&
        trace_field("K5_FAULT_VECTOR: ", k5_fault.Vector) &&
        trace_field("K5_FAULT_ERROR: ", k5_fault.Error) && trace_field("K5_USER_CS: ", k5_fault.Cs);
}

// ====== verify_and_release ======

static int verify_and_release(X86_PAGE_WINDOW *window, BOOT_U64 root, BOOT_U64 budget, BOOT_U64 alternate)
{
    X86_GDT_POINTER current_gdt;
    X86_KERNEL_IDTR current_idt;
    if (x86_kernel_root() != root || !x86_exception_environment() || !x86_k5_selectors() ||
        __readmsr(K5_GS_BASE) != k5_owner ||
        __readmsr(K5_KERNEL_GS_BASE) != 0) x86_k5_stop();
    x86_k5_load_gdt(&gdt_previous);
    x86_exception_load_idt(&idt_previous);
    __writemsr(K5_KERNEL_GS_BASE, alternate);
    x86_kernel_segments_read(&current_gdt);
    x86_exception_read_idt(&current_idt);
    for (BOOT_U32 index = 0; index < 10; ++index) {
        if (current_gdt.Bytes[index] != gdt_previous.Bytes[index] ||
            current_idt.Bytes[index] != idt_previous.Bytes[index]) x86_k5_stop();
    }
    if (__readmsr(K5_KERNEL_GS_BASE) != alternate || !x86_page_window_resume(window, &held)) x86_k5_stop();
    volatile unsigned char *alias = x86_window_bytes();
    for (BOOT_U64 index = 0; index < K5_PAGES * K5_PAGE; ++index) alias[index] = 0;
    if (!x86_page_window_release(window)) x86_k5_stop();
    k5_owner = 0;
    return x86_page_window_idle_valid(window) && kernel_physical_budget_matches(window->Pool, budget);
}

// ====== session_open ======

static int session_open(X86_PAGE_WINDOW *window, BOOT_U64 *root, BOOT_U64 *budget, BOOT_U64 *alternate)
{
    // Intel SDM 3A §2.5: forbid PGE, LA57, FSGSBASE, PCIDE and CET.
    const BOOT_U64 incompatible = (1ULL << 7) | (1ULL << 12) | (1ULL << 16) |
        (1ULL << 17) | (1ULL << 23);
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    // Intel SDM 3A §2.2: require NXE (bit 11) and no enabled SYSCALL (SCE, bit 0).
    BOOT_U64 efer = __readmsr(K5_EFER);
    *root = x86_kernel_root();
    if (k5_owner || !owner || !window || window->Root != *root || (*root & 4095) ||
        !x86_exception_environment() || !x86_k5_selectors() || !x86_irq_cpu_state() ||
        (__readcr4() & incompatible) || !(__readcr0() & (1ULL << 16)) ||
        (efer & ((1ULL << 11) | 1ULL)) != (1ULL << 11) || __readmsr(K5_FS_BASE) ||
        __readmsr(K5_GS_BASE) != (BOOT_U64)owner || x86_percpu_current_local() != owner ||
        !kernel_physical_budget_read(window->Pool, budget)) return 0;
    if (!kernel_log(0, "KERNEL_K5_CPU_OK") || !descriptors_prepare() ||
        !kernel_log(0, "KERNEL_K5_DESCRIPTORS_OK") || !address_space_prepare(window)) return 0;
    k5_owner = (BOOT_U64)owner;
    *alternate = __readmsr(K5_KERNEL_GS_BASE);
    __writemsr(K5_KERNEL_GS_BASE, 0);
    x86_k5_load_gdt(&gdt_probe);
    x86_exception_load_idt(&idt_probe);
    return 1;
}

// ====== x86_k5_privilege_probe ======

int x86_k5_privilege_probe(X86_PAGE_WINDOW *window)
{
    BOOT_U64 root, budget, alternate;
    if (admitted & 1) return 0;
    admitted |= 1;
    if (!session_open(window, &root, &budget, &alternate)) return 0;
    int passed = 1;
    for (BOOT_U32 mode = 1; passed && mode <= 3; ++mode) passed = user_case(mode, root);
    for (BOOT_U32 phase = 1; passed && phase <= 2; ++phase) {
        if (!nmi_arm(phase, (BOOT_U64)x86_k5_wait_first, (BOOT_U64)x86_k5_wait_last)) { passed = 0; break; }
        x86_k5_wait(phase - 1);
        passed = nmi_report(phase == 1 ? k5_owner : 0);
    }
    if (passed) {
        passed = nmi_arm(3, user_address(x86_k5_user_first), user_address(x86_k5_user_last)) &&
            user_case(4, root) && nmi_report(0);
    }
    if (!verify_and_release(window, root, budget, alternate)) return 0;
    return passed && kernel_log(0, "KERNEL_K5_RING3_OK");
}

// ====== x86_k5_irq_service ======

int x86_k5_irq_service(void)
{
    BOOT_U32 value, index;
    // Intel SDM 3A §13.5.4: the one-shot current count (390h) reads zero after expiry.
    if (!x86_lapic_native_read(0x390, &value) || value) return 0;
    // Intel SDM 3A §13.8.4: ISR 100h-170h; vector 41h is bank 2 (120h), bit 1.
    for (index = 0; index < 8; ++index) {
        if (!x86_lapic_native_read(0x100 + index * 16, &value) || value != (index == 2 ? 2U : 0U)) return 0;
    }
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_k5_irq_frame_valid(k5_irq_frame[0], k5_irq_frame[1], k5_irq_frame[2], k5_irq_frame[3],
        k5_irq_frame[4], k5_irq_stack, K5_USER_CODE, k5_irq_last)) return 0;
    if (!x86_lapic_native_eoi()) return 0;
    for (index = 0; index < 8; ++index) {
        if (!x86_lapic_native_read(0x100 + index * 16, &value) || value) return 0;
    }
    // Intel SDM 3A §13.5.4: writing the initial count (380h) restarts the one-shot timer.
    if (k5_irq_count + 1 < k5_irq_goal) *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + 0x380) = k5_timer->Reload;
    return 1;
}

// ====== tss_rsp0 ======

static volatile unsigned char *tss_rsp0(void)
{
    // Intel SDM 3A §10.2.3: 64-bit busy TSS descriptor (type 1011b, P=1), base 15:0, 23:16, 31:24, 63:32.
    // Intel SDM 3A §10.7, figure 10-11: RSP0 at TSS offset 4.
    BOOT_U64 low = gdt[3], high = gdt[4];
    BOOT_U64 base = ((low >> 16) & 0xffffffULL) | (((low >> 56) & 0xffULL) << 24) |
        ((high & 0xffffffffULL) << 32);
    if (((low >> 40) & 0x9f) != 0x8b || (base & 15) || base < 0xffff800000000000ULL) return 0;
    return (volatile unsigned char *)(base + 4);
}

// ====== rsp0_swap ======

static BOOT_U64 rsp0_swap(volatile unsigned char *field, BOOT_U64 value)
{
    BOOT_U64 previous = 0;
    for (BOOT_U32 index = 0; index < 8; ++index) previous |= (BOOT_U64)field[index] << (8 * index);
    for (BOOT_U32 index = 0; index < 8; ++index) field[index] = (unsigned char)(value >> (8 * index));
    return previous;
}

// ====== timer_arm ======

static int timer_arm(void)
{
    const X86_LAPIC_MAPPING *mapping = k5_timer->Mapping;
    // Intel SDM 3A §13.5.4: divide 3E0h = 3 (by 16), LVT 320h one-shot unmasked, initial count 380h.
    return x86_timer_confirm_write(mapping, 0x3e0, 3) &&
        x86_timer_confirm_write(mapping, 0x320, K5_TIMER_VECTOR) &&
        x86_timer_write(mapping, 0x380, k5_timer->Reload);
}

// ====== irq_case ======

static int irq_case(BOOT_U64 phase, BOOT_U64 root)
{
    BOOT_U64 first = user_address(x86_k5_user_irq_first), last = user_address(x86_k5_user_irq_last);
    int armed = phase == 4 ?
        nmi_arm(4, (BOOT_U64)x86_k5_irq_wait_first, (BOOT_U64)x86_k5_irq_wait_last) :
        nmi_arm(5, first, last);
    if (!armed) return 0;
    k5_irq_count = 0;
    k5_irq_goal = phase == 4 ? 3 : 1;
    k5_irq_last = last;
    k5_irq_stack = 0;
    if (k5_negative != 4 && !timer_arm()) return 0;
    if (k5_negative == 5) {
        _enable();
        for (BOOT_U32 spin = 0; spin < 0x4000000U; ++spin) _mm_pause();
        x86_k5_stop();
    }
    x86_k5_enter(K5_USER_CODE, 5, phase == 5);
    int stopped = x86_timer_stop(k5_timer->Mapping);
    return stopped && k5_irq_count == k5_irq_goal && x86_kernel_root() == root &&
        __readmsr(K5_GS_BASE) == k5_owner && __readmsr(K5_KERNEL_GS_BASE) == 0 &&
        k5_live_root == k5_root &&
        x86_k5_irq_frame_valid(k5_irq_frame[0], k5_irq_frame[1], k5_irq_frame[2], k5_irq_frame[3],
            k5_irq_frame[4], k5_irq_stack, K5_USER_CODE, last) &&
        x86_k5_frame_valid(5, k5_fault.Vector, k5_fault.Error, k5_fault.Cs,
            k5_fault.Flags, k5_fault.Rsp, k5_fault.Ss) &&
        k5_fault.Rip == user_address(x86_k5_user_ud) &&
        nmi_report(phase == 4 ? k5_owner : 0) &&
        trace_field("K5_IRQ_RIP: ", k5_irq_frame[0]) && trace_field("K5_IRQ_CS: ", k5_irq_frame[1]) &&
        trace_field("K5_IRQ_STACK: ", k5_irq_stack) && kernel_log(0, "KERNEL_K5_IRQ_RESUMED");
}

// ====== x86_k5_irq_probe ======

int x86_k5_irq_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    BOOT_U64 root, budget, alternate, previous;
    BOOT_U32 lvt, divide, count;
    if ((admitted & 2) || !timer || !timer->Mapping || !timer->Reload || timer->Active ||
        !x86_timer_read(timer->Mapping, 0x320, &lvt) || !(lvt & 0x10000) ||
        !x86_timer_read(timer->Mapping, 0x3e0, &divide) ||
        !x86_timer_read(timer->Mapping, 0x380, &count) || count ||
        !x86_lapic_idle(timer->Mapping)) return 0;
    admitted |= 2;
    k5_timer = timer;
    if (!session_open(window, &root, &budget, &alternate)) return 0;
    volatile unsigned char *field = tss_rsp0();
    if (!field) x86_k5_stop();
    const BOOT_U64 top = K5_RSP0_PAGE + K5_PAGE - (k5_negative == 3 ? 16 : 0);
    previous = rsp0_swap(field, top);
    int passed = previous && irq_case(4, root) && irq_case(5, root);
    int stopped = x86_timer_stop(timer->Mapping);
    if (rsp0_swap(field, previous) != top) x86_k5_stop();
    if (!stopped || !x86_timer_confirm_write(timer->Mapping, 0x3e0, divide) ||
        !x86_timer_confirm_write(timer->Mapping, 0x320, lvt) || !x86_lapic_idle(timer->Mapping)) x86_k5_stop();
    if (!verify_and_release(window, root, budget, alternate)) return 0;
    return passed && kernel_log(0, "KERNEL_K5_IRQ_OK");
}
