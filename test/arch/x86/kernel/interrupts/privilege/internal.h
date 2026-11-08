#ifndef VOIDOS_K5_PRIVILEGE_INTERNAL_H
#define VOIDOS_K5_PRIVILEGE_INTERNAL_H

#include "probe.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"

#define K5_PAGE 4096ULL
#define K5_USER_CODE 0x400000ULL
#define K5_RSP0_PAGE 0x404000ULL

extern volatile BOOT_U64 k5_negative, k5_irq_waiting, k5_irq_count, k5_irq_goal, k5_irq_stack, k5_irq_frame[5];
extern volatile X86_EXCEPTION_FRAME k5_fault;
extern BOOT_U64 k5_irq_last;
extern X86_LAPIC_TIMER *k5_timer;
extern BOOT_U32 k5_vector, k5_reload;
extern const unsigned char *k5_blob, *k5_blob_end;

int session_open(X86_PAGE_WINDOW *window, BOOT_U64 *root, BOOT_U64 *budget, BOOT_U64 *alternate);
int verify_and_release(X86_PAGE_WINDOW *window, BOOT_U64 root, BOOT_U64 budget, BOOT_U64 alternate);
int nmi_arm(BOOT_U64 phase, BOOT_U64 first, BOOT_U64 last);
int nmi_report(BOOT_U64 active);
int trace_field(const char *label, BOOT_U64 value);
int timer_quiet(void);
volatile unsigned char *tss_rsp0(void);
BOOT_U64 rsp0_swap(volatile unsigned char *field, BOOT_U64 value);

#endif
