#include "session.h"
#include "kernel/scheduler/payload.h"
#include "kernel/console/line/line.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];

// ====== drain_private ======

// Policy point for bytes the ISR cannot deliver (B2). Today they are read and dropped, bounded by
// limit, so the one-byte i8042 output buffer is empty on return. Leaving them for the worker, or
// stashing them, would replace this body only: callers use the returned count, the consumer sees the
// discontinuity through DeviceErrors either way. Discarded measures whether that migration is needed.
static BOOT_U32 drain_private(BOOT_U32 limit)
{
    BOOT_U32 dropped = 0;
    while (dropped < limit && (x86_read_port(0x64) & 1)) { (void)x86_read_port(0x60); ++dropped; }
    return dropped;
}

// ====== fault_private ======

static void fault_private(X86_PS2_SESSION *owner, BOOT_U32 dropped)
{
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

// ====== keyboard fault policy ======

// Consumer side. note: one fault (LOSS marker, decoder INVALID, observed ISR fault) -> 1 resync and
// continue, 2 the streak passed the limit without a valid key event. poll: folds every ISR fault
// since the last call into one discontinuity. valid: a decoded key event ends the streak.

int x86_ps2_fault_note(X86_PS2_SESSION *owner)
{
    if (owner->Streak != UINT32_MAX) ++owner->Streak;
    return owner->Streak > X86_PS2_KEY_RESYNC_LIMIT ? 2 : 1;
}

int x86_ps2_fault_poll(X86_PS2_SESSION *owner)
{
    BOOT_U32 seen = owner->DeviceErrors;
    if (seen == owner->DeviceSeen) return 0;
    owner->DeviceSeen = seen;
    return x86_ps2_fault_note(owner);
}

void x86_ps2_fault_valid(X86_PS2_SESSION *owner) { owner->Streak = 0; }

// ====== x86_ps2_fault_resync ======

// A fault broke the byte stream: drop every partial state so no key stays held (modifiers live in
// TextState) and no half-typed command can run. verdict 2 (streak over the limit) ends the session
// through the existing Failed/Stop path. Returns 0 only for that verdict.
int x86_ps2_fault_resync(X86_PS2_SESSION *owner, int verdict)
{
    owner->Decoder = owner->TextState = 0;
    kernel_ps2_line_clear(&owner->Line);
    owner->LineTruncated = 0;
    if (owner->Resyncs != ~0U) ++owner->Resyncs;
    if (verdict != 2) return 1;
    if (owner->Losses != ~0U) ++owner->Losses;
    owner->Failed = 1; owner->Stop = 1;
    return 0;
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
