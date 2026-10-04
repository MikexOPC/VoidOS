#include "timer.h"
#include "runtime.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
static X86_LAPIC_TIMER timer;
static BOOT_U32 finished;
#ifdef KERNEL_EVENT_LOOP
static const BOOT_U32 loop_mode = 1;
#else
static const BOOT_U32 loop_mode = 0;
#endif

// ====== proprieta live e profilo oneshot prima di gestire una scadenza ===== //

int x86_timer_service(X86_LAPIC_TIMER *state)
{
    BOOT_U32 value;
    if (!x86_timer_owner_valid(state) || !x86_exception_environment() || state->Active != 1 || state->Error ||
        !state->Reload || state->Phase != X86_TIMER_ARMED || !x86_lapic_validate(state->Mapping)) {
        return 0;
    }
    if (!x86_timer_read(state->Mapping,0x320,&value) || value != 0x41 ||
        !x86_timer_read(state->Mapping,0x3e0,&value) || value != 3 ||
        !x86_timer_read(state->Mapping,0x380,&value) || value != state->Reload ||
        !x86_timer_read(state->Mapping,0x390,&value) || value ||
        !x86_timer_stop(state->Mapping)) {
        x86_timer_stop(state->Mapping);
        state->Error = 1;
        return 0;
    }
    if (!x86_lapic_eoi_vector(state->Mapping,0x41)) {
        state->Error = 2;
        return 0;
    }
    ++state->Count;
    state->Phase = X86_TIMER_PENDING;
    return 1;
}

// ====== callback statico con durata kernel e nessun log durante IRQ ===== //

static int handle(void)
{
    return x86_timer_service(&timer);
}

// ====== quiescenza include source stopped ISR e IRR vuoti ===== //

static int quiet(void)
{
    BOOT_U32 value;
    return !timer.Active && x86_timer_read(timer.Mapping,0x320,&value) && value == 0x10041 &&
        x86_timer_read(timer.Mapping,0x380,&value) && !value &&
        x86_timer_read(timer.Mapping,0x390,&value) && !value && x86_lapic_idle(timer.Mapping);
}

// ====== letture firmware solo attraverso prestito ACPI gia validato ===== //

typedef struct { const KERNEL_BOOT_CONTEXT *Context; X86_PAGE_WINDOW *Window; } READER;
static int read_firmware(void *context, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    READER *reader = (READER *)context;
    if (!reader || !reader->Context || !reader->Window) {
        return 0;
    }
    return x86_page_window_read_acpi(reader->Window,reader->Context,physical,bytes,output);
}

// ====== fotografie dei due campioni e contatore dopo EOI e distacco ===== //

static int report(BOOT_U64 deliveries)
{
    return kernel_section("timer LAPIC calibrato su reference ACPI") &&
        kernel_text("KERNEL_TIMER_OWNER: ") && kernel_number((BOOT_U64)&timer) &&
        kernel_text("\nKERNEL_TIMER_PM_PORT: ") && kernel_number(timer.Clock.Port) &&
        kernel_text("\nKERNEL_TIMER_PM_MASK: ") && kernel_number(timer.Clock.Mask) &&
        kernel_text("\nKERNEL_TIMER_PM_HZ: ") && kernel_number(X86_PM_CLOCK_HZ) &&
        kernel_text("\nKERNEL_TIMER_RELOAD: ") && kernel_number(timer.Reload) &&
        kernel_text("\nKERNEL_TIMER_IRQ_COUNT: ") && kernel_number(timer.Count) &&
        kernel_text("\nKERNEL_TIMER_ERROR: ") && kernel_number(timer.Error) &&
        kernel_text("\nKERNEL_TIMER_DELIVERED: ") && kernel_number(deliveries) &&
        kernel_text("\nKERNEL_TIMER_SLEEP_RESUME: ") && kernel_number(x86_irq_sleep_resume()) &&
        kernel_text("\nKERNEL_TIMER_PENDING_PROBE: ") && kernel_number(timer.PendingProbe) &&
        kernel_text("\nKERNEL_TIMER_CONSUMED: ") && kernel_number(timer.Consumed) &&
        kernel_text("\nKERNEL_TIMER_PHASE: ") && kernel_number(timer.Phase) &&
        kernel_text("\nKERNEL_TIMER_LOOP_MODE: ") && kernel_number(loop_mode) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_IRQ_SLEEP_REJECT_OK") &&
        kernel_log(0,"KERNEL_TIMER_EVENTS_OK") && kernel_log(0,"KERNEL_TIMER_OK");
}

// ====== elapsed PM della singola scadenza senza confonderlo con un timestamp globale ===== //

static int record_deadline(BOOT_U32 goal, BOOT_U32 start)
{
    BOOT_U32 elapsed;
    if (!goal || goal > 3) {
        return 0;
    }
    elapsed = x86_pm_elapsed(start,
        x86_pm_read((unsigned short)timer.Clock.Port) & timer.Clock.Mask,timer.Clock.Mask);
    // Intervallo ampio per diagnostica: non definisce ancora garanzie scheduler.
    if (elapsed < X86_PM_SAMPLE_TICKS/2 || elapsed > X86_PM_SAMPLE_TICKS*20) {
        return 0;
    }
    timer.DeadlinePm[goal-1] = elapsed;
    return 1;
}

// ====== tre scadenze singole senza riarmo dentro il gestore ===== //

