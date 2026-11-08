#ifndef VOIDOS_K5_PRIVILEGE_PROBE_H
#define VOIDOS_K5_PRIVILEGE_PROBE_H

#include "arch/x86/mm/window/page.h"
#include "arch/x86/kernel/timer/lapic/timer.h"

int x86_k5_privilege_probe(X86_PAGE_WINDOW *window);
#ifdef KERNEL_TEST_K5_AP
void x86_k5_ap_probe(X86_PAGE_WINDOW *window);
void x86_k5_ap_run(unsigned int index);
#endif
int x86_k5_copy_probe(X86_PAGE_WINDOW *window);
int x86_k5_syscall_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer);
int x86_k5_irq_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer);

#endif
