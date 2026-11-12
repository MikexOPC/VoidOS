#include "process.h"
#include "arch/x86/kernel/cpu/percpu/clock/clock.h"
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
#define TEXT_BLOCK 128U
// Intel SDM 325462-089, vol. 3A, Fig. 12-8: LVT Timer +320h, vector bits 7:0.
// Reserve 44h for the process timer so the PS/2 keyboard can use 41h.
#define TIMER_VECTOR 0x44U
#define STRAY_TEST_VECTOR 0x50U
#define SPURIOUS_VECTOR 0xffU
// Intel SDM 3A §13.8.4: IRR 200h-270h, one 32-bit bank per 16-byte step, indexed by vector.
#define IRR_TIMER_BANK (0x200U + (TIMER_VECTOR / 32U) * 16U)
#define IRR_TIMER_BIT (1U << (TIMER_VECTOR % 32U))
#define DRAIN_ATTEMPTS 64U
#define VECTOR_STRAY 0xffffULL
#define STACK_BYTES 16384U
#define PROCESS_BUDGET_MS 2000U
#define STACK_CANARY 0x4b36435441434bULL
#define RESULT_RANGE (~0ULL - 1)
#define RESULT_LENGTH (~0ULL - 2)
#define RESULT_QUOTA (~0ULL - 3)
#define RESULT_UNKNOWN (~0ULL)
#define SYS_EXIT 0ULL
#define SYS_WRITE 1ULL
#define SYS_YIELD 2ULL
#define SYS_SEND 3ULL
#define SYS_RECV 4ULL
#define SYS_WAIT_READABLE 5ULL
#define IPC_WOULD_BLOCK (~5ULL)
#define IPC_MESSAGE_BYTES 128U
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
#define GPR_RDX 11U
#define GPR_RAX 14U
#define USER_FRAME_WORDS (FX_WORDS + 15U + 5U)

#ifndef KERNEL_K6_NEGATIVE
#define KERNEL_K6_NEGATIVE 0
#endif

#ifdef KERNEL_TEST_K6_PROCESS
#define K6_NEGATIVE(number) (k6_negative == (number))
#else
#define K6_NEGATIVE(number) 0
#endif

typedef struct { BOOT_U64 Entry, Count; struct { BOOT_U64 Virt, FileOffset, FileBytes, Flags; } Pages[MAX_IMAGE_PAGES]; } PROCESS_PLAN;

extern const BOOT_U64 x86_process_fault_targets[32];
extern const unsigned char x86_process_copy_first[], x86_process_copy_last[], x86_process_copy_fixup[];
#ifdef KERNEL_TEST_K6_PROCESS
// Keyboard slot in the ordinary IRQ registry.
#define DEVICE_PROBE_VECTOR 0x41U
#define IRR_DEVICE_BIT (1U << (DEVICE_PROBE_VECTOR % 32U))
_Static_assert(DEVICE_PROBE_VECTOR != TIMER_VECTOR, "K6 timer and input IRQ vectors must differ");
_Static_assert(DEVICE_PROBE_VECTOR / 32U == TIMER_VECTOR / 32U, "K6 probe shares the timer IRR bank");
typedef struct { BOOT_U64 Storage[142]; } IPC_CHANNEL;
BOOT_U64 x86_ipc_open(IPC_CHANNEL *channel);
void x86_ipc_close(IPC_CHANNEL *channel);
void x86_ipc_finish(IPC_CHANNEL *channel);
BOOT_U64 x86_ipc_send_check(const IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle, BOOT_U64 len);
BOOT_U64 x86_ipc_send(IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle, const unsigned char *bytes, BOOT_U64 len);
BOOT_U64 x86_ipc_peek(const IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle, unsigned char *bytes, BOOT_U64 capacity);
BOOT_U64 x86_ipc_consume(IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle);
BOOT_U64 x86_ipc_receive_ready(const IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle);
void x86_process_resume(const BOOT_U64 *frame);
void x86_process_device_irq(void);
#define WINDOW_LABELS(number) \
    extern const unsigned char x86_process_window_##number##_first[], x86_process_window_##number##_last[]
