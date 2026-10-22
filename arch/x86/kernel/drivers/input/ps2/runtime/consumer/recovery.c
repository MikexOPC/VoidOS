#include "../session.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"

// ====== x86_ps2_fault_note ======

// Consumer side. note: one fault (LOSS marker, decoder INVALID, observed ISR fault) -> 1 resync and
// continue, 2 the streak passed the limit without a valid key event. poll: folds every ISR fault
// since the last call into one discontinuity. valid: a decoded key event ends the streak.

int x86_ps2_fault_note(X86_PS2_SESSION *owner)
{
    if (owner->Streak != UINT32_MAX) ++owner->Streak;
    return owner->Streak > X86_PS2_KEY_RESYNC_LIMIT ? 2 : 1;
}

// ====== x86_ps2_fault_poll ======

int x86_ps2_fault_poll(X86_PS2_SESSION *owner)
{
    BOOT_U32 seen = owner->DeviceErrors;
    if (!owner->DevicePending && seen == owner->DeviceSeen) return 0;
    owner->DevicePending = 0;
    owner->DeviceSeen = seen;
    return x86_ps2_fault_note(owner);
}

// ====== x86_ps2_fault_valid ======

void x86_ps2_fault_valid(X86_PS2_SESSION *owner) { owner->Streak = 0; }

// ====== x86_ps2_fault_discard ======

int x86_ps2_fault_discard(X86_PS2_SESSION *owner)
{
    // payload.h: BSP consumer IF0, 256 data bytes plus one ordered LOSS.
    // No yield or device I/O: the producer cannot refill this queue here.
    for (BOOT_U32 count = 0; count < 258; ++count) {
        BOOT_U64 value = kernel_scheduler_payload_pop(4,(BOOT_U64)owner,owner->Binding,
            0,x86_scheduler_generation());
        if (value == KERNEL_PAYLOAD_EMPTY) return 1;
        if ((value & ~255ULL) != KERNEL_PAYLOAD_DATA &&
            (BOOT_U32)value != KERNEL_PAYLOAD_LOSS) return 0;
    }
    return 0;
}

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

