#include "controller.h"

// ====== mouse_command ======

int kernel_ps2_mouse_command(KERNEL_PS2_CONTROLLER *owner, uint8_t command)
{
    uint8_t   reply;
    uint32_t  attempt;

    /* QEMU v10.1.0 pckbd.c WRITE_MOUSE (D4) routes the next data byte
       to AUX. ps2.c ps2_write_mouse replies FA; each retry needs D4. */
    for (attempt = 0; attempt < 3; ++attempt) {
        if (!kernel_ps2_send(owner, 0x64, 0xd4) ||
            !kernel_ps2_send(owner, 0x60, command) ||
            !kernel_ps2_receive_channel(owner, 1, &reply)) return 0;
        if (reply == 0xfa) return 1;
        if (reply != 0xfe) break;
    }
    owner->Error = KERNEL_PS2_PROTOCOL;
    return 0;
}

// ====== mouse_reply ======

static int mouse_reply(KERNEL_PS2_CONTROLLER *owner, uint8_t expected)
{
    uint8_t  reply;

    if (!kernel_ps2_receive_channel(owner, 1, &reply)) return 0;
    if (reply != expected) {
        owner->Error = KERNEL_PS2_PROTOCOL;
        return 0;
    }
    return 1;
}

// ====== kernel_ps2_mouse_prepare ======

int kernel_ps2_mouse_prepare(KERNEL_PS2_CONTROLLER *owner)
{
    uint8_t  reply;

    if (!owner || owner->Phase != KERNEL_PS2_PREPARED || owner->Error ||
        owner->Reserved || owner->Auxiliary ||
        owner->Mode != ((owner->Mode & 4U) | 0x30U) ||
        !owner->Io.Read || !owner->Io.Write || !owner->Io.Clock ||
        (owner->Io.Mask != 0xffffffU && owner->Io.Mask != UINT32_MAX) ||
        !owner->Io.Timeout || owner->Io.Timeout > 357955U) return 0;
    owner->Phase = KERNEL_PS2_DIRTY;

    /*
        QEMU v10.1.0 pckbd.c TEST_MOUSE A9 returns controller byte00,
        MOUSE_ENABLE A8 clears mode bit5. Both IRQ bits remain zero.
    */

    if (!kernel_ps2_send(owner, 0x64, 0xa9) ||
        !kernel_ps2_receive(owner, &reply)) goto failed;
    if (reply != 0) goto protocol;
    if (!kernel_ps2_send(owner, 0x64, 0xa8)) goto failed;

    /*
        QEMU v10.1.0 ps2.c ps2_write_mouse: FF resets to type00 and
        returns FA AA 00; F5 disables, E6 scale1:1, EA stream, F2 type.
        No F4 is sent: preparation never enables data reporting.
    */

    if (!kernel_ps2_mouse_command(owner, 0xff) || !mouse_reply(owner, 0xaa) ||
        !mouse_reply(owner, 0) || !kernel_ps2_mouse_command(owner, 0xf5) ||
        !kernel_ps2_mouse_command(owner, 0xe6) || !kernel_ps2_mouse_command(owner, 0xea) ||
        !kernel_ps2_mouse_command(owner, 0xf2) || !mouse_reply(owner, 0)) goto failed;
    if (!kernel_ps2_send(owner, 0x64, 0xa7) ||
        !kernel_ps2_drain(owner) ||
        !kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4U) | 0x30U))) goto failed;
    owner->Auxiliary = 1;
    owner->Phase = KERNEL_PS2_PREPARED;
    return 1;
protocol:
    owner->Error = KERNEL_PS2_PROTOCOL;
failed:
    kernel_ps2_close(owner);
    return 0;
}

// ====== kernel_ps2_mouse_enable_wheel ======

// Microsoft IntelliMouse PS/2 Extension: Set Sample Rate 200 -> 100 -> 80, then Get Device ID (0xF2).
// If device replies ID 0x03, 4-byte packets [status, dx, dy, int8 wheel] are active.
int kernel_ps2_mouse_enable_wheel(KERNEL_PS2_CONTROLLER *owner)
{
    uint8_t id = 0;
    int enabled = 0;
    if (!owner || owner->Phase != KERNEL_PS2_PREPARED || owner->Error || !owner->Auxiliary)
        return 0;
    if (!kernel_ps2_send(owner, 0x64, 0xa8))
        return 0;
    if (kernel_ps2_mouse_command(owner, 0xf3) && kernel_ps2_mouse_command(owner, 200) &&
        kernel_ps2_mouse_command(owner, 0xf3) && kernel_ps2_mouse_command(owner, 100) &&
        kernel_ps2_mouse_command(owner, 0xf3) && kernel_ps2_mouse_command(owner, 80) &&
        kernel_ps2_mouse_command(owner, 0xf2) && kernel_ps2_receive_channel(owner, 1, &id) &&
        id == 0x03U) {
        enabled = 1;
    }
    owner->Error = 0;
    (void)kernel_ps2_send(owner, 0x64, 0xa7);
    (void)kernel_ps2_drain(owner);
    (void)kernel_ps2_mode(owner, (uint8_t)((owner->Mode & 4U) | 0x30U));
    owner->Phase = KERNEL_PS2_PREPARED;
    return enabled;
}
