#include "internal.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>

// Intel SDM 2B, SYSCALL and SYSRET: IA32_STAR, IA32_LSTAR, IA32_CSTAR; FMASK C0000084h.
#define K5_STAR 0xc0000081U
#define K5_LSTAR 0xc0000082U
#define K5_CSTAR 0xc0000083U
#define K5_FMASK 0xc0000084U
#define K5_EFER_MSR 0xc0000080U
// Intel SDM 3A §2.2: EFER.SCE is bit 0.
#define K5_EFER_SCE 1ULL
// Intel SDM 3A §2.3: TF 8, IF 9, DF 10, IOPL 12-13, NT 14, AC 18.
#define K5_FLAGS_MASK 0x47700ULL
// Intel SDM 3A §6.8.8: STAR 47:32 gives CS 08h and SS 10h; 63:48 + 8 and + 16 give 2Bh and 33h.
#define K5_STAR_VALUE ((0x23ULL << 48) | (8ULL << 32))
#define K5_FX_WORDS 64U
#define K5_GPR(frame, index) ((frame)[K5_FX_WORDS + (index)])
#define K5_IRET(frame, index) ((frame)[K5_FX_WORDS + 15U + (index)])

extern const unsigned char x86_k5_sys_user[], x86_k5_sys_user_end[];
extern const unsigned char x86_k5_sys_loop_first[], x86_k5_sys_loop_last[];
extern const unsigned char x86_k5_sys_ok[], x86_k5_sys_nosce[];
extern const unsigned char x86_k5_sys_wait_first[], x86_k5_sys_wait_last[];
void x86_k5_syscall(void);
void x86_k5_enter(BOOT_U64 entry, BOOT_U32 mode, BOOT_U64 argument);
__declspec(noreturn) void x86_k5_stop(void);
BOOT_U32 x86_k5_sys_number(BOOT_U64 number);
BOOT_U32 x86_k5_sys_frame_valid(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);
BOOT_U32 x86_k5_frame_valid(BOOT_U64 mode, BOOT_U64 vector, BOOT_U64 error,
    BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);

volatile BOOT_U64 k5_sys_window, k5_sys_budget, k5_sys_stack, k5_sys_user_rsp;
static volatile BOOT_U64 calls;

// ====== sys_address ======

static BOOT_U64 sys_address(const unsigned char *label)
{
    return K5_USER_CODE + ((BOOT_U64)label - (BOOT_U64)x86_k5_sys_user);
}

// ====== negative_frame ======

static void negative_frame(BOOT_U64 *frame)
{
    if (k5_negative == 10) K5_IRET(frame, 0) = 0x0000800000000000ULL;
    if (k5_negative == 11) K5_IRET(frame, 0) = (BOOT_U64)x86_k5_stop;
    if (k5_negative == 12) K5_IRET(frame, 1) = 8;
    if (k5_negative == 13) K5_IRET(frame, 2) |= 0x3000ULL;
    if (k5_negative == 17) K5_GPR(frame, 3) ^= 1;
}

// ====== x86_k5_sys_dispatch ======

int x86_k5_sys_dispatch(BOOT_U64 *frame)
{
    negative_frame(frame);
    if (!x86_k5_sys_frame_valid(K5_IRET(frame, 0), K5_IRET(frame, 1), K5_IRET(frame, 2),
        K5_IRET(frame, 3), K5_IRET(frame, 4))) return 0;
    BOOT_U32 kind = x86_k5_sys_number(K5_GPR(frame, 14));
    if (kind == 1) K5_GPR(frame, 14) = K5_GPR(frame, 8) + 1;
    else if (kind == 2) K5_GPR(frame, 14) = 0;
    else K5_GPR(frame, 14) = ~0ULL;
    if (++calls % 3 == 0) {
        // Intel SDM 3A §13.5.4: divide 3E0h = 3, LVT 320h one-shot unmasked, initial count 380h.
        *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + 0x3e0) = 3;
        *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + 0x320) = k5_vector;
        *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + 0x380) = k5_reload;
    }
    return 1;
}

// ====== sys_case ======

