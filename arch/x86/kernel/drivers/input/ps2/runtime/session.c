#include "session.h"
#ifdef KERNEL_PS2_MOUSE
#include "kernel/drivers/input/ps2/mouse/stream/control.h"
#endif
#ifdef KERNEL_CONSOLE_SHELL
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#endif
#ifdef KERNEL_TEST_CONSOLE_DRAW
#include "test/kernel/console/video/active/worker.h"
#endif
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_AP_TASKS
#include "arch/x86/kernel/scheduler/remote/remote.h"
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif
static X86_PS2_SESSION session;
#ifdef KERNEL_CONSOLE_SHELL
// G6: the console object of this input session (composition root of the interactive session).
static X86_CONSOLE console;
#endif

// ====== x86_ps2_session ======

X86_PS2_SESSION *x86_ps2_session(void) { return &session; }

// ====== firmware_read ======

static int firmware_read(void *context, OS_ACPI_U64 address, OS_ACPI_U32 bytes, unsigned char *output)
{
    X86_PS2_SESSION *owner = context;
    return x86_page_window_read_acpi(owner->Env.Window,owner->Env.Context,address,bytes,output);
}

// ====== port_read ======

static int port_read(void *context, uint16_t port, uint8_t *output)
{
    (void)context;
    /* Byte ports60/64, QEMU pckbd.c i8042_realizefn; FADT8042 authorized. */
    if (!output || (port != 0x60 && port != 0x64) || !x86_exception_environment()) return 0;
    *output = x86_read_port(port); return 1;
}

// ====== port_write ======

static int port_write(void *context, uint16_t port, uint8_t value)
{
    (void)context;
    if ((port != 0x60 && port != 0x64) || !x86_exception_environment()) return 0;
    x86_write_port(port,value); return 1;
}

// ====== clock_read ======

static int clock_read(void *context, uint32_t *ticks)
{
    X86_PS2_SESSION *owner = context;
    /* Existing validated ACPI PM clock, ACPI6.5 §4.8.3.3, 32-bit read. */
    if (!ticks || !owner->Clock.Ready || !x86_exception_environment()) return 0;
    *ticks = x86_pm_read((unsigned short)owner->Clock.Port) & owner->Clock.Mask; return 1;
}

// ====== x86_ps2_activate ======

int x86_ps2_activate(void)
{
    if (!session.Active || session.Quiet || !x86_watchdog_budget_active()) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_ps2_pair_start(&session.Controller) || !x86_ps2_mouse_route_apply(&session,0)) return 0;
#else
    if (!kernel_ps2_start(&session.Controller)) return 0;
#endif
    return x86_ps2_route_apply(&session,0) && kernel_log(0,"KERNEL_PS2_INPUT_READY");
}

// ====== x86_ps2_after_dispatch ======

int x86_ps2_after_dispatch(void)
{
    KERNEL_EVENT_REPLY reply;
#ifdef KERNEL_AP_TASKS
    x86_remote_service();
#endif
    if (!session.Stop || session.Quiet) return 1;
    if (!session.WorkerGeneration || !x86_ps2_route_apply(&session,1)) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (!session.Mouse.Generation || !x86_ps2_mouse_route_apply(&session,1) ||
        !kernel_ps2_pair_stop(&session.Controller)) return 0;
#else
    if (!kernel_ps2_stop(&session.Controller)) return 0;
#endif
    session.Quiet = 1; session.Ready = 1;
    // Drain any owned IRR before releasing the handler; independent HPET ack.
    if (!x86_watchdog_idle(&session.Ready) || !x86_lapic_idle(session.Env.Mapping) ||
        x86_irq_registry_deliveries(x86_irq_registry(),&session.Token) != session.Irqs ||
        !x86_irq_registry_unbind(x86_irq_registry(),&session.Token) || !x86_ps2_route_apply(&session,2)) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (x86_irq_registry_deliveries(x86_irq_registry(),&session.Mouse.Token) != session.Mouse.Irqs ||
        !x86_irq_registry_unbind(x86_irq_registry(),&session.Mouse.Token) || !x86_ps2_mouse_route_apply(&session,2)) return 0;
#endif
#ifdef KERNEL_AP_TASKS
    // The dispatcher must still own the scheduler while xcn_close drains event 7.
    if (xis_native.Count >= 2 && (!kernel_text("KERNEL_AP_TASK_JOBS: ") ||
        !kernel_number(x86_remote_jobs()) || !kernel_text("\n") || !x86_remote_close() ||
        !xcn_close(1,1) || !xcn_release_allowed() || !kernel_log(0,"KERNEL_AP_TASKS_CLEAN"))) return 0;
#endif
#ifdef KERNEL_PS2_MOUSE
    reply = kernel_scheduler_stop_apply(0,3,session.Mouse.Generation);
    if (reply.reply.status != KERNEL_SCHEDULER_OK) return 0;
#endif
    reply = kernel_scheduler_stop_apply(0,1,session.WorkerGeneration);
    if (reply.reply.status != KERNEL_SCHEDULER_OK || !session.ConsumerGeneration) return 0;
    reply = kernel_scheduler_stop_apply(0,0,session.ConsumerGeneration);
    return reply.reply.status == KERNEL_SCHEDULER_OK;
}