WINDOW_LABELS(1); WINDOW_LABELS(2); WINDOW_LABELS(3); WINDOW_LABELS(4); WINDOW_LABELS(5); WINDOW_LABELS(6);
void x86_process_probe_read(BOOT_U64 address);
BOOT_U32 x86_process_nmi_valid(BOOT_U64 window, BOOT_U32 user_root, BOOT_U32 kernel_gs, BOOT_U32 ac, BOOT_U64 cs);
#endif
void x86_process_syscall(void);
void x86_process_nmi(void);
void x86_process_irq(void);
void x86_process_stray(void);
void x86_process_spurious(void);
__declspec(noreturn) void x86_process_stop(void);
void x86_process_enter(BOOT_U64 entry, BOOT_U64 stack, BOOT_U64 argument);
BOOT_U64 x86_process_copy(void *target, const void *source, BOOT_U64 bytes);
BOOT_U32 x86_process_plan(const unsigned char *bytes, BOOT_U64 length, PROCESS_PLAN *plan);
BOOT_U32 x86_process_copy_range(BOOT_U64 pointer, BOOT_U64 length);
BOOT_U32 x86_process_return_valid(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);
BOOT_U32 x86_process_irq_frame_valid(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);
BOOT_U32 x86_process_fault_recoverable(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 error, BOOT_U64 cr2,
    BOOT_U64 first, BOOT_U64 last, BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write);
void x86_process_console_filter(unsigned char *bytes, BOOT_U64 length);
BOOT_U32 x86_process_state_next(BOOT_U32 state, BOOT_U32 next);
BOOT_U64 x86_process_budget_initial(void);
BOOT_U64 x86_process_budget_next(BOOT_U64 remaining);
BOOT_U32 x86_process_write_allowed(BOOT_U64 total, BOOT_U64 length);

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

static _Alignas(4096) BOOT_U64 kernel_stack[STACK_BYTES / 8];
static X86_IRQ_IDT idt;
static X86_KERNEL_IDTR idt_previous, idt_probe;
static X86_PAGE_WINDOW *owner_window;
static X86_LAPIC_TIMER *owner_timer;
static volatile unsigned char *tss_field;
static BOOT_U64 rsp0_previous, msr_saved[5], alternate_saved;
static BOOT_U64 expected_image, expected_windows;
static KERNEL_PHYSICAL_LEASE held;
static BOOT_U32 state, opened, timer_lvt_saved, timer_divide_saved;
static BOOT_U64 budget_left, drained, written_bytes;
static const KERNEL_CLOCK *budget_clock;
static KERNEL_DEADLINE budget_deadline;
#ifdef KERNEL_TEST_K6_PROCESS
static BOOT_U32 delay_next_syscall;
static BOOT_U64 *yield_frame;
static BOOT_U32 yielded;
static BOOT_U32 waiting;
static IPC_CHANNEL ipc_channel;
static BOOT_U64 ipc_endpoint;
static X86_IRQ_TOKEN device_probe_token;
static BOOT_U32 device_probe_armed, device_probe_delivered;
static int (*device_probe_inject)(void);
#endif
static X86_PROCESS_RESULT *current;
static volatile BOOT_U64 window_pointer, window_length, window_write;
static _Alignas(16) unsigned char buffer[MAX_COPY + 1];

// ====== die ======

static __declspec(noreturn) void die(const char *reason)
{
    x86_exception_load_idt(&idt_previous);
    (void)kernel_log(3, reason);
    kernel_panic(reason);
}

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
    idt.Gates[SPURIOUS_VECTOR][5] = 0xee;
#endif
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
    const KERNEL_LOCK_WAIT *wait = x86_percpu_wait_current();
    if (!wait) return 0;
    const volatile BOOT_U64 *root = x86_window_entries(0);
    for (BOOT_U32 slot = 0; slot < 256; ++slot) if (root[slot]) return 0;
    tss_field = tss_rsp0();
    if (!tss_field || !x86_timer_read(timer->Mapping, 0x320, &timer_lvt_saved) ||
        !x86_timer_read(timer->Mapping, 0x3e0, &timer_divide_saved)) return 0;
    owner_window = window;
    owner_timer = timer;
    budget_clock = &wait->Clock;
    k6_owner = (BOOT_U64)owner;
    k6_kernel_root = __readcr3();
    k6_stack_top = (BOOT_U64)kernel_stack + STACK_BYTES;
    kernel_stack[0] = STACK_CANARY;
    expected_image = root[SLOT_IMAGE];
    expected_windows = root[X86_LAYOUT_WINDOWS_SLOT];
    if (K6_NEGATIVE(33)) expected_windows ^= 0x1000;
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
    if (!x86_timer_stop(owner_timer->Mapping) ||
        !x86_timer_confirm_write(owner_timer->Mapping, 0x3e0, timer_divide_saved) ||
        !x86_timer_confirm_write(owner_timer->Mapping, 0x320, timer_lvt_saved)) die("PROCESS_TIMER_RETAINED");
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

