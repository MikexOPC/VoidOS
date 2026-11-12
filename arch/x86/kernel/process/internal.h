#ifndef OS_X86_PROCESS_INTERNAL_H
#define OS_X86_PROCESS_INTERNAL_H
#include "process.h"
#include "slot.h"
#include "arch/x86/kernel/cpu/percpu/clock/clock.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/descriptors/stacks.h"
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
#define STACK_WORD (*(volatile BOOT_U64 *)X86_KERNEL_SYSCALL_BASE)
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
#define WINDOW_LABELS(number) \
    extern const unsigned char x86_process_window_##number##_first[], x86_process_window_##number##_last[]
WINDOW_LABELS(1); WINDOW_LABELS(2); WINDOW_LABELS(3); WINDOW_LABELS(4); WINDOW_LABELS(5); WINDOW_LABELS(6);
void x86_process_probe_read(BOOT_U64 address);
BOOT_U32 x86_process_nmi_valid(BOOT_U64 window, BOOT_U32 user_root, BOOT_U32 kernel_gs, BOOT_U32 ac, BOOT_U64 cs);
#endif
BOOT_U64 x86_ipc_send_check(const IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle, BOOT_U64 len);
void x86_process_resume(const BOOT_U64 *frame);
void x86_process_syscall(void);
void x86_process_nmi(void);
void x86_process_irq(void);
void x86_process_device_irq40(void);
void x86_process_device_irq41(void);
void x86_process_device_irq42(void);
void x86_process_device_irq43(void);
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

// ====== stato del runtime dei processi: una sola istanza sul BSP ======

typedef struct {
    X86_KERNEL_STACKS         SyscallStack;
    X86_IRQ_IDT               Idt;
    X86_KERNEL_IDTR           IdtPrevious, IdtProbe;
    X86_PAGE_WINDOW          *Window;
    X86_LAPIC_TIMER          *Timer;
    volatile unsigned char   *TssField;
    BOOT_U64                  Rsp0Previous, MsrSaved[5], AlternateSaved;
    BOOT_U64                  ExpectedImage, ExpectedWindows;
    KERNEL_PHYSICAL_LEASE     Held;
    BOOT_U32                  State, Opened, TimerLvtSaved, TimerDivideSaved;
    BOOT_U64                  BudgetLeft, Drained, WrittenBytes;
    const KERNEL_CLOCK       *BudgetClock;
    KERNEL_DEADLINE           BudgetDeadline;
    BOOT_U64                 *YieldFrame;
    BOOT_U32                  Yielded, Waiting;
    IPC_CHANNEL              *IpcChannel;
    BOOT_U64                  IpcEndpoint;
    X86_PROCESS_RESULT       *Current;
    BOOT_U32                  InputIrqMask;
    X86_IRQ_TOKEN             InputIrqTokens[X86_IRQ_CAPACITY];
    volatile BOOT_U64         CopyPointer, CopyLength, CopyWrite;
    unsigned char             Buffer[MAX_COPY + 1];
} X86_PROCESS_RUNTIME;

_Static_assert(X86_PROCESS_FRAME_WORDS == USER_FRAME_WORDS, "Saved process frame size");

// ====== stato condiviso con l'ingresso Assembly ======

extern X86_PROCESS_RUNTIME process_rt;
extern volatile BOOT_U64 k6_owner, k6_kernel_root, k6_user_root, k6_stack_top, k6_user_rsp, k6_saved_rsp;
extern volatile BOOT_U64 k6_fault[7], k6_fault_cr2, k6_exit_kind, k6_drain;
extern unsigned char k6_fx_saved[512];
extern const unsigned char k6_fx_clean[512];
#ifdef KERNEL_TEST_K6_PROCESS
extern volatile BOOT_U64 k6_window, k6_window_budget, k6_negative;
extern volatile BOOT_U64 k6_nmi_armed, k6_nmi_count, k6_nmi_rip, k6_nmi_cs, k6_nmi_cr3, k6_nmi_gs, k6_nmi_flags;
extern BOOT_U32 process_delay_next_syscall, process_probe_armed, process_probe_delivered;
void process_probe_send(void);
#endif

// ====== funzioni interne condivise fra i file del modulo ======

__declspec(noreturn) void process_die(const char *reason);
void process_timer_arm(void);
void process_timer_start(void);
void process_timer_quiesce(void);
int process_budget_expired(BOOT_U64 rip);
#endif
