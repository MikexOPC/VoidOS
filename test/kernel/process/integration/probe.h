#ifndef VOIDOS_K6_PROCESS_PROBE_H
#define VOIDOS_K6_PROCESS_PROBE_H
#include "arch/x86/mm/window/page.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"

int x86_k6_process_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer);
#endif