static int map_image(const X86_PROCESS_IMAGE *image, const PROCESS_PLAN *plan, KERNEL_PHYSICAL_LEASE *lease_out)
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
    if (!x86_page_window_park(owner_window, lease_out)) x86_process_stop();
    return 1;
}

// ====== reap ======

static int reap(KERNEL_PHYSICAL_LEASE *lease)
{
    if (__readcr3() != k6_kernel_root || !roots_stable() || kernel_stack[0] != STACK_CANARY) return 0;
    BOOT_U64 pages = lease->Pages;
    if (!x86_page_window_resume(owner_window, lease)) x86_process_stop();
    volatile unsigned char *alias = x86_window_bytes();
    for (BOOT_U64 index = 0; index < pages * PAGE_BYTES; ++index) alias[index] = 0;
    if (!x86_page_window_release(owner_window)) x86_process_stop();
    k6_user_root = 0;
    return x86_page_window_idle_valid(owner_window);
}

// ====== timer_arm ======

static void timer_arm(void)
{
    const X86_LAPIC_MAPPING *mapping = owner_timer->Mapping;
    BOOT_U32 vector = K6_NEGATIVE(36) ? STRAY_TEST_VECTOR : TIMER_VECTOR;
    // Intel SDM 3A §13.5.4: divide 3E0h = 3 (by 16), LVT 320h one-shot unmasked, initial count 380h.
    if (!x86_timer_confirm_write(mapping, 0x3e0, 3) || !x86_timer_confirm_write(mapping, 0x320, vector) ||
        !x86_timer_write(mapping, 0x380, owner_timer->Reload)) die("PROCESS_TIMER_FAILED");
}

// ====== timer_start ======

static void timer_start(void)
{
    budget_left = x86_process_budget_initial();
    BOOT_U32 duration = PROCESS_BUDGET_MS;
#ifdef KERNEL_TEST_K6_PROCESS
    if (delay_next_syscall) duration = 100;
#endif
    if (!kernel_deadline_start(budget_clock, kernel_clock_ms(budget_clock, duration), &budget_deadline))
        die("PROCESS_CLOCK_FAILED");
    timer_arm();
}

// ====== timer_quiesce ======

static void timer_quiesce(void)
{
    BOOT_U32 pending = 0;
    BOOT_U32 mask = IRR_TIMER_BIT;
#ifdef KERNEL_TEST_K6_PROCESS
    if (device_probe_armed) mask |= IRR_DEVICE_BIT;
#endif
    if (!x86_timer_stop(owner_timer->Mapping)) die("PROCESS_TIMER_RETAINED");
    for (BOOT_U32 attempt = 0; attempt < DRAIN_ATTEMPTS; ++attempt) {
        if (!x86_lapic_native_read(IRR_TIMER_BANK, &pending)) die("PROCESS_TIMER_RETAINED");
        if (!(pending & mask)) break;
        k6_drain = 1;
        _enable();
        __nop();
        __nop();
        _disable();
        k6_drain = 0;
    }
    if (!x86_lapic_idle(owner_timer->Mapping)) die("PROCESS_TIMER_RETAINED");
}

// ====== turn_finish ======

static void turn_finish(X86_PROCESS_RESULT *result)
{
    timer_quiesce();
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
}

#ifdef KERNEL_TEST_K6_PROCESS
// ====== device_probe_send ======

