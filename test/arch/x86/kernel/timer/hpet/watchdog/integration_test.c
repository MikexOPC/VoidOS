#include "arch/x86/kernel/timer/hpet/watchdog/integration/kernel.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/diagnostics/log.h"
#include <stdio.h>
#include <stdlib.h>

static KERNEL_BOOT_CONTEXT boot;
static X86_PAGE_WINDOW window;
static X86_OBJECT_ARENA arena;
static KERNEL_OBJECT_HANDLE acpi, madt;
static X86_PAGING_INVENTORY inventory;
static X86_APIC_PROFILE profile;
static X86_APIC_CAPABILITIES caps;
static X86_HPET_DESCRIPTION description;
static KERNEL_ACPI_DIRECTORY directory;
static KERNEL_MADT_TOPOLOGY topology;
static X86_LAPIC_MAPPING mapping;
static X86_LAPIC_TIMER timer;
static const X86_WATCHDOG_ENV *retained;
static unsigned prepared, processes, sessions, closed, ready, failure;

// ====== require ======

static void require(int condition)
{
    if (!condition) {
        fputs("Watchdog integration assertion failed\n", stderr);
        abort();
    }
}

// ====== kernel_panic ======

__declspec(noreturn) void kernel_panic(const char *message)
{
    fputs(message, stderr);
    abort();
}

// ====== x86_uart_lapic_mapping ======

const X86_LAPIC_MAPPING *x86_uart_lapic_mapping(void)
{
    return &mapping;
}

// ====== x86_object_arena_data ======

unsigned char *x86_object_arena_data(const X86_OBJECT_ARENA *owner,
    const KERNEL_OBJECT_HANDLE *handle)
{
    require(owner == &arena);
    if (handle == &acpi) {
        return failure == 1 ? NULL : (unsigned char *)&directory;
    }
    require(handle == &madt);
    return failure == 2 ? NULL : (unsigned char *)&topology;
}

// ====== x86_timer_run ======

int x86_timer_run(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *pages,
    const KERNEL_ACPI_DIRECTORY *tables, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *paging, const X86_WATCHDOG_ENV *env)
{
    require(context == &boot && pages == &window && tables == &directory);
    require(lapic == &mapping && paging == &inventory);
    require(env->Context == context && env->Window == pages && env->Inventory == paging);
    require(env->Mapping == lapic && env->Topology == &topology);
    require(env->Profile == &profile && env->Capabilities == &caps && env->Description == &description);
    require(!processes && !sessions && !closed);
    retained = env;
    ++prepared;
    return failure != 3;
}

// ====== x86_timer_ready ======

int x86_timer_ready(void)
{
    return ready;
}

// ====== x86_timer_owner ======

X86_LAPIC_TIMER *x86_timer_owner(void)
{
    require(ready && prepared && !closed);
    return &timer;
}

// ====== kernel_process_boot ======

int kernel_process_boot(X86_PAGE_WINDOW *pages, X86_LAPIC_TIMER *clock)
{
    require(pages == &window && clock == &timer && prepared == 1);
    require(!sessions && !closed);
    ++processes;
    return 1;
}

// ====== x86_ps2_session_run ======

int x86_ps2_session_run(const X86_WATCHDOG_ENV *env, const X86_PM_CLOCK *clock,
    const KERNEL_ACPI_DIRECTORY *tables)
{
    require(env == retained && clock == &timer.Clock && tables == &directory);
    require(processes == 1 && !closed);
    ++sessions;
    return 1;
}

// ====== x86_timer_finish ======

int x86_timer_finish(void)
{
    require(prepared == 1 && !closed && retained->Context == &boot);
    require(processes == ready && sessions == ready);
    ++closed;
    return failure != 4;
}

// ====== reset ======

static void reset(unsigned injected_failure, unsigned timer_ready)
{
    prepared = 0;
    processes = 0;
    sessions = 0;
    closed = 0;
    retained = NULL;
    failure = injected_failure;
    ready = timer_ready;
}

// ====== prepare ======

static int prepare(X86_WATCHDOG_KERNEL_RUNTIME *runtime)
{
    return x86_watchdog_kernel_prepare(runtime,&boot,&window,&arena,&acpi,&madt,
        &inventory,&profile,&caps,&description);
}

// ====== main ======

int main(void)
{
    X86_WATCHDOG_KERNEL_RUNTIME runtime;
    reset(0,1);
    require(prepare(&runtime));
    require(prepared == 1 && !processes && !sessions && !closed);
    require(retained == &runtime.Environment && runtime.Directory == &directory);
    x86_watchdog_kernel_dispatch(&runtime);
    require(processes == 1 && sessions == 1 && !closed);
    require(x86_timer_finish());

    for (unsigned injected = 1; injected <= 3; ++injected) {
        reset(injected,0);
        require(!prepare(&runtime));
        require(prepared == (unsigned)(injected == 3) && !processes && !sessions && !closed);
    }

    for (unsigned timer_ready = 0; timer_ready <= 1; ++timer_ready) {
        for (unsigned injected = 0; injected <= 4; injected += 4) {
            reset(injected,timer_ready);
            require(x86_watchdog_kernel_start(&boot,&window,&arena,&acpi,&madt,
                &inventory,&profile,&caps,&description) == (injected == 0));
            require(closed == 1 && processes == timer_ready && sessions == timer_ready);
        }
    }
    puts("WATCHDOG_INTEGRATION_PASS: 8 scenarios");
    return 0;
}