static int deadlines(void)
{
    BOOT_U32 goal, start, waited, event;
    if (!x86_irq_registry_bind(x86_irq_registry(),0x41,handle,quiet,&timer.Token)) {
        return 0;
    }
    for (goal = 1; goal <= 3; ++goal) {
        if (!x86_irq_ready() || !quiet()) {
            return 0;
        }
        start = x86_pm_read((unsigned short)timer.Clock.Port) & timer.Clock.Mask;
        if (!x86_timer_arm(&timer)) {
            return 0;
        }
        waited = x86_irq_sleep(&timer.Count,goal,8);
        if (!x86_timer_wait_result(waited,8,timer.Count,timer.Error,goal) ||
            timer.Count != goal || timer.Error || !x86_lapic_idle(timer.Mapping)) {
            return 0;
        }
        if (x86_timer_arm(&timer) || !x86_timer_take(&timer,&event) ||
            event != goal || x86_timer_take(&timer,&event)) {
            return 0;
        }
        timer.SleepWakes[goal-1] = waited-1;
        // Una consegna gia osservata deve ritornare subito senza HLT aggiuntivi.
        if (x86_irq_sleep(&timer.Count,goal,8) != 1) {
            return 0;
        }
        ++timer.PendingProbe;
        if (!record_deadline(goal,start)) {
            return 0;
        }
    }
    return 1;
}

// ====== configurazione acquisita solo se il timer precedente e inattivo e mascherato ===== //

static int acquire(void)
{
    BOOT_U32 value;
    return x86_lapic_authority(timer.Mapping) && x86_lapic_idle(timer.Mapping) &&
        x86_timer_read(timer.Mapping,0x30,&value) && x86_apic_lapic_version(value) &&
        x86_timer_read(timer.Mapping,0x320,&timer.OriginalLvt) &&
        x86_timer_read(timer.Mapping,0x3e0,&timer.OriginalDivide) &&
        // Initial Count conserva il reload anche dopo una scadenza oneshot.
        // Solo Current Count zero prova che non c'e un conto alla rovescia live.
        x86_timer_read(timer.Mapping,0x390,&value) &&
        x86_timer_previous_valid(timer.OriginalLvt,timer.OriginalDivide,value);
}

// ====== prove ring zero di precondizioni rifiutate senza HLT o accessi invalidi ===== //

static int sleep_rejections(void)
{
    __declspec(align(8)) volatile BOOT_U32 source[2] = {3,1};
    if (x86_irq_sleep(source,3,8) || x86_irq_sleep(0,3,8) ||
        x86_irq_sleep((volatile BOOT_U32 *)1,3,8) ||
        x86_irq_sleep(source,0,8) || x86_irq_sleep(source,3,0) ||
        x86_irq_sleep(source,3,1025) || source[0] != 3 || source[1] != 1) {
        return 0;
    }
    return x86_exception_environment() != 0;
}

// ====== calibrazione masked poi handler poi attesa e cleanup senza perdere owner live ===== //

int x86_timer_run(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const KERNEL_ACPI_DIRECTORY *directory, const X86_LAPIC_MAPPING *mapping,
    const X86_PAGING_INVENTORY *inventory)
{
    READER reader = {context,window};
    BOOT_U64 delivered = 0;
    int result = 0, stopped, detached = 1, restored = 0;
    if (finished || !context || !window || !window->Pool || !mapping || !inventory || !x86_irq_ready() ||
        !x86_paging_inventory_verify_lapic(context,window->Pool,mapping,inventory) ||
        !x86_pm_clock_capture(directory,window->Pool->PhysicalBits,read_firmware,&reader,&timer.Clock)) {
        return 0;
    }
    kernel_log(1,"TIMER_REFERENCE_CAPTURED");
    timer.Mapping = mapping;
    if (!acquire()) {
        kernel_log(3,"TIMER_ACQUIRE_FAILED");
        return 0;
    }
    kernel_log(1,"TIMER_ACQUIRED");
    if (!x86_timer_stop(mapping) || !x86_timer_confirm_write(mapping,0x3e0,3) ||
        !x86_timer_measure(&timer)) {
        kernel_log(3,"TIMER_CALIBRATION_FAILED");
        goto cleanup;
    }
    kernel_log(1,"TIMER_CALIBRATED");
    if (!x86_timer_events_initialize(&timer) ||
        !(loop_mode ? x86_timer_loop_run(&timer,handle,quiet) : deadlines()) || !sleep_rejections()) {
        kernel_log(3,"TIMER_DEADLINES_FAILED");
        goto cleanup;
    }
    result = 1;
cleanup:
    stopped = x86_timer_stop(mapping);
    timer.Active = 0;
    if (timer.Token.Id) {
        delivered = x86_irq_registry_deliveries(x86_irq_registry(),&timer.Token);
        detached = stopped && x86_irq_registry_unbind(x86_irq_registry(),&timer.Token);
    }
    if (stopped && detached && x86_timer_events_close(&timer) && x86_lapic_idle(mapping)) {
        int divider = x86_timer_confirm_write(mapping,0x3e0,timer.OriginalDivide);
        int lvt = x86_timer_confirm_write(mapping,0x320,timer.OriginalLvt);
        restored = divider && lvt;
    }
    finished = 1;
    if (!result || !stopped || !detached || !restored || delivered != timer.Count ||
        (!loop_mode && timer.Count != 3) || timer.Error ||
        !x86_irq_ready() || !x86_lapic_idle(mapping) || !x86_page_window_idle_valid(window) ||
        !x86_paging_inventory_verify_lapic(context,window->Pool,mapping,inventory)) {
        return 0;
    }
    return (!loop_mode || x86_timer_loop_closed(&timer)) && report(delivered);
}
