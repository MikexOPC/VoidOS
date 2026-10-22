#include "session.h"
#include "kernel/scheduler/payload.h"
#include "kernel/console/line/line.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];

// ====== drain_private ======

// QEMU v11.1.0 hw/input/pckbd.c: KBD_STAT_OBF bit0, kbd_read_data,
// i8042_realizefn ports60h/64h. Consume only while OBF is set; a bounded
// drain does not guarantee an empty device after the budget is exhausted.
static BOOT_U32 drain_private(BOOT_U32 limit)
{
    BOOT_U32 dropped = 0;
    while (dropped < limit && (x86_read_port(0x64) & 1)) { (void)x86_read_port(0x60); ++dropped; }
    return dropped;
}

// ====== fault_private ======

static void fault_private(X86_PS2_SESSION *owner, BOOT_U32 dropped)
{
    owner->DevicePending = 1;
    if (owner->DeviceErrors != ~0U) ++owner->DeviceErrors;
    owner->Discarded = dropped > ~0U - owner->Discarded ? ~0U : owner->Discarded + dropped;
}

// ====== handle_private ======

static int handle_private(BOOT_U32 vector)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    BOOT_U32 count;
    unsigned char status, byte;
    BOOT_U32 *irqs = vector == 0x41 ? &owner->Irqs : &owner->Mouse.Irqs;
    if (!owner->Active || *irqs == ~0U) return 0;
    ++*irqs;
    /* QEMU pckbd.c status: OBF0, AUX5, timeout6/parity7. Read data60h
       only after OBF; 32-byte ISR budget, no allocation/logging/decoder.
       Parity/timeout, unexpected auxiliary data and bursts are device faults: the byte is
       consumed and dropped, the fault is counted and the interrupt is still acknowledged.
       Only ownership/protocol violations (payload push result 0) stay terminal. */
    for (count = 0; count < 32; ++count) {
        status = x86_read_port(0x64);
#ifdef KERNEL_PS2_MOUSE
        if (status & 0xc0) {
#else
        if (status & 0xe0) {
#endif
            BOOT_U32 dropped = 0;
            if (status & 1) { (void)x86_read_port(0x60); dropped = 1; }
            fault_private(owner, dropped);
            break;
        }
        if (!(status & 1)) break;
        byte = x86_read_port(0x60);
        if (!owner->Quiet) {
            BOOT_U32 auxiliary = (status >> 5) & 1U;
            BOOT_U32 *bytes = auxiliary ? &owner->Mouse.Bytes : &owner->Bytes;
            BOOT_U32 pushed = kernel_scheduler_payload_push(auxiliary ? 6U : 4U,
                auxiliary ? (BOOT_U64)&owner->Mouse : (BOOT_U64)owner,
                auxiliary ? owner->Mouse.Binding : owner->Binding,byte);
            if (!pushed || *bytes == ~0U) { owner->Error = 1; break; }
            ++*bytes; owner->Ready = 1;
        }
    }
    if (count == 32 && (x86_read_port(0x64) & 1)) fault_private(owner, drain_private(X86_PS2_DRAIN_LIMIT));
    /* Existing LAPIC helper verifies the entered ISR vector before EOI (SDM Vol3
       §10.8.5). Keep source ownership through acknowledgement. */
    return !owner->Error && x86_lapic_eoi_vector(owner->Env.Mapping,vector);
}

// ====== x86_ps2_handle ======

int x86_ps2_handle(void) { return handle_private(0x41); }

// ====== x86_ps2_mouse_handle ======

int x86_ps2_mouse_handle(void) { return handle_private(0x42); }

// ====== x86_ps2_mouse_quiet ======

int x86_ps2_mouse_quiet(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    return owner->Mouse.RouteOwned && x86_ps2_mouse_route_apply(owner,1);
}

// ====== x86_ps2_quiet ======

int x86_ps2_quiet(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    return owner->RouteOwned && x86_ps2_route_apply(owner,1);
}

// ====== x86_ps2_irq_record ======

int x86_ps2_irq_record(BOOT_U64 vector, BOOT_U64 rip, BOOT_U64 rsp, BOOT_U64 flags)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    if ((vector != 0x40 && vector != 0x41 && vector != 0x42) || x86_scheduler_index() != 1 ||
        !x86_scheduler_task_valid() || !(flags & 512) || (flags & 1024) ||
        rip < (BOOT_U64)x86_scheduler_irq_compute_begin || rip >= (BOOT_U64)x86_scheduler_irq_compute_end ||
        rsp < x86_task_stack_base(1) + 4096 || rsp >= x86_task_stack_base(1) + 65536 ||
        !x86_scheduler_code_valid(rip) || owner->TaskIrqs == ~0U) return 0;
    if (vector == 0x42) {
        if (owner->Mouse.TaskIrqs == UINT32_MAX) return 0;
        ++owner->Mouse.TaskIrqs;
    }
    if (vector == 0x41 || vector == 0x42) ++owner->TaskIrqs;
    return 1;
}
