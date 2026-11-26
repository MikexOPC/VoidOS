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
#include "kernel/process/table.h"
#include "kernel/sync/internal.h"
#include <intrin.h>

// Intel SDM 3A §§5.5.4, 5.6: P bit 0, R/W bit 1, U/S bit 2; XD bit 63.
#define PTE_PRESENT 1ULL
#define PTE_WRITE 2ULL
#define PTE_USER 4ULL
#define PTE_NX (1ULL << 63)
#define PTE_ADDRESS 0x000ffffffffff000ULL
#define PAGE_BYTES 4096ULL
#define TABLE_PAGES 11U
#define USER_PT_COUNT 8U
#define STACK_PAGES 16U
#define STACK_BASE 0x13f0000ULL
#define STACK_TOP 0x1400000ULL
#define IMAGE_BASE 0x400000ULL
#define PT_OFFSET(address) (1536U + (BOOT_U32)(((address) - IMAGE_BASE) >> 12))
#define SLOT_IMAGE 511U
// 3584 pages = 14 MiB image ceiling, matching KERNEL_PROCESS_MAX_IMAGE_PAGES and Rust MAX_IMAGE_PAGES.
#define MAX_IMAGE_PAGES 3584U
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
#define SYS_OPEN 6ULL
#define SYS_READ 7ULL
#define SYS_WRITE_FILE 8ULL
#define SYS_CLOSE 9ULL
#define SYS_UNLINK 10ULL
#define SYS_SPAWN 11ULL
#define SYS_WAIT_PID 12ULL
#define SYS_LIST_DIR 13ULL
#define SYS_SYNC 14ULL
#define SYS_CONSOLE_READ 15ULL
#define SYS_DISPLAY_INFO 16ULL
#define SYS_DISPLAY_ACQUIRE 17ULL
#define SYS_DISPLAY_PRESENT 18ULL
#define SYS_INPUT_POLL 19ULL
#define SYS_SHM_CREATE 20ULL
#define SYS_SHM_MAP 21ULL
#define SYS_SHM_UNMAP 22ULL
#define SYS_SHM_PUBLISH 23ULL
#define SYS_SHM_VIEW 24ULL
#define SYS_PROCESS_STOP 25ULL
#define SYS_IPC_CLOSE 26ULL
#define SYS_GUI_WAIT 27ULL
#define SYS_SYS_INFO 28ULL
#define SYS_GPU_INFO 29ULL
#define WAIT_HANDLE_GUI_MASK (1ULL << 63)

#define X86_GPU_BACKEND_GOP_SW    0U
#define X86_GPU_BACKEND_VIRTIO_2D 1U
#define X86_GPU_BACKEND_VIRTIO_3D 2U
#define X86_GPU_BACKEND_INTEL     3U
#define X86_GPU_BACKEND_AMD       4U

#define X86_GPU_CAP_2D_FLUSH      (1U << 0)
#define X86_GPU_CAP_2D_BLIT       (1U << 1)
#define X86_GPU_CAP_HW_CURSOR     (1U << 2)
#define X86_GPU_CAP_3D_VIRGL      (1U << 3)
#define X86_GPU_CAP_VULKAN_VENUS  (1U << 4)

typedef struct {
    BOOT_U32  Backend;
    BOOT_U32  Caps;
    BOOT_U32  Width;
    BOOT_U32  Height;
    BOOT_U32  Stride;
    BOOT_U32  Format;
    BOOT_U64  PresentCalls;
    BOOT_U64  MaxShmBytes;
    BOOT_U32  VendorId;
    BOOT_U32  DeviceId;
    char      Name[16];
} X86_GPU_INFO;
_Static_assert(sizeof(X86_GPU_INFO) == 64, "X86_GPU_INFO ABI size");

typedef struct {
    BOOT_U64  TotalPhysPages;
    BOOT_U64  FreePhysPages;
    BOOT_U64  HeapTotalPages;
    BOOT_U64  HeapFreePages;
    BOOT_U64  UptimeMs;
    BOOT_U64  PreemptTicks;
    BOOT_U32  ActiveProcesses;
    BOOT_U32  OnlineCpus;
    BOOT_U32  ChannelCount;
    BOOT_U32  Reserved;
} X86_SYS_INFO;

_Static_assert(sizeof(X86_SYS_INFO) == 64U, "X86_SYS_INFO ABI size");

