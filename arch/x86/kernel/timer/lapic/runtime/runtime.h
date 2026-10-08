#ifndef OS_X86_LAPIC_RUNTIME_H
#define OS_X86_LAPIC_RUNTIME_H
#include "../timer.h"
typedef struct {
    BOOT_U64 Self, Owner;
    volatile BOOT_U32 StopRequest;
    BOOT_U32 Phase, Consumed, Sleeps, Reason, Reserved, Started, Closed;
} X86_TIMER_LOOP_CONTROL;
_Static_assert(sizeof(X86_TIMER_LOOP_CONTROL) == 48, "Controllo ciclo timer");
_Static_assert(offsetof(X86_TIMER_LOOP_CONTROL, StopRequest) == 16, "Richiesta stop debug");
BOOT_U32 x86_timer_loop_action(BOOT_U32 request, BOOT_U32 count, BOOT_U32 consumed);
int x86_timer_confirm_write(const X86_LAPIC_MAPPING *mapping, BOOT_U32 offset, BOOT_U32 value);
int x86_timer_stop(const X86_LAPIC_MAPPING *mapping);
int x86_timer_loop_run(X86_LAPIC_TIMER *state, X86_IRQ_CALLBACK handle, X86_IRQ_CALLBACK quiet);
int x86_timer_loop_request_stop(void);
int x86_timer_loop_closed(const X86_LAPIC_TIMER *state);
#endif