static void device_probe_send(void)
{
    BOOT_U32 value;
    if (!device_probe_inject || !device_probe_inject())
        die("PROCESS_DEVICE_IRQ_ARM_FAILED");
    // Intel SDM §12.8.4: IRR bank +200h + 16*(vector/32), bit vector%32.
    for (BOOT_U32 attempt = 0; attempt < DRAIN_ATTEMPTS; ++attempt) {
        if (!x86_lapic_native_read(IRR_TIMER_BANK, &value)) break;
        if (value & IRR_DEVICE_BIT) return;
        _mm_pause();
    }
    die("PROCESS_DEVICE_IRQ_ARM_FAILED");
}
#endif

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
    if (!map_image(image, &plan, &held)) return 0;
    k6_user_root = held.Base;
    advance(2);
    current = result;
    written_bytes = 0;
    k6_exit_kind = 0;
    advance(3);
    timer_start();
#ifdef KERNEL_TEST_K6_PROCESS
    if (device_probe_armed) device_probe_send();
#endif
    x86_process_enter(plan.Entry, STACK_TOP, 0);
    advance(4);
    turn_finish(result);
    if (!reap(&held) || !kernel_physical_budget_matches(owner_window->Pool, budget)) {
        die(roots_stable() ? "PROCESS_REAP_FAILED" : "PROCESS_ROOT_CHANGED");
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
    if (!x86_process_write_allowed(written_bytes, length)) return RESULT_QUOTA;
    if (copy_checked(buffer, (const void *)pointer, pointer, length, 0)) {
        for (BOOT_U32 index = 0; index <= MAX_COPY; ++index) ((volatile unsigned char *)buffer)[index] = 0;
        return RESULT_RANGE;
    }
    x86_process_console_filter(buffer, length);
    if (!kernel_text("USER: ")) x86_process_stop();
    for (BOOT_U64 offset = 0; offset < length; offset += TEXT_BLOCK) {
        BOOT_U64 end = offset + TEXT_BLOCK < length ? offset + TEXT_BLOCK : length;
        unsigned char saved = buffer[end];
        buffer[end] = 0;
        if (!kernel_text((const char *)buffer + offset)) x86_process_stop();
        buffer[end] = saved;
    }
    if (buffer[length - 1] != '\n') {
        if (!kernel_text("\n")) x86_process_stop();
    }
    written_bytes += length;
    ++current->Writes;
    return length;
}

#ifdef KERNEL_TEST_K6_PROCESS
// ====== sys_send ======

static BOOT_U64 sys_send(BOOT_U64 handle, BOOT_U64 pointer, BOOT_U64 len)
{
    BOOT_U64 status = x86_ipc_send_check(&ipc_channel, ipc_endpoint, handle, len);
    if (status) return status;
    unsigned char data[IPC_MESSAGE_BYTES] = {0};
    if (x86_process_copy_range(pointer, len) != 1 ||
        copy_checked(data, (const void *)pointer, pointer, len, 0)) return RESULT_RANGE;
    return x86_ipc_send(&ipc_channel, ipc_endpoint, handle, data, len);
}

// ====== sys_recv ======

static BOOT_U64 sys_recv(BOOT_U64 handle, BOOT_U64 pointer, BOOT_U64 capacity)
{
    unsigned char data[IPC_MESSAGE_BYTES] = {0};
    BOOT_U64 len = x86_ipc_peek(&ipc_channel, ipc_endpoint, handle, data, capacity);
    if (len > sizeof(data)) return len;
    if (!len || x86_process_copy_range(pointer, len) != 1 ||
        copy_checked((void *)pointer, data, pointer, len, 1)) return RESULT_RANGE;
    if (x86_ipc_consume(&ipc_channel, ipc_endpoint, handle)) die("PROCESS_IPC_STATE");
    return len;
}
#endif

// ====== budget_expired ======

static int budget_expired(BOOT_U64 rip)
{
    if (budget_left && !kernel_deadline_expired(budget_clock, &budget_deadline)) return 0;
    current->Kind = X86_PROCESS_BUDGET;
    current->Rip = rip;
    return 1;
}

// ====== x86_process_syscall_dispatch ======

int x86_process_syscall_dispatch(BOOT_U64 *frame)
{
#ifdef KERNEL_TEST_K6_PROCESS
    if (k6_negative == 31) (void)*(const volatile unsigned char *)0x10ULL;
    if (k6_negative == 32) x86_process_probe_read(X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_HEAP_SLOT));