typedef struct {
    BOOT_U64               InitialArg;
    BOOT_U32               Flags;
    BOOT_U32               UseBudget;
    KERNEL_PROCESS_BUDGET  Budget;
} X86_SPAWN_ARGS;

typedef struct {
    BOOT_U32  ExitKind;
    BOOT_U32  Reserved;
    BOOT_U64  ExitCode;
    BOOT_U64  FaultVector;
} X86_WAIT_STATUS;

_Static_assert(sizeof(X86_SPAWN_ARGS) == 32U, "X86_SPAWN_ARGS ABI size");
_Static_assert(sizeof(X86_WAIT_STATUS) == 24U, "X86_WAIT_STATUS ABI size");
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
#define K6_NEGATIVE(number) (process_negative == (number))
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
void x86_process_syscall_suspend(void);
void x86_process_syscall_resume(BOOT_U64 resume_rsp);
void x86_process_leave(void);
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
int x86_process_copy_recover(BOOT_U64 *frame, BOOT_U64 cr2);
void x86_process_irq_capture(const BOOT_U64 *fx, const BOOT_U64 *gprs, const BOOT_U64 *iret);
void x86_process_on_preempt_tick(const X86_IRQ_FRAME *frame);
BOOT_U32 x86_process_plan(const unsigned char *bytes, BOOT_U64 length, PROCESS_PLAN *plan);
BOOT_U32 x86_process_plan_budgeted(const unsigned char *bytes, BOOT_U64 length,
    BOOT_U64 max_pages, PROCESS_PLAN *plan);
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

// ====== stato per-CPU per transizioni ring 3 (layout condiviso con Assembly) ======

typedef struct {
    _Alignas(64) BOOT_U64       KernelRoot;
    BOOT_U64                    UserRoot;
    BOOT_U64                    SyscallTop;
    BOOT_U64                    UserRsp;
    BOOT_U64                    SavedRsp;
    volatile BOOT_U64           ExitKind;
    volatile BOOT_U64           FaultCr2;
    volatile BOOT_U64           Fault[7];
    BOOT_U64                    Owner;
    volatile BOOT_U64           CopyPointer;
    volatile BOOT_U64           CopyLength;
    volatile BOOT_U64           CopyWrite;
    volatile BOOT_U32           Active;
    volatile BOOT_U32           LeaveRequested;
    volatile BOOT_U32           InSyscall;
    BOOT_U32                    Reserved;
    _Alignas(16) unsigned char  FxSaved[512];
    BOOT_U64                    KernelStackTop;
    BOOT_U64                    KernelStackBase;
    BOOT_U64                    SyscallResumeRsp;
    volatile BOOT_U32           SyscallSuspended;
    volatile BOOT_U32           SyscallNoSuspend;
    BOOT_U64                    BudgetLeft;
    BOOT_U64                    WrittenBytes;
    const KERNEL_CLOCK         *BudgetClock;
    KERNEL_DEADLINE             BudgetDeadline;
    BOOT_U64                   *YieldFrame;
    BOOT_U32                    Yielded;
    BOOT_U32                    Waiting;
    BOOT_U32                    RenewBudget;
    BOOT_U32                    BudgetReserved;
    IPC_CHANNEL                *IpcChannel;
    BOOT_U64                    IpcEndpoint;
    KERNEL_PROCESS_TABLE       *Table;
    BOOT_U64                    Pid;
    BOOT_U64                    WaitHandle;
    X86_PROCESS_RESULT         *Current;
    unsigned char               Buffer[MAX_COPY + 1];
    unsigned char               CopyScratch[256];
} X86_PROCESS_CPU;

