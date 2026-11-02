#include "service.h"

// ====== terminal_private ======

static uint32_t terminal_private(uint32_t index) {
    (void)xis_fail(index, 0);
    return XIS_WAIT_FAILED;
}

// ====== xis_wait ======

uint32_t xis_wait(uint32_t index, uint64_t generation, const XIS_WAIT *wait) {
    if (!wait || !wait->Read || wait->Reserved || !wait->Ticks || wait->Ticks >= wait->Mask / 2 ||
        (wait->Mask != 0xffffffU && wait->Mask != UINT32_MAX) || !wait->MaxPolls ||
        wait->MaxPolls > 1000000 || !generation || generation == XIS_INVALID ||
        xis_value(index, XIS_PHASE) != XIS_READY || xis_value(index, XIS_REQUEST) != generation)
        return XIS_WAIT_INVALID;
    uint32_t start, previous;
    if (!wait->Read(wait->Context, &start) || (start & ~wait->Mask)) return terminal_private(index);
    previous = start;
    uint32_t elapsed = 0;
    for (uint32_t poll = 0; poll < wait->MaxPolls; ++poll) {
        uint32_t now;
        if (!wait->Read(wait->Context, &now) || (now & ~wait->Mask)) return terminal_private(index);
        uint32_t delta = (now - previous) & wait->Mask;
        if (delta >= wait->Mask / 2 || delta >= wait->Ticks - elapsed) return terminal_private(index);
        elapsed += delta;
        previous = now;
        if (xis_value(index, XIS_PHASE) != XIS_READY || xis_value(index, XIS_REQUEST) != generation)
            return terminal_private(index);
        uint64_t ack = xis_value(index, XIS_ACK);
        if (ack == generation) return XIS_WAIT_ACK;
        if (ack != generation - 1) return terminal_private(index);
    }
    return terminal_private(index);
}