#endif
    if (!x86_process_return_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        current->Kind = X86_PROCESS_BAD_FRAME;
        current->Rip = IRET(frame, 0);
        return 2;
    }
    if (budget_expired(IRET(frame, 0))) return 2;
    BOOT_U64 number = GPR(frame, GPR_RAX);
    if (number == SYS_EXIT) {
        current->Kind = X86_PROCESS_EXIT;
        current->Code = GPR(frame, GPR_RDI);
        return 2;
    }
#ifdef KERNEL_TEST_K6_PROCESS
    if (yield_frame && (number == SYS_YIELD || (ipc_endpoint && number == SYS_WAIT_READABLE))) {
        BOOT_U64 result = number == SYS_YIELD ? 0 :
            x86_ipc_receive_ready(&ipc_channel, ipc_endpoint, GPR(frame, GPR_RDI));
        GPR(frame, GPR_RAX) = result;
        if (number == SYS_WAIT_READABLE && result != IPC_WOULD_BLOCK)
            return budget_expired(IRET(frame, 0)) ? 2 : 1;
        waiting = number == SYS_WAIT_READABLE;
        for (BOOT_U32 index = 0; index < USER_FRAME_WORDS; ++index) yield_frame[index] = frame[index];
        yielded = 1;
        return 2;
    }
    if (ipc_endpoint && (number == SYS_SEND || number == SYS_RECV)) {
        BOOT_U64 handle = GPR(frame, GPR_RDI), pointer = GPR(frame, GPR_RSI), length = GPR(frame, GPR_RDX);
        GPR(frame, GPR_RAX) = number == SYS_SEND ? sys_send(handle, pointer, length) : sys_recv(handle, pointer, length);
        return budget_expired(IRET(frame, 0)) ? 2 : 1;
    }
#endif
    GPR(frame, GPR_RAX) = number == SYS_WRITE ? sys_write(GPR(frame, GPR_RDI), GPR(frame, GPR_RSI)) : RESULT_UNKNOWN;
#ifdef KERNEL_TEST_K6_PROCESS
    if (delay_next_syscall) {
        delay_next_syscall = 0;
        BOOT_U32 attempt = 0;
        while (!kernel_deadline_expired(budget_clock, &budget_deadline)) {
            if (++attempt == 0x1000000U) die("PROCESS_CLOCK_FAILED");
            _mm_pause();
        }
        if (!kernel_log(0, "KERNEL_K6_SYSCALL_DEADLINE_OK")) x86_process_stop();
    }
#endif
    return budget_expired(IRET(frame, 0)) ? 2 : 1;
}

// ====== x86_process_irq_service ======

int x86_process_irq_service(BOOT_U64 *frame)
{
    BOOT_U32 value, index;
    if ((BOOT_U64)&IRET(frame, 0) != k6_stack_top - 40) die("PROCESS_IRQ_INVALID");
    // Intel SDM 3A §13.8.4: ISR 100h-170h, one 32-bit bank per 16-byte step, indexed by vector.
    for (index = 0; index < 8; ++index) {
        if (!x86_lapic_native_read(0x100 + index * 16, &value) ||
            value != (index == TIMER_VECTOR / 32 ? 1U << (TIMER_VECTOR % 32) : 0U)) die("PROCESS_IRQ_INVALID");
    }
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_lapic_native_eoi()) die("PROCESS_IRQ_INVALID");
    ++current->Irqs;
    current->Rip = IRET(frame, 0);
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        current->Kind = X86_PROCESS_BAD_FRAME;
        return 2;
    }
    budget_left = x86_process_budget_next(budget_left);
    if (budget_expired(IRET(frame, 0))) return 2;
    if (!x86_timer_write(owner_timer->Mapping, 0x380, owner_timer->Reload)) die("PROCESS_TIMER_FAILED");
    return 1;
}

#ifdef KERNEL_TEST_K6_PROCESS
// ====== device_probe_quiet ======

static int device_probe_quiet(void)
{
    return !device_probe_armed && x86_lapic_idle(owner_timer->Mapping);
}

// ====== x86_process_device_irq_service ======

