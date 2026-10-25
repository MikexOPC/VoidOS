#include "watchdog.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/resources.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
static X86_HPET_WATCHDOG watchdog;
static BOOT_U32 first_failure, first_cleanup_failure;
static BOOT_U32 recovery_attempts;
#ifdef KERNEL_TEST_WATCHDOG_CLEANUP_RECOVERY
static BOOT_U32 test_recovery_injected, test_recovery_called;
#endif

// ====== x86_watchdog_recovery_attempts ======

BOOT_U32 x86_watchdog_recovery_attempts(void) { return recovery_attempts; }

// ====== x86_watchdog_recovery_claim ======

int x86_watchdog_recovery_claim(void)
{
    if (recovery_attempts >= X86_WATCHDOG_RECOVERY_LIMIT) { return 0; }
    ++recovery_attempts;
    return 1;
}

// ====== x86_watchdog_failure ======

BOOT_U32 x86_watchdog_failure(void) { return first_failure; }

// ====== x86_watchdog_cleanup_failure ======

BOOT_U32 x86_watchdog_cleanup_failure(void) { return first_cleanup_failure; }

// ====== x86_watchdog_fail ======

/* Metadata only, on the existing BSP/IF=0 contract. IRQ path never logs.
 * Not synchronization for SMP, and does not release or transfer resources. */
int x86_watchdog_fail(BOOT_U32 phase)
{
    if (phase && phase <= X86_WATCHDOG_LOG && !first_failure) { first_failure = phase; }
    return 0;
}

// ====== phase_name ======

static const char *phase_name(BOOT_U32 phase)
{
    static const char *const names[] = {
        "NONE", "ENVIRONMENT", "PROFILE", "CAPTURE", "ROUTE", "SELECTION",
        "CONFIGURE", "BIND", "UNMASK", "OUTPUT", "WAIT_STATE", "SOURCE", "ARM",
        "IRQ", "WAKE", "CLASSIFY", "DEADLINE", "CLOSE_STATE", "MASK", "UNBIND",
        "RESTORE_DEVICE", "RESTORE_ROUTE", "VERIFY", "LOG"
    };
    _Static_assert(sizeof(names)/sizeof(names[0]) == X86_WATCHDOG_LOG+1, "Diagnostic phase table");
    return phase <= X86_WATCHDOG_LOG ? names[phase] : "INVALID";
}

// ====== report_failure ======

static int report_failure(void)
{
    return kernel_text("KERNEL_WATCHDOG_FAILURE: ") && kernel_number(first_failure) &&
        kernel_text("\nKERNEL_WATCHDOG_CLEANUP_FAILURE: ") && kernel_number(first_cleanup_failure) &&
        kernel_text("\nKERNEL_WATCHDOG_FAILURE_PHASE: ") && kernel_text(phase_name(first_failure)) &&
        kernel_text("\nKERNEL_WATCHDOG_CLEANUP_PHASE: ") && kernel_text(phase_name(first_cleanup_failure)) &&
        kernel_text("\nKERNEL_WATCHDOG_RECOVERY_STATE: ") && kernel_number(x86_watchdog_recovery_status()) &&
        kernel_text("\nKERNEL_WATCHDOG_RECOVERY_ATTEMPTS: ") && kernel_number(recovery_attempts) &&
        kernel_text("\n");
}

// ====== cleanup_failure ======

static int cleanup_failure(BOOT_U32 phase)
{
    if (!first_cleanup_failure) { first_cleanup_failure = phase; }
    report_failure();
    return 0;
}

// ====== reject_environment ======

static int reject_environment(BOOT_U32 phase)
{
    x86_watchdog_fail(phase);
    report_failure();
    return 0;
}

// ====== x86_watchdog_state ======

X86_HPET_WATCHDOG *x86_watchdog_state(void) { return &watchdog; }

// ====== x86_watchdog_valid ======

int x86_watchdog_valid(void)
{
    X86_APIC_RAW cpu = {0};
    return watchdog.Self == (BOOT_U64)&watchdog && !watchdog.Closed &&
        watchdog.Env.Window && watchdog.Env.Window->Pool && watchdog.Env.Mapping &&
        watchdog.Env.Profile && watchdog.Env.Window->Root == watchdog.Env.Mapping->Root &&
        x86_watchdog_budget_valid() &&
        x86_irq_ready() && x86_lapic_validate(watchdog.Env.Mapping) &&
        x86_apic_cache_state() && x86_apic_read_profile(&cpu) && !cpu.Reserved &&
        (cpu.Rflags & 0x202) == 2 && cpu.Pat == watchdog.Env.Profile->Raw.Pat &&
        cpu.ApicBase == watchdog.Env.Profile->Raw.ApicBase &&
        cpu.PhysicalBits == watchdog.Env.Window->Pool->PhysicalBits;
}

// ====== quiet ======

static int quiet(void)
{
    return x86_watchdog_valid() && !watchdog.Armed &&
        x86_watchdog_device(5) && x86_lapic_idle(watchdog.Env.Mapping);
}

