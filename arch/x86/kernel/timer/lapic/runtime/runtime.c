#include "runtime.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#ifdef KERNEL_SCHEDULER_IRQ
#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "arch/x86/kernel/scheduler/idle/idle.h"
#endif
static X86_TIMER_LOOP_CONTROL control;

// ====== scrittura readback prima di considerare un registro configurato ===== //

int x86_timer_confirm_write(const X86_LAPIC_MAPPING *mapping, BOOT_U32 offset, BOOT_U32 value)
{
    BOOT_U32 actual;
    return x86_timer_write(mapping,offset,value) && x86_timer_read(mapping,offset,&actual) && actual == value;
}

// ====== sorgente fermata prima di verificare quiescenza o rimuovere l'handler ===== //

int x86_timer_stop(const X86_LAPIC_MAPPING *mapping)
{
    BOOT_U32 current;
    // Tentare entrambe le scritture anche se la prima non viene confermata.
    int masked = x86_timer_confirm_write(mapping,0x320,0x10041);
    int stopped = x86_timer_confirm_write(mapping,0x380,0);
    return masked && stopped && x86_timer_read(mapping,0x390,&current) && !current;
}

// ====== source mascherata ferma e senza IRQ prima di una transizione ===== //

static int masked_idle(const X86_LAPIC_TIMER *state)
{
    BOOT_U32 value;
    return x86_timer_read(state->Mapping,0x320,&value) && value == 0x10041 &&
        x86_timer_read(state->Mapping,0x380,&value) && !value &&
        x86_timer_read(state->Mapping,0x390,&value) && !value &&
        x86_timer_read(state->Mapping,0x3e0,&value) && value == 3 && x86_lapic_idle(state->Mapping);
}

// ====== calibrazione completa prima di rendere riusabile la source ===== //

int x86_timer_events_initialize(X86_LAPIC_TIMER *state)
{
    if (!x86_timer_owner_valid(state) || !x86_irq_ready() || state->Phase || state->Error ||
        !state->Reload || state->Token.Id || state->Token.Vector || state->Token.Reserved || !masked_idle(state)) {
        return 0;
    }
    state->Phase = X86_TIMER_READY;
    return 1;
}

// ====== nessun riarmo sopra una consegna non consumata ===== //

int x86_timer_arm(X86_LAPIC_TIMER *state)
{
    if (!x86_timer_owner_valid(state) || !x86_irq_ready() || state->Error || !state->Reload ||
        state->Phase != X86_TIMER_READY || state->Count == ~0U || !state->Token.Id ||
        state->Token.Vector != 0x41 || state->Token.Reserved ||
        !x86_irq_registry_token_live(x86_irq_registry(),&state->Token) || !masked_idle(state)) {
        return 0;
    }
    state->Active = 1;
    state->Phase = X86_TIMER_ARMED;
    // ACPI 6.5 4.8.3.3: read-only PM_TMR, dword; low 24/32 bits selected by FADT.
    state->ArmedPm = x86_pm_read((unsigned short)state->Clock.Port) & state->Clock.Mask;
    if (!x86_timer_confirm_write(state->Mapping,0x320,0x41) || !x86_timer_confirm_write(state->Mapping,0x380,state->Reload)) {
        if (x86_timer_stop(state->Mapping)) {
            state->Active = 0;
            state->Phase = X86_TIMER_READY;
            state->ArmedPm = 0;
        }
        state->Error = 3;
        return 0;
    }
    return 1;
}

// ====== consegna una volta sola a output disgiunto senza conservare prestiti ===== //

