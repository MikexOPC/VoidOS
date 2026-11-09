#ifndef OS_X86_PROCESS_H
#define OS_X86_PROCESS_H
#include "arch/x86/mm/window/page.h"
#include "arch/x86/kernel/timer/lapic/timer.h"

#define X86_PROCESS_EXIT 1ULL
#define X86_PROCESS_FAULT 2ULL
#define X86_PROCESS_BAD_FRAME 3ULL
#define X86_PROCESS_REJECTED 4ULL
#define X86_PROCESS_BUDGET 5ULL

typedef struct {
    BOOT_U64 Kind, Code, Vector, Error, Rip, Cr2, Writes, Irqs;
} X86_PROCESS_RESULT;

typedef struct {
    const unsigned char *Bytes;
    BOOT_U64 Length;
} X86_PROCESS_IMAGE;

int x86_process_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer);
int x86_process_run(const X86_PROCESS_IMAGE *image, X86_PROCESS_RESULT *result);
int x86_process_close(void);
#ifdef KERNEL_TEST_K6_PROCESS
int x86_process_nmi_report(BOOT_U64 window);
void x86_process_nmi_arm(BOOT_U64 window);
BOOT_U64 x86_process_window_first(BOOT_U64 window);
BOOT_U64 x86_process_window_last(BOOT_U64 window);
BOOT_U64 x86_process_copy_instruction(void);
BOOT_U64 x86_process_budget_ticks(void);
int x86_process_drain_check(void);
void x86_process_spurious_check(void);
#endif
#endif