// ====== x86_ps2_before_collect ======

int x86_ps2_before_collect(void)
{
    if (!session.Quiet || session.Token.Id || session.RouteOwned || !session.Stop) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (session.Mouse.Token.Id || session.Mouse.RouteOwned) return 0;
#endif
    if (session.ScopeEnded) return 1;
    if (!x86_watchdog_budget_end()) return 0;
    session.ScopeEnded = 1; return 1;
}

// ====== x86_ps2_complete ======

int x86_ps2_complete(void)
{
#ifdef KERNEL_PS2_MOUSE
    if (!session.Mouse.Done || session.Mouse.Token.Id || session.Mouse.RouteOwned ||
        session.Mouse.Decoder.State || session.Mouse.Buttons) return 0;
#endif
    return session.Stop && session.Quiet && session.ScopeEnded && session.ConsumerDone && session.WorkerDone &&
        session.ShellDone && kernel_ps2_command_closed_empty(&session.Commands) &&
        !session.Error && !session.TextState && !session.LineTruncated && !session.Line.Length && !session.Line.Data[0] &&
        !session.Token.Id && !session.RouteOwned &&
        session.Controller.Phase == KERNEL_PS2_CLOSED;
}

// ====== x86_ps2_session_run ======

int x86_ps2_session_run(const X86_WATCHDOG_ENV *env, const X86_PM_CLOCK *clock,
    const KERNEL_ACPI_DIRECTORY *directory)
{
    KERNEL_PS2_DESCRIPTION description;
    KERNEL_PS2_IO io;
    const X86_HPET_WATCHDOG *watchdog = x86_watchdog_state();
    if (!env || !clock || !directory || session.Active || session.Stop || !clock->Ready ||
        !x86_watchdog_valid() || env->Window != watchdog->Env.Window || env->Mapping != watchdog->Env.Mapping) return 0;
    session.Env = *env; session.Clock = *clock;
    if (!kernel_ps2_discover(directory,env->Window->Pool->PhysicalBits,firmware_read,&session,&description)) {
        kernel_log(3,"PS2_DISCOVERY_FAILED"); return 0;
    }
    if (!description.Present) return kernel_log(2,"KERNEL_PS2_INPUT_ABSENT");
    if (!x86_ps2_route_plan(env->Topology,env->Capabilities,env->Window->Pool->PhysicalBits,&session.Route)) {
        kernel_log(3,"PS2_ROUTE_PLAN_FAILED"); return 0;
    }
    kernel_log(1,"KERNEL_PS2_ROUTE_CAPTURED");
    if (env->Capabilities->Io[session.Route.Io].Address == watchdog->IoAddress && session.Route.Pin == watchdog->Pin) return 0;
    io = (KERNEL_PS2_IO){&session,port_read,port_write,clock_read,clock->Mask,357955};
    if (!kernel_ps2_prepare(&session.Controller,&io)) { kernel_log(3,"PS2_PREPARE_FAILED"); return 0; }
    if (!x86_ps2_route_apply(&session,1)) { kernel_log(3,"PS2_ROUTE_ACQUIRE_FAILED"); return 0; }
    kernel_log(1,"KERNEL_PS2_ROUTE_ACQUIRED");
    session.Binding = kernel_scheduler_irq_bind(4,(BOOT_U64)&session);
    if (!session.Binding || !kernel_scheduler_payload_open(4,(BOOT_U64)&session,session.Binding,0) ||
        !x86_irq_registry_bind(x86_irq_registry(),0x41,x86_ps2_handle,x86_ps2_quiet,&session.Token)) {
        kernel_log(3,"PS2_BINDING_FAILED"); return 0;
    }
#ifdef KERNEL_PS2_MOUSE
    if (!x86_ps2_mouse_open(&session)) { kernel_log(3,"PS2_MOUSE_OPEN_FAILED"); return 0; }
#endif
#ifdef KERNEL_CONSOLE_SHELL
    session.Console = &console;
    if (!x86_console_open(&session, x86_vgpu_kernel_display())) return 0;
#endif
#ifdef KERNEL_AP_TASKS
    if (xis_native.Count < 2) kernel_log(1,"KERNEL_AP_TASKS_ABSENT");
    else {
        uint64_t root = 0;
        KERNEL_PHYSICAL_ALLOCATOR *pool = x86_scheduler_batch_resources(&root);
        if (!pool || !xcn_prepare(root) ||
            !x86_remote_open(pool,root,XCN_EVENT,xcn_record.Self,xcn_record.Binding)) {
            kernel_log(3,"KERNEL_AP_TASKS_OPEN_FAILED"); return 0;
        }
        kernel_log(0,"KERNEL_AP_TASKS_READY");
    }
#endif
    session.Active = 1;
    if (!x86_scheduler_run_input() || !kernel_scheduler_payload_close(4,(BOOT_U64)&session,session.Binding) ||
        !kernel_scheduler_irq_unbind(4,(BOOT_U64)&session,session.Binding) || !x86_watchdog_valid()) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (!x86_ps2_mouse_close(&session) || !x86_ps2_mouse_report(&session)) return 0;
#endif
    session.Active = 0;
#ifdef KERNEL_CONSOLE_SHELL
    if (!x86_console_finish(&session)) return 0;
#endif
#ifdef KERNEL_TEST_CONSOLE_DRAW
    if (!kernel_console_active_finish(&session)) return 0;
#endif
    if (!kernel_text("PS2_BYTES: ") || !kernel_number(session.Bytes) || !kernel_text("\nPS2_KEYS: ") ||
        !kernel_number(session.Keys) || !kernel_text("\nPS2_RELEASES: ") || !kernel_number(session.Releases) ||
        !kernel_text("\nPS2_TASK_IRQS: ") || !kernel_number(session.TaskIrqs) ||
        !kernel_text("\nPS2_IDLE: ") || !kernel_number(session.Idle) ||
        !kernel_text("\nPS2_TEXT_COUNT: ") || !kernel_number(session.TextCount) ||
        !kernel_text("\nPS2_LINE_COUNT: ") || !kernel_number(session.Lines) ||
        !kernel_text("\nPS2_LINE_FULL_COUNT: ") || !kernel_number(session.LineFull) ||
        !kernel_text("\nPS2_RESYNCS: ") || !kernel_number(session.Resyncs) ||
        !kernel_text("\nPS2_DEVICE_ERRORS: ") || !kernel_number(session.DeviceErrors) ||
        !kernel_text("\nPS2_DISCARDED: ") || !kernel_number(session.Discarded) ||
        !kernel_text("\nSHELL_COMMAND_COUNT: ") || !kernel_number(session.ShellCommands) || !kernel_text("\n")) return 0;
    if (session.Failed) { kernel_log(2,"KERNEL_PS2_INPUT_REJECTED_CLEAN"); return 0; }
    return kernel_log(0,"KERNEL_PS2_INPUT_CLEANUP_OK");
}