int x86_process_device_irq_service(BOOT_U64 *frame)
{
    if (!current || !device_probe_armed || device_probe_delivered ||
        (BOOT_U64)&IRET(frame, 0) != k6_stack_top - 40)
        die("PROCESS_DEVICE_IRQ_INVALID");
    if (!x86_irq_registry_deliver(x86_irq_registry(), DEVICE_PROBE_VECTOR))
        die("PROCESS_DEVICE_IRQ_INVALID");
    device_probe_delivered = 1;
    if (!x86_process_irq_frame_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        current->Kind = X86_PROCESS_BAD_FRAME;
        return 2;
    }
    return budget_expired(IRET(frame, 0)) ? 2 : 1;
}

// ====== x86_process_device_irq_kernel ======

void x86_process_device_irq_kernel(void)
{
    if (!k6_drain || !device_probe_armed || current || device_probe_delivered != 1 ||
        !x86_irq_registry_deliver(x86_irq_registry(), DEVICE_PROBE_VECTOR))
        die("PROCESS_DEVICE_IRQ_INVALID");
    device_probe_delivered = 2;
}

// ====== x86_process_device_irq_check ======

int x86_process_device_irq_check(const X86_PROCESS_IMAGE *image, int (*inject)(void), int (*handle)(void))
{
    X86_PROCESS_RESULT result;
    X86_IRQ_REGISTRY *registry = x86_irq_registry();
    if (!opened || current || !registry || !inject || !handle ||
        !x86_irq_registry_bind(registry, DEVICE_PROBE_VECTOR, handle,
            device_probe_quiet, &device_probe_token)) return 0;
    gate(DEVICE_PROBE_VECTOR, (BOOT_U64)x86_process_device_irq, 0);
    device_probe_delivered = 0;
    device_probe_armed = 1;
    device_probe_inject = inject;
    int ran = x86_process_run(image, &result);
    int resumed = ran && device_probe_delivered == 1 &&
        result.Kind == X86_PROCESS_EXIT && result.Code == 0 && result.Writes == 1;
    int drained_ok = resumed && x86_process_drain_check();
    device_probe_armed = 0;
    device_probe_inject = 0;
    BOOT_U64 deliveries = x86_irq_registry_deliveries(registry, &device_probe_token);
    if (!x86_irq_registry_unbind(registry, &device_probe_token)) die("PROCESS_DEVICE_IRQ_RETAINED");
    gate(DEVICE_PROBE_VECTOR, (BOOT_U64)x86_process_stray, 3);
    return drained_ok && device_probe_delivered == 2 && deliveries == 2;
}
#endif

// ====== x86_process_irq_drained ======

void x86_process_irq_drained(void)
{
    // Intel SDM 3A §13.8.5: writing zero to EOI (B0h) retires the highest ISR bit.
    if (!x86_lapic_native_eoi()) die("PROCESS_TIMER_RETAINED");
    ++drained;
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
    const char *reason = frame[0] == VECTOR_STRAY ? "PROCESS_STRAY_IRQ" :
        frame[0] == 2 ? "PROCESS_STRAY_NMI" : "PROCESS_KERNEL_FAULT";
    (void)kernel_log(3, reason);
    kernel_panic(reason);
}

#ifdef KERNEL_TEST_K6_PROCESS
typedef struct {
    _Alignas(16) BOOT_U64 Frame[USER_FRAME_WORDS];
    KERNEL_PHYSICAL_LEASE Lease;
    BOOT_U64 Entry, RemainingIrqs, Written;
    BOOT_U32 RemainingTicks, Yields, Waiting;
} PROCESS_SLOT;

// ====== run_slot ======

static void run_slot(PROCESS_SLOT *slot, X86_PROCESS_RESULT *result, BOOT_U64 endpoint)
{
    k6_user_root = slot->Lease.Base;
    current = result;
    written_bytes = slot->Written;
    budget_left = slot->RemainingIrqs;
    k6_exit_kind = 0;
    yielded = 0;
    waiting = 0;
    yield_frame = slot->Frame;
    ipc_endpoint = endpoint;
    if (!kernel_deadline_start(budget_clock, slot->RemainingTicks, &budget_deadline)) die("PROCESS_CLOCK_FAILED");
    timer_arm();
    if (slot->Yields) x86_process_resume(slot->Frame);
    else x86_process_enter(slot->Entry, STACK_TOP, endpoint);
    BOOT_U32 elapsed = kernel_clock_elapsed(budget_clock, budget_deadline.Start);
    yield_frame = 0;
    ipc_endpoint = 0;
    turn_finish(result);
    slot->Written = written_bytes;
    slot->RemainingIrqs = budget_left;
    if (yielded) {
        ++slot->Yields;
        slot->Waiting = waiting;
        if (elapsed >= slot->RemainingTicks) result->Kind = X86_PROCESS_BUDGET;
        else slot->RemainingTicks -= elapsed;
    }
}

