#include "probe.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/process/process.h"
#include "kernel/diagnostics/log.h"

#define PROGRAM(name) extern const unsigned char k6_program_##name[]; extern const BOOT_U64 k6_program_##name##_length
PROGRAM(hello); PROGRAM(null); PROGRAM(readonly); PROGRAM(privileged);
PROGRAM(kernel_jump); PROGRAM(bad_pointer); PROGRAM(bss); PROGRAM(irq);
#define IMAGE(name) {k6_program_##name, 0}

static _Alignas(8) unsigned char corrupted[64];

// ====== field ======

static int field(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== run ======

static int run(const unsigned char *bytes, BOOT_U64 length, X86_PROCESS_RESULT *result)
{
    X86_PROCESS_IMAGE image = {bytes, length};
    return x86_process_run(&image, result) && field("K6_KIND: ", result->Kind) && field("K6_CODE: ", result->Code);
}

// ====== exits ======

static int exits(const unsigned char *bytes, BOOT_U64 length, BOOT_U64 code, X86_PROCESS_RESULT *result)
{
    return run(bytes, length, result) && result->Kind == X86_PROCESS_EXIT && result->Code == code;
}

// ====== faults ======

static int faults(const unsigned char *bytes, BOOT_U64 length, BOOT_U64 vector, BOOT_U64 error, BOOT_U64 cr2)
{
    X86_PROCESS_RESULT result;
    return run(bytes, length, &result) && result.Kind == X86_PROCESS_FAULT &&
        result.Vector == vector && result.Error == error && (cr2 == ~0ULL || result.Cr2 == cr2) &&
        field("K6_FAULT_VECTOR: ", result.Vector) && field("K6_FAULT_ERROR: ", result.Error);
}

// ====== nmi_case ======

static int nmi_case(BOOT_U64 window)
{
    X86_PROCESS_RESULT result;
    x86_process_nmi_arm(window);
    if (!field("K6_NMI_WINDOW: ", window) ||
        !field("K6_NMI_FIRST: ", x86_process_window_first(window)) ||
        !field("K6_NMI_LAST: ", x86_process_window_last(window)) ||
        !kernel_log(0, "KERNEL_K6_NMI_ARMED")) return 0;
    return exits(k6_program_hello, k6_program_hello_length, 0, &result) && result.Writes == 1 &&
        x86_process_nmi_report(window) && kernel_log(0, "KERNEL_K6_NMI_RESUMED");
}

// ====== cases ======

static int cases(void)
{
    X86_PROCESS_RESULT result;
    for (BOOT_U32 index = 0; index < sizeof(corrupted); ++index) corrupted[index] = k6_program_hello[index];
    corrupted[16] = 3;
    if (!run(corrupted, sizeof(corrupted), &result) || result.Kind != X86_PROCESS_REJECTED ||
        !field("K6_REJECTED: ", result.Code) || !kernel_log(0, "KERNEL_K6_ELF_REJECTED")) return 0;
    if (!exits(k6_program_hello, k6_program_hello_length, 0, &result) || result.Writes != 1 ||
        !kernel_log(0, "KERNEL_K6_HELLO_OK")) return 0;
    if (!faults(k6_program_null, k6_program_null_length, 14, 4, 0) ||
        !faults(k6_program_readonly, k6_program_readonly_length, 14, 7, ~0ULL) ||
        !faults(k6_program_privileged, k6_program_privileged_length, 13, 0, ~0ULL) ||
        !faults(k6_program_kernel_jump, k6_program_kernel_jump_length, 14, 0x15, 0xffffffff80001000ULL) ||
        !kernel_log(0, "KERNEL_K6_FAULTS_OK")) return 0;
    if (!exits(k6_program_bad_pointer, k6_program_bad_pointer_length, 7, &result) ||
        !kernel_log(0, "KERNEL_K6_POINTERS_OK")) return 0;
    if (!exits(k6_program_bss, k6_program_bss_length, 0, &result) ||
        !exits(k6_program_bss, k6_program_bss_length, 0, &result) ||
        !kernel_log(0, "KERNEL_K6_ISOLATION_OK")) return 0;
    if (!x86_process_timer_arm() || !exits(k6_program_irq, k6_program_irq_length, 0, &result) ||
        result.Irqs != 1 || !kernel_log(0, "KERNEL_K6_IRQ_OK")) return 0;
    for (BOOT_U64 window = 1; window <= 6; ++window) {
        if (!nmi_case(window)) return 0;
    }
    return field("K6_COPY_INSTRUCTION: ", x86_process_copy_instruction());
}

// ====== x86_k6_process_probe ======

int x86_k6_process_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    if (!x86_user_mode_allowed()) return kernel_log(2, "PROCESS_DISABLED");
    if (!x86_process_open(window, timer)) return 0;
    int passed = cases();
    int stopped = x86_timer_stop(timer->Mapping);
    if (!x86_process_close() || !stopped) return 0;
    return passed && kernel_log(0, "KERNEL_K6_PROCESS_OK");
}
