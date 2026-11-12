#ifndef VOIDOS_K6_PROCESS_PROBE_H
#define VOIDOS_K6_PROCESS_PROBE_H
#include "arch/x86/mm/window/page.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"

extern const unsigned char k6_program_hello[];
extern const BOOT_U64 k6_program_hello_length;
int x86_k6_process_probe(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer,
    const X86_WATCHDOG_ENV *env, const KERNEL_ACPI_DIRECTORY *directory);
int x86_k6_keyboard_probe(const X86_WATCHDOG_ENV *env, const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock);
#endif
