#include "internal.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include <intrin.h>

// Intel SDM 3A §2.5: CR4.SMEP bit 20, CR4.SMAP bit 21. §2.3: EFLAGS.AC bit 18.
#define K5_CR4_SMEP (1ULL << 20)
#define K5_CR4_SMAP (1ULL << 21)
#define K5_AC (1ULL << 18)
#define K5_MAX_COPY 256U
#define K5_RESULT_RANGE (~0ULL - 1)
#define K5_RESULT_LENGTH (~0ULL - 2)

extern const unsigned char x86_k5_copy_user[], x86_k5_copy_user_end[];
extern const unsigned char x86_k5_copy_ok[];
extern const unsigned char x86_k5_copy_wait_first[], x86_k5_copy_wait_last[];
extern const unsigned char x86_k5_copy_first[], x86_k5_copy_last[], x86_k5_copy_fixup[];
extern const unsigned char x86_k5_copy_outside[];
void x86_k5_copy_nmi(void);
void x86_k5_copy_pf(void);
void x86_k5_enter(BOOT_U64 entry, BOOT_U32 mode, BOOT_U64 argument);
__declspec(noreturn) void x86_k5_stop(void);
BOOT_U64 x86_k5_copy_raw(void *target, const void *source, BOOT_U64 bytes);
BOOT_U32 x86_k5_copy_range(BOOT_U64 pointer, BOOT_U64 length);
BOOT_U32 x86_k5_copy_fault(BOOT_U64 rip, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 error, BOOT_U64 cr2,
    BOOT_U64 first, BOOT_U64 last, BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write);
BOOT_U32 x86_k5_frame_valid(BOOT_U64 mode, BOOT_U64 vector, BOOT_U64 error,
    BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 rsp, BOOT_U64 ss);

volatile BOOT_U64 k5_copy_wait, k5_copy_budget;
static volatile BOOT_U64 faults, window_pointer, window_length, window_write, copies;
static _Alignas(16) unsigned char buffer[K5_MAX_COPY];

// ====== x86_k5_copy_recover ======

int x86_k5_copy_recover(BOOT_U64 *frame, BOOT_U64 cr2)
{
    enum { ERROR_CODE, RIP, CS, FLAGS };
    if (!x86_k5_copy_fault(frame[RIP], frame[CS], frame[FLAGS], frame[ERROR_CODE], cr2,
        (BOOT_U64)x86_k5_copy_first, (BOOT_U64)x86_k5_copy_last,
        window_pointer, window_length, window_write)) return 0;
    frame[RIP] = (BOOT_U64)x86_k5_copy_fixup;
    frame[FLAGS] &= ~K5_AC;
    ++faults;
    return 1;
}

// ====== raw_call ======

static BOOT_U64 raw_call(void *target, const void *source, BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write)
{
    window_pointer = pointer;
    window_length = k5_negative == 22 ? 1 : length;
    window_write = write;
    BOOT_U64 result = x86_k5_copy_raw(target, source, length);
    if (__readeflags() & K5_AC) x86_k5_stop();
    window_length = 0;
    return result;
}

// ====== copy_call ======

static int copy_call(BOOT_U64 *frame)
{
    BOOT_U64 number = K5_GPR(frame, 14), pointer = K5_GPR(frame, 8), length = K5_GPR(frame, 9);
    if (k5_negative == 21) ((void (*)(void))K5_USER_CODE)();
    BOOT_U32 range = x86_k5_copy_range(pointer, length);
    if (range == 3) { K5_GPR(frame, 14) = K5_RESULT_LENGTH; return 1; }
    if (range != 1) { K5_GPR(frame, 14) = K5_RESULT_RANGE; return 1; }
    ++copies;
    if (number == 2) {
        void *target = k5_negative == 24 ? (void *)X86_KERNEL_IST_GUARD : buffer;
        if (raw_call(target, (const void *)pointer, pointer, length, 0)) {
            for (BOOT_U32 index = 0; index < K5_MAX_COPY; ++index) ((volatile unsigned char *)buffer)[index] = 0;
            K5_GPR(frame, 14) = K5_RESULT_RANGE;
            return 1;
        }
        BOOT_U64 sum = 0;
        for (BOOT_U64 index = 0; index < length; ++index) sum += buffer[index];
        K5_GPR(frame, 14) = sum;
        return 1;
    }
    for (BOOT_U64 index = 0; index < length; ++index) buffer[index] = (unsigned char)(index ^ 0xa5);
    K5_GPR(frame, 14) = raw_call((void *)pointer, buffer, pointer, length, 1) ? K5_RESULT_RANGE : 0;
    return 1;
}