// ====== x86_process_pair ======

int x86_process_pair(const X86_PROCESS_IMAGE images[2], X86_PROCESS_RESULT results[2], BOOT_U32 yields[2], int use_ipc)
{
    PROCESS_SLOT slots[2] = {0};
    PROCESS_PLAN plan;
    BOOT_U64 budget;
    if (!opened || current || !images || !results || !yields ||
        !kernel_physical_budget_read(owner_window->Pool, &budget)) return 0;
    BOOT_U32 ticks = kernel_clock_ms(budget_clock, PROCESS_BUDGET_MS);
    if (ticks == UINT32_MAX) return 0;
    for (BOOT_U32 index = 0; index < 2; ++index) {
        if (x86_process_plan(images[index].Bytes, images[index].Length, &plan) ||
            !map_image(&images[index], &plan, &slots[index].Lease)) {
            if (index && !reap(&slots[0].Lease)) die("PROCESS_REAP_FAILED");
            if (!kernel_physical_budget_matches(owner_window->Pool, budget)) die("PROCESS_REAP_FAILED");
            return 0;
        }
        slots[index].Entry = plan.Entry;
        slots[index].RemainingTicks = ticks;
        slots[index].RemainingIrqs = x86_process_budget_initial();
        results[index] = (X86_PROCESS_RESULT){0};
    }
    BOOT_U64 channel = use_ipc ? x86_ipc_open(&ipc_channel) : 0;
    for (BOOT_U32 turn = 0; turn < 32 && (!use_ipc || channel) && (!results[0].Kind || !results[1].Kind); ++turn) {
        BOOT_U32 index = turn % 2;
        if (results[index].Kind) continue;
        BOOT_U64 endpoint = channel ? channel | index : 0;
        if (slots[index].Waiting) {
            BOOT_U64 ready = x86_ipc_receive_ready(&ipc_channel, endpoint, endpoint);
            if (ready == IPC_WOULD_BLOCK) continue;
            GPR(slots[index].Frame, GPR_RAX) = ready;
            slots[index].Waiting = 0;
        }
        run_slot(&slots[index], &results[index], endpoint);
        if (__readcr3() != k6_kernel_root || !roots_stable() || kernel_stack[0] != STACK_CANARY)
            die("PROCESS_ROOT_CHANGED");
        if (results[index].Kind) {
            if (channel) x86_ipc_close(&ipc_channel);
            if (!reap(&slots[index].Lease)) die("PROCESS_REAP_FAILED");
        }
    }
    if (channel) x86_ipc_finish(&ipc_channel);
    for (BOOT_U32 index = 0; index < 2; ++index) {
        if (slots[index].Lease.Id && !reap(&slots[index].Lease)) die("PROCESS_REAP_FAILED");
        yields[index] = slots[index].Yields;
    }
    if (!kernel_physical_budget_matches(owner_window->Pool, budget)) die("PROCESS_REAP_FAILED");
    return results[0].Kind && results[1].Kind;
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
    delay_next_syscall = 1;
}

// ====== x86_process_drain_check ======

int x86_process_drain_check(void)
{
    const X86_LAPIC_MAPPING *mapping = owner_timer->Mapping;
    BOOT_U64 before = drained;
    BOOT_U32 pending = 0;
    if (!x86_timer_confirm_write(mapping, 0x3e0, 3) || !x86_timer_confirm_write(mapping, 0x320, TIMER_VECTOR) ||
        !x86_timer_write(mapping, 0x380, 1)) return 0;
    for (BOOT_U32 attempt = 0; attempt < 0x1000000U && !(pending & IRR_TIMER_BIT); ++attempt) {
        if (!x86_lapic_native_read(IRR_TIMER_BANK, &pending)) return 0;
    }
    if (!(pending & IRR_TIMER_BIT)) return 0;
    if (device_probe_armed) device_probe_send();
    timer_quiesce();
    return drained == before + 1;
}
#endif
