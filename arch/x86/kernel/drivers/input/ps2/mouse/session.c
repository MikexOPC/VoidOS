#include "../runtime/session.h"
#include "kernel/drivers/input/ps2/mouse/controller.h"
#include "kernel/scheduler/payload.h"
#include "kernel/diagnostics/log.h"

// ====== x86_ps2_mouse_open ======

int x86_ps2_mouse_open(X86_PS2_SESSION *owner)
{
    const X86_HPET_WATCHDOG *watchdog = x86_watchdog_state();
    X86_PS2_MOUSE *mouse;
    if (!owner || owner->Active || owner->Mouse.Binding || !watchdog) return 0;
    mouse = &owner->Mouse;
    if (!x86_ps2_route_plan_source(owner->Env.Topology,owner->Env.Capabilities,
        owner->Env.Window->Pool->PhysicalBits,12,&mouse->Route) ||
        (mouse->Route.Io == owner->Route.Io && mouse->Route.Pin == owner->Route.Pin) ||
        (owner->Env.Capabilities->Io[mouse->Route.Io].Address == watchdog->IoAddress &&
        mouse->Route.Pin == watchdog->Pin) || !kernel_ps2_mouse_prepare(&owner->Controller) ||
        !x86_ps2_mouse_route_apply(owner,1)) return 0;
    mouse->Binding = kernel_scheduler_irq_bind(6,(BOOT_U64)mouse);
    return mouse->Binding && kernel_scheduler_payload_open(6,(BOOT_U64)mouse,mouse->Binding,3) &&
        x86_irq_registry_bind(x86_irq_registry(),0x42,x86_ps2_mouse_handle,x86_ps2_mouse_quiet,&mouse->Token);
}

// ====== x86_ps2_mouse_close ======

int x86_ps2_mouse_close(X86_PS2_SESSION *owner)
{
    X86_PS2_MOUSE *mouse;
    if (!owner || !owner->Quiet || !owner->Mouse.Done || owner->Mouse.Token.Id || owner->Mouse.RouteOwned) return 0;
    mouse = &owner->Mouse;
    return kernel_scheduler_payload_close(6,(BOOT_U64)mouse,mouse->Binding) &&
        kernel_scheduler_irq_unbind(6,(BOOT_U64)mouse,mouse->Binding);
}

// ====== x86_ps2_mouse_report ======

int x86_ps2_mouse_report(X86_PS2_SESSION *owner)
{
    return owner && kernel_text("MOUSE_BYTES: ") && kernel_number(owner->Mouse.Bytes) &&
        kernel_text("\nMOUSE_EVENTS: ") && kernel_number(owner->Mouse.Events) &&
        kernel_text("\nMOUSE_IRQS: ") && kernel_number(owner->Mouse.Irqs) &&
        kernel_text("\nMOUSE_TASK_IRQS: ") && kernel_number(owner->Mouse.TaskIrqs) &&
        kernel_text("\nMOUSE_LOSSES: ") && kernel_number(owner->Mouse.Losses) && kernel_text("\n") &&
        kernel_log(0,"KERNEL_PS2_MOUSE_CLEANUP_OK");
}
