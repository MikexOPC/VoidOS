#include "controller.h"

// ====== controller_io_valid ======

static int controller_io_valid(const KERNEL_PS2_IO *io)
{
    return io && io->Read && io->Write && io->Clock &&
        (io->Mask == 0xffffffU || io->Mask == 0xffffffffU) &&
        io->Timeout && io->Timeout <= 357955U;
}

// ====== kernel_ps2_close ======

int kernel_ps2_close(KERNEL_PS2_CONTROLLER *owner)
{
    uint32_t error;
    if (!owner || !controller_io_valid(&owner->Io) || owner->Reserved ||
        (owner->Phase != KERNEL_PS2_PREPARED && owner->Phase != KERNEL_PS2_DIRTY)) return 0;
    error = owner->Error;
    owner->Phase = KERNEL_PS2_DIRTY;

    /*
        pckbd.c commands AD/A7: disable keyboard/aux interfaces.
        Mode bits4/5 disable, bits0/1 IRQ and bit6 translation stay clear.
        Only system bit2 is preserved. No A20/output-port commands.
    */

    if (!kernel_ps2_send(owner, 0x64, 0xad) || !kernel_ps2_send(owner, 0x64, 0xa7) ||
        !kernel_ps2_drain(owner) || !kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4) | 0x30))) {
        owner->Error = error | owner->Error | KERNEL_PS2_CLEANUP_FAILED;
        return 0;
    }
    owner->Error = error;
    owner->Phase = KERNEL_PS2_CLOSED;
    return 1;
}

// ====== kernel_ps2_start ======

int kernel_ps2_start(KERNEL_PS2_CONTROLLER *owner)
{
    if (!owner || !controller_io_valid(&owner->Io) || owner->Reserved ||
        owner->Phase != KERNEL_PS2_PREPARED || owner->Error) return 0;
    owner->Phase = KERNEL_PS2_DIRTY;
    /* pckbd.c mode bit4 clear enables keyboard; bit0 enables IRQ1.
       ps2.c F4 enables scanning only after verified ACK and route ownership. */
    if (!kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4) | 0x20)) ||
        !kernel_ps2_command(owner, 0xf4) ||
        !kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4) | 0x21))) return 0;
    return 1;
}

// ====== kernel_ps2_stop ======

int kernel_ps2_stop(KERNEL_PS2_CONTROLLER *owner)
{
    if (!owner || !controller_io_valid(&owner->Io) || owner->Reserved ||
        owner->Phase != KERNEL_PS2_DIRTY || owner->Error) return 0;
    if (!kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4) | 0x20)) ||
        !kernel_ps2_command(owner, 0xf5)) return 0;
    return kernel_ps2_close(owner);
}

// ====== kernel_ps2_prepare ======

int kernel_ps2_prepare(KERNEL_PS2_CONTROLLER *owner, const KERNEL_PS2_IO *io)
{
    uint8_t original, reply;
    if (!owner || owner->Phase || owner->Error || owner->Mode || owner->Reserved ||
        !controller_io_valid(io)) return 0;
    owner->Io = *io;
    owner->Phase = KERNEL_PS2_DIRTY;
    /* Quiesce before command traffic; reject excess stale bytes. */
    if (!kernel_ps2_send(owner, 0x64, 0xad) || !kernel_ps2_send(owner, 0x64, 0xa7) ||
        !kernel_ps2_drain(owner) || !kernel_ps2_send(owner, 0x64, 0x20) ||
        !kernel_ps2_receive(owner, &original)) goto failed;
    owner->Mode = original & 4;
    if (!kernel_ps2_mode(owner, (uint8_t)(owner->Mode | 0x30))) goto failed;
    /* pckbd.c KBD_CCMD_KBD_TEST=AB, expected interface response 00. */
    if (!kernel_ps2_send(owner, 0x64, 0xab) || !kernel_ps2_receive(owner, &reply)) goto failed;
    if (reply != 0) goto protocol;
    if (!kernel_ps2_send(owner, 0x64, 0xae)) goto failed;
    /* ps2.c: F5 resets and disables scanning; F0 02 selects set2.
       F0 00 queries the set; ACK alone does not establish set2. */
    if (!kernel_ps2_command(owner, 0xf5) || !kernel_ps2_command(owner, 0xf0) ||
        !kernel_ps2_command(owner, 2) || !kernel_ps2_command(owner, 0xf0) ||
        !kernel_ps2_command(owner, 0) || !kernel_ps2_receive(owner, &reply)) goto failed;
    if (reply != 2) goto protocol;
    if (!kernel_ps2_send(owner, 0x64, 0xad) ||
        !kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4) | 0x30)) ||
        !kernel_ps2_drain(owner)) goto failed;
    owner->Phase = KERNEL_PS2_PREPARED;
    owner->Error = 0;
    return 1;
protocol:
    owner->Error = KERNEL_PS2_PROTOCOL;
failed:
    /* DIRTY persists if quiescence cannot be confirmed; never fake close. */
    kernel_ps2_close(owner);
    return 0;
}