// ====== x86_k5_copy_probe ======

int x86_k5_copy_probe(X86_PAGE_WINDOW *window)
{
    static BOOT_U32 admitted_once;
    int registers[4];
    BOOT_U64 root, budget, alternate, previous, saved[5], ok;
    if (admitted_once) return 0;
    admitted_once = 1;
    // Intel SDM 3A §2.5 (CR4): CPUID.07H.00H:EBX SMEP bit 7, SMAP bit 20.
    __cpuid(registers, 0);
    if (registers[0] < 7) return 0;
    __cpuidex(registers, 7, 0);
    if (!(registers[1] & (1 << 7)) || !(registers[1] & (1 << 20))) return 0;
    if (!kernel_log(0, "KERNEL_K5_SMEP_SMAP_SUPPORTED")) return 0;
    k5_blob = x86_k5_copy_user;
    k5_blob_end = x86_k5_copy_user_end;
    k5_gate_nmi = (BOOT_U64)x86_k5_copy_nmi;
    k5_gate_pf = (BOOT_U64)x86_k5_copy_pf;
    k5_sys_extra = copy_call;
    if (!session_open(window, &root, &budget, &alternate)) return 0;
    volatile unsigned char *field = tss_rsp0();
    if (!field) x86_k5_stop();
    previous = rsp0_swap(field, K5_RSP0_PAGE + K5_PAGE);
    k5_sys_open(saved);
    BOOT_U64 cr4 = __readcr4();
    __writecr4(cr4 | K5_CR4_SMEP | K5_CR4_SMAP);
    k5_copy_budget = 0x4000000ULL;
    k5_copy_wait = !k5_negative;
    int passed = trace_field("K5_COPY_INSTRUCTION: ", (BOOT_U64)x86_k5_copy_first) &&
        trace_field("K5_COPY_OUTSIDE: ", (BOOT_U64)x86_k5_copy_outside) &&
        nmi_arm(1, (BOOT_U64)x86_k5_copy_wait_first, (BOOT_U64)x86_k5_copy_wait_last);
    if (passed) x86_k5_enter(K5_USER_CODE, 5, 0);
    ok = K5_USER_CODE + ((BOOT_U64)x86_k5_copy_ok - (BOOT_U64)x86_k5_copy_user);
    __writecr4(cr4);
    k5_sys_close(saved);
    passed = passed && k5_fault.Rip == ok && faults == 4 && nmi_report((BOOT_U64)x86_percpu_bsp()) &&
        x86_k5_frame_valid(5, k5_fault.Vector, k5_fault.Error, k5_fault.Cs, k5_fault.Flags, k5_fault.Rsp, k5_fault.Ss) &&
        trace_field("K5_COPY_FAULTS: ", faults) && trace_field("K5_COPY_COUNT: ", copies) &&
        kernel_log(0, "KERNEL_K5_COPY_RESUMED");
    if (rsp0_swap(field, previous) != K5_RSP0_PAGE + K5_PAGE) x86_k5_stop();
    if (!verify_and_release(window, root, budget, alternate)) return 0;
    return passed && kernel_log(0, "KERNEL_K5_COPY_OK");
}
