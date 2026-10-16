#include "control.h"

// ====== pair_valid ======

static int pair_valid(const KERNEL_PS2_CONTROLLER *owner, uint32_t phase)
{
    return owner && owner->Phase == phase && !owner->Error && !owner->Reserved &&
        owner->Auxiliary == 1 && owner->Io.Read && owner->Io.Write && owner->Io.Clock &&
        (owner->Io.Mask == 0xffffffU || owner->Io.Mask == UINT32_MAX) &&
        owner->Io.Timeout && owner->Io.Timeout <= 357955U;
}

// ====== kernel_ps2_pair_start ======

int kernel_ps2_pair_start(KERNEL_PS2_CONTROLLER *owner)
{
    if (!pair_valid(owner, KERNEL_PS2_PREPARED) ||
        owner->Mode != ((owner->Mode & 4U) | 0x30U)) return 0;
    owner->Phase = KERNEL_PS2_DIRTY;
    /* QEMU v10.1.0 pckbd.c mode bits0/1 enable IRQ1/12; AE/A8 enable
       interfaces. ps2.c F4 enables reporting. Routes remain masked;
       block each reporting interface before the next command/readback. */
    if (!kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4U) | 3U)) ||
        !kernel_ps2_command(owner, 0xf4) || !kernel_ps2_send(owner, 0x64, 0xad) ||
        !kernel_ps2_drain(owner) || !kernel_ps2_mouse_command(owner, 0xf4) ||
        !kernel_ps2_send(owner, 0x64, 0xa7) || !kernel_ps2_drain(owner) ||
        !kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4U) | 0x33U)) ||
        !kernel_ps2_send(owner, 0x64, 0xae) || !kernel_ps2_send(owner, 0x64, 0xa8)) return 0;
    owner->Mode = (owner->Mode & 4U) | 3U;
    return 1;
}

// ====== kernel_ps2_pair_stop ======

int kernel_ps2_pair_stop(KERNEL_PS2_CONTROLLER *owner)
{
    uint8_t  reply;

    if (!pair_valid(owner, KERNEL_PS2_DIRTY)) return 0;
    /* QEMU v10.1.0 pckbd.c WRITE_MODE clears IRQ0/1 and disables ports4/5.
       ps2.c AUX_RESET FF stops reporting and clears the mouse queue before
       FA AA 00. F5 alone leaves queued packets; reset avoids ambiguous ACK
       inside a motion payload. This is device reset via D4, never CPU reset. */
    if (!kernel_ps2_send(owner, 0x64, 0x60) ||
        !kernel_ps2_send(owner, 0x60, (uint8_t)((owner->Mode & 4U) | 0x30U)) ||
        !kernel_ps2_drain(owner) || !kernel_ps2_mouse_command(owner, 0xff) ||
        !kernel_ps2_receive_channel(owner, 1, &reply)) return 0;
    if (reply != 0xaa) goto protocol;
    if (!kernel_ps2_receive_channel(owner, 1, &reply)) return 0;
    if (reply != 0) goto protocol;
    if (!kernel_ps2_send(owner, 0x64, 0xa7) || !kernel_ps2_stop_scanning(owner)) return 0;
    return kernel_ps2_close(owner);
protocol:
    owner->Error = KERNEL_PS2_PROTOCOL;
    return 0;
}
