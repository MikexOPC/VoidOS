#include "edu.h"

// QEMU v11.1.0 hw/misc/edu.c (the device has no datasheet: the model source is the reference).
// 0x00 identification 0x010000ed, 0x04 liveness (reads back the inverted value), 0x80 DMA source,
// 0x88 DMA destination, 0x90 DMA count, 0x98 DMA command: bit0 start/busy, bit1 direction
// (0 = RAM to device, 1 = device to RAM), bit2 interrupt on completion (never set here).
// Accesses below 0x80 are 4 bytes, from 0x80 4 or 8 bytes. The device buffer is 4096 bytes at
// 0x40000; a DMA address is masked to 28 bits (dma_mask) and a range outside the buffer is
// dropped. DMA errors are ignored by the device: completion never proves that the data moved.
#define EDU_LIVENESS 0x04U
#define EDU_SOURCE   0x80U
#define EDU_TARGET   0x88U
#define EDU_COUNT    0x90U
#define EDU_COMMAND  0x98U
#define EDU_START    1U
#define EDU_MASK     ((1ULL << 28) - 1)

// ====== edu_bus_valid ======

static int edu_bus_valid(const EDU_BUS *bus)
{
    return bus && bus->Read && bus->Write && bus->Ticks && bus->Hz &&
        (bus->Mask == 0xffffffULL || bus->Mask == 0xffffffffULL);
}

// ====== edu_identify ======

int edu_identify(const EDU_BUS *bus)
{
    BOOT_U64 value = 0;

    if (!edu_bus_valid(bus) || !bus->Read(bus->State, 0, 4, &value) || value != EDU_IDENTITY) return 0;

    if (!bus->Write(bus->State, EDU_LIVENESS, 4, 0x5aa5c33cULL) ||
        !bus->Read(bus->State, EDU_LIVENESS, 4, &value)
    )
    return 0;

    return value == (~0x5aa5c33cULL & 0xffffffffULL);
}

// ====== edu_wait ======

// Bounded like the VT-d modules: one second of PM clock, 1 << 20 reads, 256 stalled samples.
static int edu_wait(const EDU_BUS *bus)
{
    BOOT_U64 initial = bus->Ticks(bus->State), previous = initial;
    BOOT_U32 stagnant = 0;

    if (initial == ~0ULL) return 0;

    for (BOOT_U32 count = 0; count < (1U << 20); ++count) {
        BOOT_U64 now = bus->Ticks(bus->State), command = 0;

        if (now == ~0ULL || ((now - initial) & bus->Mask) >= bus->Hz ||
            (now == previous && ++stagnant == 256) ||
            !bus->Read(bus->State, EDU_COMMAND, 4, &command)
        )
        return 0;

        if (now != previous) stagnant = 0;

        previous = now;

        if (!(command & EDU_START)) return 1;
    }

    return 0;
}

// ====== edu_transfer ======

// One polled transfer between RAM (an IOVA under 2^28) and the device buffer.
int edu_transfer(const EDU_BUS *bus, BOOT_U64 source, BOOT_U64 destination, BOOT_U32 bytes, BOOT_U32 direction)
{
    BOOT_U64 device = direction == EDU_TO_RAM ? source : destination;
    BOOT_U64 memory = direction == EDU_TO_RAM ? destination : source, command = 0;

    if (!edu_bus_valid(bus) || direction > EDU_TO_RAM || !bytes || bytes > EDU_BUFFER_BYTES ||
        device < EDU_BUFFER || device - EDU_BUFFER > EDU_BUFFER_BYTES - bytes ||
        memory > EDU_MASK || EDU_MASK - memory < bytes - 1ULL ||
        !bus->Read(bus->State, EDU_COMMAND, 4, &command) || (command & EDU_START)
    )
    return 0;

    if (!bus->Write(bus->State, EDU_SOURCE, 8, source) ||
        !bus->Write(bus->State, EDU_TARGET, 8, destination) ||
        !bus->Write(bus->State, EDU_COUNT, 8, bytes) ||
        !bus->Write(bus->State, EDU_COMMAND, 4, EDU_START | (direction << 1))
    )
    return 0;

    return edu_wait(bus);
}