// ====== handle ======

static int handle(void)
{
    if (!watchdog.Ready || !watchdog.Armed || watchdog.Error || watchdog.Count == ~0U ||
        !x86_watchdog_device(3) || !x86_lapic_eoi_vector(watchdog.Env.Mapping,0x40)) {
        watchdog.Error = 1;
        return x86_watchdog_fail(X86_WATCHDOG_IRQ);
    }
    if (!watchdog.Armed) ++watchdog.Count;
    return 1;
}

// ====== trusted_range ======

static int trusted_range(const void *pointer, BOOT_U64 bytes)
{
    BOOT_U64 address = (BOOT_U64)pointer;
    return address >= 4096 && !(address & 7) && address <= (1ULL << 47) - bytes &&
        !os_address_ranges_overlap(address,bytes,(BOOT_U64)&watchdog,sizeof(watchdog)) &&
        !os_address_ranges_overlap(address,bytes,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(address,bytes,X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(address,bytes,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES);
}

// ====== announce ======

static int announce(void)
{
    return kernel_section("watchdog HPET indipendente su IOAPIC") &&
        kernel_text("KERNEL_WATCHDOG_OWNER: ") && kernel_number((BOOT_U64)&watchdog) &&
        kernel_text("\nKERNEL_WATCHDOG_PHYSICAL: ") && kernel_number(watchdog.Physical) &&
        kernel_text("\nKERNEL_WATCHDOG_TIMER: ") && kernel_number(watchdog.Timer) &&
        kernel_text("\nKERNEL_WATCHDOG_PIN: ") && kernel_number(watchdog.Pin) &&
        kernel_text("\nKERNEL_WATCHDOG_TICKS: ") && kernel_number(watchdog.Ticks) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_WATCHDOG_READY");
}

// ====== x86_watchdog_open ======

int x86_watchdog_open(const X86_WATCHDOG_ENV *env)
{
    BOOT_U32 index, phase;
    const X86_IO_APIC_CAPACITY *io;
    if (watchdog.Self) { return 0; }
    first_failure = first_cleanup_failure = 0;
    recovery_attempts = 0;
#ifdef KERNEL_TEST_WATCHDOG_CLEANUP_RECOVERY
    test_recovery_injected = test_recovery_called = 0;
#endif
    if (!trusted_range(env,sizeof(*env)) ||
        !trusted_range(env->Context,sizeof(*env->Context)) ||
        !trusted_range(env->Window,sizeof(*env->Window)) ||
        !trusted_range(env->Inventory,sizeof(*env->Inventory)) ||
        !trusted_range(env->Mapping,sizeof(*env->Mapping)) ||
        !trusted_range(env->Topology,sizeof(*env->Topology)) ||
        !trusted_range(env->Profile,sizeof(*env->Profile)) ||
        !trusted_range(env->Capabilities,sizeof(*env->Capabilities)) ||
        !trusted_range(env->Description,sizeof(*env->Description)) || !env->Window->Pool ||
        !x86_irq_ready() || !x86_page_window_idle_valid(env->Window) ||
        !x86_apic_profile_valid(env->Topology,env->Profile,env->Window->Pool->PhysicalBits) ||
        env->Capabilities->Ready != 1 || env->Capabilities->IoCount != 1 ||
        env->Topology->IoCount != 1 || env->Capabilities->LapicId != env->Profile->InitialId ||
        env->Profile->InitialId > 15 || env->Description->Physical < 4096 ||
        !x86_hpet_description_fields(env->Description->BlockId,0,env->Description->Physical,
            env->Window->Pool->PhysicalBits,env->Description->PageAttributes) ||
        !x86_hpet_owned_preflight(env->Context,env->Window->Pool,env->Inventory,
            env->Description->Physical,env->Mapping)) { return reject_environment(X86_WATCHDOG_ENVIRONMENT); }
    io = &env->Capabilities->Io[0];
    if (io->Address != env->Topology->Io[0].Address || io->Base ||
        env->Topology->Io[0].GsiBase || io->Id != env->Topology->Io[0].Id || io->Id > 15 ||
        io->Inputs != 24 || x86_apic_io_inputs(io->Version) != io->Inputs || io->End != 24 ||
        !x86_hpet_owned_preflight(env->Context,env->Window->Pool,env->Inventory,
            io->Address,env->Mapping)) { return reject_environment(X86_WATCHDOG_PROFILE); }
    for (index = 0; index < sizeof(watchdog); ++index) { ((volatile unsigned char *)&watchdog)[index] = 0; }
    watchdog.Env.Context = env->Context; watchdog.Env.Window = env->Window;
    watchdog.Env.Inventory = env->Inventory; watchdog.Env.Mapping = env->Mapping;
    watchdog.Env.Topology = env->Topology; watchdog.Env.Profile = env->Profile;
    watchdog.Env.Capabilities = env->Capabilities; watchdog.Env.Description = env->Description;
    watchdog.Self = (BOOT_U64)&watchdog; watchdog.Physical = env->Description->Physical;
    watchdog.Id = env->Description->BlockId; watchdog.Cache = env->Profile->CacheBits;
    watchdog.Budget = env->Window->Pool->FreePages;
    watchdog.IoAddress = io->Address; watchdog.IoId = io->Id; watchdog.IoVersion = io->Version;
    phase = X86_WATCHDOG_CAPTURE;
    if (!x86_watchdog_device(0)) { goto failed; }
    /* Each advance increases Timer; at most NUM_TIM_CAP+1 <=32 candidates.
     * No fallback after a failed transaction or partial route acquisition. */
    for (;;) {
        phase = X86_WATCHDOG_ROUTE;
        if (!x86_watchdog_route(0)) { goto failed; }
        if (watchdog.RouteOwned) { break; }
        phase = X86_WATCHDOG_SELECTION;
        if (!x86_watchdog_device(6)) { goto failed; }
    }
    phase = X86_WATCHDOG_CONFIGURE;
    if (!x86_watchdog_device(1)) { goto failed; }
    phase = X86_WATCHDOG_BIND;
    if (!x86_irq_registry_bind(x86_irq_registry(),0x40,handle,quiet,&watchdog.Token)) { goto failed; }
    watchdog.Ready = 1;
    phase = X86_WATCHDOG_UNMASK;
    if (!x86_watchdog_route(1)) { goto failed; }
    phase = X86_WATCHDOG_OUTPUT;
    if (!announce()) { goto failed; }
    return 1;
failed:
    x86_watchdog_fail(phase);
    watchdog.Result = X86_WATCHDOG_FAULT;
    x86_watchdog_close();
    return 0;
}

// ====== x86_watchdog_result ======

BOOT_U32 x86_watchdog_result(void) { return watchdog.Result; }

// ====== x86_watchdog_finish_cleanup ======

int x86_watchdog_finish_cleanup(void)
{
    if (!watchdog.Self) { return 1; }
    if (watchdog.Closed) { return 1; }
    if (x86_watchdog_budget_active()) { return cleanup_failure(X86_WATCHDOG_CLOSE_STATE); }
    if (!x86_watchdog_valid() || watchdog.Armed || watchdog.Error) {
        return cleanup_failure(X86_WATCHDOG_CLOSE_STATE);
    }
    /* After confirmed HPET restore, a partial IOAPIC restore may already
     * contain the original masked entry. Do not require owned routing again. */
    if (watchdog.RouteOwned && watchdog.Ready && watchdog.Acquired && !x86_watchdog_route(2)) {
        return cleanup_failure(X86_WATCHDOG_MASK);
    }
    if (watchdog.Token.Id && !x86_irq_registry_unbind(x86_irq_registry(),&watchdog.Token)) {
        return cleanup_failure(X86_WATCHDOG_UNBIND);
    }
#ifdef KERNEL_TEST_WATCHDOG_CLEANUP_RECOVERY
    /* Software injection before HPET restore, never a fabricated MMIO value. */
    if (watchdog.Acquired && !test_recovery_injected) {
        test_recovery_injected = 1;
        return cleanup_failure(X86_WATCHDOG_RESTORE_DEVICE);
    }
#endif
    if (watchdog.Acquired && !x86_watchdog_device(4)) {
        return cleanup_failure(X86_WATCHDOG_RESTORE_DEVICE);
    }
    if (watchdog.RouteOwned && !x86_watchdog_route(3)) {
        return cleanup_failure(X86_WATCHDOG_RESTORE_ROUTE);
    }
    if (!x86_page_window_idle_valid(watchdog.Env.Window) ||
        !x86_device_inventory_verify(watchdog.Env.Context,watchdog.Env.Window->Pool,
            watchdog.Env.Mapping,watchdog.Env.Inventory) || !x86_lapic_idle(watchdog.Env.Mapping)) {
        return cleanup_failure(X86_WATCHDOG_VERIFY);
    }
    watchdog.Ready = 0; watchdog.Closed = 1;
    if (!report_failure() || !kernel_text("KERNEL_WATCHDOG_FIRED: ") || !kernel_number(watchdog.Count) ||
        !kernel_text("\nKERNEL_WATCHDOG_RESULT: ") || !kernel_number(watchdog.Result) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_WATCHDOG_CLEANUP_OK")) {
        return cleanup_failure(X86_WATCHDOG_LOG);
    }
    return 1;
}

// ====== x86_watchdog_close ======

int x86_watchdog_close(void)
{
    int result;
    if (!watchdog.Closed && first_cleanup_failure) {
        return x86_watchdog_recover() == X86_WATCHDOG_RELEASED;
    }
    result = x86_watchdog_finish_cleanup();
#ifdef KERNEL_TEST_WATCHDOG_CLEANUP_RECOVERY
    if (!result && test_recovery_injected && !test_recovery_called) {
        test_recovery_called = 1;
        return x86_watchdog_recover() == X86_WATCHDOG_RELEASED;
    }
#endif
    return result;
}
