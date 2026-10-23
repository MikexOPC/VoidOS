#include "model.h"
MODEL model;
unsigned checks;

// ====== model_reset ======

void model_reset(void)
{
    memset(&model, 0, sizeof(model));
    model.Identity = EDU_IDENTITY;
    model.Step = 7;
    model.BusyReads = 3;
}

// ====== read_private ======

static int read_private(void *state, BOOT_U32 offset, BOOT_U32 width, BOOT_U64 *value)
{
    MODEL *device = state;

    if (++device->Reads == device->FailRead) return 0;

    // QEMU edu: 4-byte accesses below 0x80, 4 or 8 bytes from 0x80.
    CHECK((width == 4 || (width == 8 && offset >= 0x80)) && value);

    if (offset == 0) *value = device->Identity;
    else if (offset == 4) *value = device->Invert ? device->Liveness : (BOOT_U64)(BOOT_U32)~device->Liveness;
    else if (offset == 0x98) {
        if ((device->Command & 1) && device->BusyReads && --device->BusyReads == 0) device->Command &= ~1ULL;

        *value = device->Command;
    }
    else CHECK(0);

    return 1;
}

// ====== write_private ======

static int write_private(void *state, BOOT_U32 offset, BOOT_U32 width, BOOT_U64 value)
{
    MODEL *device = state;

    if (++device->Writes == device->FailWrite) return 0;

    CHECK((width == 4 || (width == 8 && offset >= 0x80)) && device->Writes <= 16);
    device->Log[device->Writes - 1] = (MODEL_WRITE){offset, width, value};

    if (offset == 4) device->Liveness = (BOOT_U32)value;
    else if (offset == 0x80) device->Source = value;
    else if (offset == 0x88) device->Target = value;
    else if (offset == 0x90) device->Count = value;
    else if (offset == 0x98) device->Command = value;
    else CHECK(0);

    return 1;
}

// ====== ticks_private ======

static BOOT_U64 ticks_private(void *state)
{
    MODEL *device = state;

    if (!device->Stall) device->Ticks += device->Step;

    return device->Ticks;
}

// ====== model_bus ======

EDU_BUS model_bus(void)
{
    return (EDU_BUS){&model, read_private, write_private, ticks_private, 3579545, 0xffffff};
}
