#include "controller.h"

/*
    QEMU v10.1.0 hw/input/pckbd.c: i8042_realizefn maps byte I/O at 0x60
    (data) and 0x64 (status/command). KBD_STAT_OBF bit0, IBF bit1,
    mouse bit5, timeout bit6, parity bit7. Only data with OBF is consumed.
    No device DMA/MMIO. Clock callbacks have the validated PM timer mask.
*/

// ====== wait_status ======

static int wait_status(KERNEL_PS2_CONTROLLER *owner, uint8_t bit, uint8_t expected,
    uint8_t *status)
{
    uint32_t start, now, poll;
    if (!owner->Io.Clock(owner->Io.Context, &start)) goto io_error;
    for (poll = 0; poll < 1000000U; ++poll) {
        if (!owner->Io.Clock(owner->Io.Context, &now) ||
            !owner->Io.Read(owner->Io.Context, 0x64, status)) goto io_error;
        if (((now - start) & owner->Io.Mask) >= owner->Io.Timeout) break;
        if (*status & 0xc0) { owner->Error = KERNEL_PS2_PROTOCOL; return 0; }
        if ((*status & bit) == expected) return 1;
    }
    owner->Error = KERNEL_PS2_TIMEOUT;
    return 0;
io_error:
    owner->Error = KERNEL_PS2_IO_ERROR;
    return 0;
}

// ====== kernel_ps2_send ======

int kernel_ps2_send(KERNEL_PS2_CONTROLLER *owner, uint16_t port, uint8_t byte)
{
    uint8_t status;
    if ((port != 0x60 && port != 0x64) || !wait_status(owner, 2, 0, &status)) return 0;
    if (!owner->Io.Write(owner->Io.Context, port, byte)) {
        owner->Error = KERNEL_PS2_IO_ERROR;
        return 0;
    }
    return 1;
}

// ====== kernel_ps2_receive ======

int kernel_ps2_receive(KERNEL_PS2_CONTROLLER *owner, uint8_t *byte)
{
    uint8_t status;
    if (!wait_status(owner, 1, 1, &status)) return 0;
    if (!owner->Io.Read(owner->Io.Context, 0x60, byte)) {
        owner->Error = KERNEL_PS2_IO_ERROR;
        return 0;
    }
    if (status & 0x20) { owner->Error = KERNEL_PS2_PROTOCOL; return 0; }
    return 1;
}

// ====== kernel_ps2_drain ======

int kernel_ps2_drain(KERNEL_PS2_CONTROLLER *owner)
{
    uint32_t count;
    uint8_t status, byte;
    for (count = 0; count <= 32; ++count) {
        if (!owner->Io.Read(owner->Io.Context, 0x64, &status)) goto io_error;
        if (status & 0xc0) { owner->Error = KERNEL_PS2_PROTOCOL; return 0; }
        if (!(status & 1)) return 1;
        if (count == 32) { owner->Error = KERNEL_PS2_PROTOCOL; return 0; }
        if (!owner->Io.Read(owner->Io.Context, 0x60, &byte)) goto io_error;
    }
io_error:
    owner->Error = KERNEL_PS2_IO_ERROR;
    return 0;
}

// ====== kernel_ps2_mode ======

int kernel_ps2_mode(KERNEL_PS2_CONTROLLER *owner, uint8_t mode)
{
    uint8_t readback;
    /* pckbd.c KBD_CCMD_WRITE_MODE=0x60, READ_MODE=0x20. Verify write. */
    if (!kernel_ps2_send(owner, 0x64, 0x60) || !kernel_ps2_send(owner, 0x60, mode) ||
        !kernel_ps2_send(owner, 0x64, 0x20) || !kernel_ps2_receive(owner, &readback)) return 0;
    if (readback != mode) { owner->Error = KERNEL_PS2_PROTOCOL; return 0; }
    owner->Mode = mode;
    return 1;
}

// ====== kernel_ps2_command ======

int kernel_ps2_command(KERNEL_PS2_CONTROLLER *owner, uint8_t command)
{
    uint8_t reply;
    uint32_t attempt;

    /*
        ps2.c ps2_write_keyboard: ACK=FA, RESEND=FE. No other response is
        treated as success; three total writes including the first attempt.
    */

    for (attempt = 0; attempt < 3; ++attempt) {
        if (!kernel_ps2_send(owner, 0x60, command) || !kernel_ps2_receive(owner, &reply)) return 0;
        if (reply == 0xfa) return 1;
        if (reply != 0xfe) break;
    }
    owner->Error = KERNEL_PS2_PROTOCOL;
    return 0;
}
