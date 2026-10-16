#ifndef X86_PS2_SESSION_H
#define X86_PS2_SESSION_H
#include "kernel/drivers/input/ps2/controller/controller.h"
#include "kernel/console/line/line.h"
#include "kernel/shell/shell.h"
#include "arch/x86/kernel/scheduler/diagnostic/profiles.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/drivers/input/ps2/mouse/decoder.h"

typedef struct {
    BOOT_U32 Io, Pin, Gsi, Bits, Destination, Reserved;
} X86_PS2_ROUTE;
/* Consecutive framing errors (rejected header or payload loss) tolerated without a
   valid event before the session fails. Resync skips one byte per rejection and
   converges within a packet; 64 is a hypothesis to measure, not a hardware value. */
#define X86_PS2_MOUSE_RESYNC_LIMIT 64U
typedef struct {
    X86_PS2_ROUTE Route;
    X86_IRQ_TOKEN Token;
    BOOT_U64 Binding, Generation;
    BOOT_U32 SavedLow, SavedHigh, RouteOwned, Done;
    BOOT_U32 Bytes, Irqs, Events, Buttons, Losses, TaskIrqs;
    BOOT_U32 Rejected, Saturated, Streak;
    KERNEL_PS2_MOUSE_DECODER Decoder;
} X86_PS2_MOUSE;
typedef struct {
    _Alignas(8) volatile BOOT_U32 Ready;
    volatile BOOT_U32 Error;
    X86_WATCHDOG_ENV Env;
    X86_PM_CLOCK Clock;
    KERNEL_PS2_CONTROLLER Controller;
    X86_PS2_ROUTE Route;
    X86_IRQ_TOKEN Token;
    BOOT_U64 Binding, WorkerGeneration;
    BOOT_U32 Active, Stop, Quiet, ScopeEnded, ConsumerDone, WorkerDone;
    BOOT_U32 Bytes, Keys, Releases, Losses, Irqs, TaskIrqs, Idle, Decoder;
    BOOT_U32 SavedLow, SavedHigh, RouteOwned;
    BOOT_U32 Failed;
    BOOT_U32 TextState, TextCount;
    KERNEL_PS2_LINE Line;
    BOOT_U32 Lines, LineFull;
    KERNEL_PS2_COMMAND_QUEUE Commands;
    BOOT_U32 LineTruncated, ShellDone, ShellCommands;
    BOOT_U64 ConsumerGeneration;
    X86_PS2_MOUSE Mouse;
} X86_PS2_SESSION;
_Static_assert(offsetof(X86_PS2_SESSION,Error) == 4, "Watchdog source count/error ABI");
_Static_assert(offsetof(X86_PS2_SESSION,Token) % 8 == 0, "IRQ token alignment");
X86_PS2_SESSION *x86_ps2_session(void);
int x86_ps2_session_run(const X86_WATCHDOG_ENV *env, const X86_PM_CLOCK *clock,
    const KERNEL_ACPI_DIRECTORY *directory);
int x86_ps2_route_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    BOOT_U32 physical_bits, X86_PS2_ROUTE *route);
int x86_ps2_route_apply(X86_PS2_SESSION *owner, BOOT_U32 masked);
int x86_ps2_route_plan_source(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    BOOT_U32 physical_bits, BOOT_U32 source, X86_PS2_ROUTE *route);
int x86_ps2_mouse_route_apply(X86_PS2_SESSION *owner, BOOT_U32 masked);
int x86_ps2_mouse_handle(void);
int x86_ps2_mouse_quiet(void);
void x86_ps2_mouse_task(void);
int x86_ps2_mouse_open(X86_PS2_SESSION *owner);
int x86_ps2_mouse_close(X86_PS2_SESSION *owner);
int x86_ps2_mouse_report(X86_PS2_SESSION *owner);
int x86_ps2_handle(void);
int x86_ps2_quiet(void);
int x86_ps2_activate(void);
int x86_ps2_after_dispatch(void);
int x86_ps2_before_collect(void);
int x86_ps2_complete(void);
int x86_ps2_irq_record(BOOT_U64 vector, BOOT_U64 rip, BOOT_U64 rsp, BOOT_U64 flags);
void x86_ps2_task(uint32_t index);
int x86_ps2_idle(void);
int x86_scheduler_run_input(void);
#endif