_Static_assert(offsetof(X86_PROCESS_CPU, KernelRoot) == 0, "X86_PROCESS_CPU KernelRoot offset");
_Static_assert(offsetof(X86_PROCESS_CPU, UserRoot) == 8, "X86_PROCESS_CPU UserRoot offset");
_Static_assert(offsetof(X86_PROCESS_CPU, SyscallTop) == 16, "X86_PROCESS_CPU SyscallTop offset");
_Static_assert(offsetof(X86_PROCESS_CPU, UserRsp) == 24, "X86_PROCESS_CPU UserRsp offset");
_Static_assert(offsetof(X86_PROCESS_CPU, SavedRsp) == 32, "X86_PROCESS_CPU SavedRsp offset");
_Static_assert(offsetof(X86_PROCESS_CPU, ExitKind) == 40, "X86_PROCESS_CPU ExitKind offset");
_Static_assert(offsetof(X86_PROCESS_CPU, FaultCr2) == 48, "X86_PROCESS_CPU FaultCr2 offset");
_Static_assert(offsetof(X86_PROCESS_CPU, Fault) == 56, "X86_PROCESS_CPU Fault offset");
_Static_assert(offsetof(X86_PROCESS_CPU, Owner) == 112, "X86_PROCESS_CPU Owner offset");
_Static_assert(offsetof(X86_PROCESS_CPU, CopyPointer) == 120, "X86_PROCESS_CPU CopyPointer offset");
_Static_assert(offsetof(X86_PROCESS_CPU, CopyLength) == 128, "X86_PROCESS_CPU CopyLength offset");
_Static_assert(offsetof(X86_PROCESS_CPU, CopyWrite) == 136, "X86_PROCESS_CPU CopyWrite offset");
_Static_assert(offsetof(X86_PROCESS_CPU, Active) == 144, "X86_PROCESS_CPU Active offset");
_Static_assert(offsetof(X86_PROCESS_CPU, LeaveRequested) == 148, "X86_PROCESS_CPU LeaveRequested offset");
_Static_assert(offsetof(X86_PROCESS_CPU, InSyscall) == 152, "X86_PROCESS_CPU InSyscall offset");
_Static_assert(offsetof(X86_PROCESS_CPU, FxSaved) == 160, "X86_PROCESS_CPU FxSaved offset");
_Static_assert(offsetof(X86_PROCESS_CPU, KernelStackTop) == 672, "X86_PROCESS_CPU KernelStackTop offset");
_Static_assert(offsetof(X86_PROCESS_CPU, KernelStackBase) == 680, "X86_PROCESS_CPU KernelStackBase offset");
_Static_assert(offsetof(X86_PROCESS_CPU, SyscallResumeRsp) == 688, "X86_PROCESS_CPU SyscallResumeRsp offset");
_Static_assert(offsetof(X86_PROCESS_CPU, SyscallSuspended) == 696, "X86_PROCESS_CPU SyscallSuspended offset");
_Static_assert(offsetof(X86_PROCESS_CPU, SyscallNoSuspend) == 700, "X86_PROCESS_CPU SyscallNoSuspend offset");

// ====== stato condiviso del runtime dei processi ======

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
    BOOT_U32                  State, Opened, Permanent, TimerLvtSaved, TimerDivideSaved;
    BOOT_U64                  Drained;
    BOOT_U32                  InputIrqMask;
    X86_IRQ_TOKEN             InputIrqTokens[X86_IRQ_DEVICE_SLOTS];
} X86_PROCESS_RUNTIME;

_Static_assert(X86_PROCESS_FRAME_WORDS == USER_FRAME_WORDS, "Saved process frame size");

// ====== stato condiviso con l'ingresso Assembly ======

extern X86_PROCESS_RUNTIME process_rt;
extern X86_PROCESS_CPU x86_process_cpus[X86_PERCPU_CAPACITY];
extern volatile BOOT_U64 process_drain;
extern const unsigned char process_fx_clean[512];
#ifdef KERNEL_TEST_K6_PROCESS
extern volatile BOOT_U64 process_window, process_window_budget, process_negative;
extern volatile BOOT_U64 process_nmi_armed, process_nmi_count, process_nmi_rip, process_nmi_cs, process_nmi_cr3, process_nmi_gs, process_nmi_flags;
extern BOOT_U32 process_delay_next_syscall, process_probe_armed, process_probe_delivered;
void process_probe_send(void);
#endif

// ====== funzioni interne condivise fra i file del modulo ======

X86_PROCESS_CPU *x86_process_cpu_current(void);
__declspec(noreturn) void process_die(const char *reason);
void process_timer_arm(void);
void process_timer_start(void);
void process_timer_quiesce(void);
int process_budget_expired(BOOT_U64 rip);
int x86_process_slot_step_managed(X86_PROCESS_SLOT *slot, X86_PROCESS_RESULT *result,
    KERNEL_PROCESS_TABLE *table, BOOT_U64 pid, BOOT_U64 initial_arg,
    BOOT_U64 *wait_handle_io, int renew_budget);
BOOT_U64 x86_process_copy_user(X86_PROCESS_CPU *cpu, void *target, const void *source,
    BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write);
#endif