int x86_timer_take(X86_LAPIC_TIMER *state, BOOT_U32 *event)
{
    BOOT_U64 address = (BOOT_U64)event;
    if (!event || (address & 3) || address > ~0ULL-sizeof(*event) ||
        os_address_ranges_overlap(address,sizeof(*event),(BOOT_U64)state,sizeof(*state)) ||
        os_address_ranges_overlap(address,sizeof(*event),X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(address,sizeof(*event),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap(address,sizeof(*event),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
        !x86_timer_owner_valid(state) || !x86_irq_ready() || state->Error ||
        state->Phase != X86_TIMER_PENDING || !masked_idle(state) ||
        os_address_ranges_overlap(address,sizeof(*event),(BOOT_U64)state->Mapping,sizeof(*state->Mapping))) {
        return 0;
    }
    *event = state->Count;
    state->Consumed = state->Count;
    state->Active = 0;
    state->Phase = X86_TIMER_READY;
    return 1;
}

// ====== chiusura solo dopo consumo e rimozione della subscription ===== //

int x86_timer_events_close(X86_LAPIC_TIMER *state)
{
    if (!x86_timer_owner_valid(state) || !x86_irq_ready() || state->Error ||
        state->Phase != X86_TIMER_READY || state->Token.Id || state->Token.Vector ||
        state->Token.Reserved || !masked_idle(state)) {
        return 0;
    }
    state->Phase = X86_TIMER_CLOSED;
    return 1;
}

// ====== controllo statico mai trasferibile tramite copia ===== //

static int control_valid(const X86_LAPIC_TIMER *state)
{
    return control.Self == (BOOT_U64)&control && control.Owner == (BOOT_U64)state &&
        !control.Reserved && control.Started == 1 && control.Closed <= 1 &&
        control.Phase >= 1 && control.Phase <= 2 && control.StopRequest <= 1 &&
        control.Consumed == state->Consumed && state->Consumed == state->Count;
}

// ====== richiesta kernel fidata con IF zero e nessuna scrittura ai dispositivi ===== //

int x86_timer_loop_request_stop(void)
{
    const X86_LAPIC_TIMER *state;
    if (!x86_irq_ready() || !control.Owner || control.Phase != 1 || control.Closed) return 0;
    state = (const X86_LAPIC_TIMER *)control.Owner;
    if (!x86_timer_owner_valid(state) || !control_valid(state)) return 0;
    control.StopRequest = 1;
    return 1;
}

// ====== indirizzi diagnostici prima del primo sonno ===== //

static int announce(const X86_LAPIC_TIMER *state)
{
    return kernel_section("ciclo kernel con timer attivo") &&
        kernel_text("KERNEL_TIMER_LOOP_CONTROL: ") && kernel_number((BOOT_U64)&control) &&
        kernel_text("\nKERNEL_TIMER_LOOP_OWNER: ") && kernel_number((BOOT_U64)state) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_TIMER_LOOP_STARTED");
}

// ====== una scadenza consumata fuori dal gestore prima del riarmo successivo ===== //

static int step(X86_LAPIC_TIMER *state)
{
    BOOT_U32 goal, waited, event, start = 0;
    if (!x86_irq_ready() || !x86_timer_owner_valid(state) || !control_valid(state) ||
        state->Phase != X86_TIMER_READY || state->Count == ~0U || control.Sleeps == ~0U) return 0;
    goal = state->Count+1;
#if defined(KERNEL_SCHEDULER_IRQ) && !defined(KERNEL_TEST_WATCHDOG_LOST_LAPIC)
    if (goal == 1) {
        if (!x86_scheduler_timer_tasks(state)) return 0;
    } else if (goal == 2) {
        if (!x86_scheduler_timer_idle_tasks(state)) return 0;
    } else if (goal == 3) {
        if (!x86_scheduler_timer_idle_repeat(state)) return 0;
    } else
#endif
    if (!x86_timer_arm(state)) return 0;
#ifdef KERNEL_TEST_WATCHDOG_LOST_LAPIC
    if (!x86_timer_confirm_write(state->Mapping,0x320,0x10041)) { return 0; }
#endif
#if defined(KERNEL_SCHEDULER_IRQ) && !defined(KERNEL_TEST_WATCHDOG_LOST_LAPIC)
    if (goal == 1) start = x86_pm_read((unsigned short)state->Clock.Port) & state->Clock.Mask;
    waited = (goal == 2 || goal == 3) ? x86_scheduler_idle_slept() : x86_watchdog_wait(&state->Count,goal,&control.StopRequest);
#else
    if (goal <= 3) start = x86_pm_read((unsigned short)state->Clock.Port) & state->Clock.Mask;
    waited = x86_watchdog_wait(&state->Count,goal,&control.StopRequest);
#endif
#if defined(KERNEL_SCHEDULER_IRQ) && !defined(KERNEL_TEST_WATCHDOG_LOST_LAPIC)
    if (goal == 1)
#else
    if (goal <= 3)
#endif
        state->WaitPm[goal-1] = x86_pm_elapsed(start,
            x86_pm_read((unsigned short)state->Clock.Port) & state->Clock.Mask,state->Clock.Mask);
    if (x86_watchdog_result() == X86_WATCHDOG_STOP && state->Count == state->Consumed) {
        if (!x86_timer_stop(state->Mapping)) { return 0; }
        state->Active = 0; state->Phase = X86_TIMER_READY; state->ArmedPm = 0;
        return 2;
    }
    if (!waited || waited > X86_WATCHDOG_WAKE_LIMIT || state->Count != goal || state->Error ||
        !x86_timer_take(state,&event) || event != goal) return 0;
    if (waited-1 > ~0U-control.Sleeps) return 0;
    control.Sleeps += waited-1;
    control.Consumed = event;
    return control_valid(state) && x86_irq_ready();
}

// ====== avanzamento osservabile senza stampa nel gestore IRQ ===== //

static int progress(const X86_LAPIC_TIMER *state)
{
    if (state->Count & 7) return 1;
    return kernel_text("KERNEL_TIMER_LOOP_EVENTS: ") && kernel_number(state->Count) && kernel_text("\n");
}

// ====== arresto distinto da errore e sempre a evento gia consumato ===== //

static int finish(const X86_LAPIC_TIMER *state, BOOT_U32 reason)
{
    if (!control_valid(state) || !x86_irq_ready() || state->Phase != X86_TIMER_READY ||
        (reason != 1 && reason != 2)) return 0;
    control.Reason = reason;
    control.Phase = 2;
    return kernel_log(1,"KERNEL_TIMER_LOOP_STOPPING");
}

// ====== durata del callback pari al kernel e subscription conservata tra gli eventi ===== //

int x86_timer_loop_run(X86_LAPIC_TIMER *state, X86_IRQ_CALLBACK handle, X86_IRQ_CALLBACK quiet)
{
    BOOT_U32 action;
    int advanced;
    if (!x86_timer_owner_valid(state) || !x86_irq_ready() || state->Phase != X86_TIMER_READY ||
        state->Count || state->Consumed || state->Error || state->Token.Id || state->Token.Vector ||
        state->Token.Reserved || control.Self || control.Owner || control.StopRequest || control.Phase ||
        control.Consumed || control.Sleeps || control.Reason || control.Reserved || control.Started ||
        control.Closed || !x86_irq_registry_bind(x86_irq_registry(),0x41,handle,quiet,&state->Token)) return 0;
    control.Self = (BOOT_U64)&control;
    control.Owner = (BOOT_U64)state;
    control.Started = 1;
    control.Phase = 1;
    if (!announce(state)) return 0;
    for (;;) {
        if (!x86_timer_owner_valid(state) || !x86_irq_ready() || !control_valid(state)) return 0;
        action = x86_timer_loop_action(control.StopRequest,state->Count,state->Consumed);
        if (!action) return 0;
        if (action == 2) return finish(state,control.StopRequest ? 1 : 2);
        advanced = step(state);
        if (advanced == 2) { return finish(state,1); }
        if (!advanced || !progress(state)) return 0;
    }
}

// ====== conferma del cleanup reale dopo l'uscita dal ciclo ===== //

int x86_timer_loop_closed(const X86_LAPIC_TIMER *state)
{
    if (!x86_timer_owner_valid(state) || !control_valid(state) || !x86_irq_ready() ||
        control.Phase != 2 || control.Closed || !control.Reason || control.Reason > 2 ||
        state->Phase != X86_TIMER_CLOSED || state->Token.Id || state->Token.Vector || state->Token.Reserved ||
        state->Error || !x86_lapic_idle(state->Mapping)) return 0;
    control.Closed = 1;
    return kernel_log(0,"KERNEL_TIMER_LOOP_CLOSED");
}