static int sys_case(BOOT_U64 phase, BOOT_U64 root)
{
    BOOT_U64 argument = phase == 5 ? 1 : 0;
    BOOT_U64 last = sys_address(x86_k5_sys_loop_last);
    if (k5_negative == 15) argument = 3;
    if (k5_negative == 18) argument = 4;
    k5_irq_waiting = 0;
    k5_irq_count = 0;
    k5_irq_goal = 1;
    k5_irq_last = last;
    k5_irq_stack = 0;
    calls = 0;
    k5_sys_budget = 0x4000000ULL;
    k5_sys_window = phase == 2 && (!k5_negative || k5_negative == 16);
    if (!(phase == 2 ?
        nmi_arm(2, (BOOT_U64)x86_k5_sys_wait_first, (BOOT_U64)x86_k5_sys_wait_last) :
        nmi_arm(5, sys_address(x86_k5_sys_loop_first), last))) return 0;
    x86_k5_enter(K5_USER_CODE, 5, argument);
    return timer_quiet() && calls == 3 && k5_irq_count == 1 && x86_kernel_root() == root &&
        x86_k5_frame_valid(5, k5_fault.Vector, k5_fault.Error, k5_fault.Cs,
            k5_fault.Flags, k5_fault.Rsp, k5_fault.Ss) &&
        k5_fault.Rip == sys_address(x86_k5_sys_ok) && nmi_report(0) &&
        trace_field("K5_SYS_CALLS: ", calls) && trace_field("K5_IRQ_RIP: ", k5_irq_frame[0]) &&
        kernel_log(0, "KERNEL_K5_SYSCALL_RESUMED");
}

// ====== x86_k5_syscall_probe ======

int x86_k5_syscall_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    static BOOT_U32 admitted_once;
    BOOT_U64 root, budget, alternate, previous, saved[4];
    BOOT_U32 lvt, divide, count;
    const BOOT_U32 msrs[4] = {K5_STAR, K5_LSTAR, K5_CSTAR, K5_FMASK};
    if (admitted_once || !timer || !timer->Mapping || !timer->Reload || timer->Active ||
        !x86_timer_read(timer->Mapping, 0x320, &lvt) || !(lvt & 0x10000) ||
        !x86_timer_read(timer->Mapping, 0x3e0, &divide) ||
        !x86_timer_read(timer->Mapping, 0x380, &count) || count ||
        !x86_lapic_idle(timer->Mapping)) return 0;
    admitted_once = 1;
    k5_timer = timer;
    k5_reload = timer->Reload;
    k5_blob = x86_k5_sys_user;
    k5_blob_end = x86_k5_sys_user_end;
    if (!session_open(window, &root, &budget, &alternate)) return 0;
    volatile unsigned char *field = tss_rsp0();
    if (!field) x86_k5_stop();
    previous = rsp0_swap(field, K5_RSP0_PAGE + K5_PAGE);
    k5_sys_stack = K5_RSP0_PAGE + K5_PAGE;
    BOOT_U64 efer = __readmsr(K5_EFER_MSR);
    for (BOOT_U32 index = 0; index < 4; ++index) saved[index] = __readmsr(msrs[index]);
    __writemsr(K5_STAR, K5_STAR_VALUE);
    __writemsr(K5_LSTAR, (BOOT_U64)x86_k5_syscall);
    __writemsr(K5_CSTAR, (BOOT_U64)x86_k5_stop);
    __writemsr(K5_FMASK, K5_FLAGS_MASK);
    __writemsr(K5_EFER_MSR, efer | K5_EFER_SCE);
    int passed = sys_case(2, root) && sys_case(5, root);
    __writemsr(K5_EFER_MSR, efer);
    for (BOOT_U32 index = 0; index < 4; ++index) __writemsr(msrs[index], saved[index]);
    if (__readmsr(K5_EFER_MSR) & K5_EFER_SCE) x86_k5_stop();
    if (passed) {
        x86_k5_enter(K5_USER_CODE, 5, 2);
        passed = k5_fault.Vector == 6 && k5_fault.Cs == 0x33 && k5_fault.Rip == sys_address(x86_k5_sys_nosce) &&
            kernel_log(0, "KERNEL_K5_SCE_OFF_UD");
    }
    int stopped = timer_quiet();
    if (rsp0_swap(field, previous) != K5_RSP0_PAGE + K5_PAGE) x86_k5_stop();
    if (!stopped || !x86_timer_confirm_write(timer->Mapping, 0x3e0, divide) ||
        !x86_timer_confirm_write(timer->Mapping, 0x320, lvt) || !x86_lapic_idle(timer->Mapping)) x86_k5_stop();
    if (!verify_and_release(window, root, budget, alternate)) return 0;
    return passed && kernel_log(0, "KERNEL_K5_SYSCALL_OK");
}
