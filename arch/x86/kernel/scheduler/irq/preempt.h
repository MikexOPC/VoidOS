#ifndef X86_SCHEDULER_IRQ_PREEMPT_H
#define X86_SCHEDULER_IRQ_PREEMPT_H
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "arch/x86/kernel/interrupts/return/table.h"

int x86_preempt_open(const X86_LAPIC_TIMER *timer);
int x86_preempt_close(void);
int x86_preempt_pause(void);
int x86_preempt_resume(void);
int x86_preempt_rearm(void);
int x86_preempt_report(void);
BOOT_U64 x86_preempt_ticks(void);
void x86_preempt_after(const X86_IRQ_FRAME *frame);
int x86_preempt_ap_open(void);
BOOT_U64 x86_preempt_cpu_progress(BOOT_U32 cpu);
int x86_preempt_task_frame_valid(const X86_IRQ_FRAME *frame);

#endif
