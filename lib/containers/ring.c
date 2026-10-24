#include "ring.h"

// ====== valid_private ======

static int valid_private(const OS_RING *ring)
{
    return ring && ring->Data && ring->Capacity && !(ring->Capacity & (ring->Capacity - 1)) &&
        ring->Capacity <= 0x80000000U && ring->Head - ring->Tail <= ring->Capacity;
}

// ====== os_ring_init ======

int os_ring_init(OS_RING *ring, uint8_t *storage, uint32_t capacity)
{
    if (!ring || !storage || !capacity || (capacity & (capacity - 1)) || capacity > 0x80000000U) return 0;

    *ring = (OS_RING){storage, capacity, 0, 0, 0};
    return 1;
}

// ====== os_ring_push ======

// A full ring keeps its content and counts the lost byte.
int os_ring_push(OS_RING *ring, uint8_t value)
{
    if (!valid_private(ring)) return 0;

    if (ring->Head - ring->Tail == ring->Capacity) {
        if (ring->Overflows != UINT32_MAX) ++ring->Overflows;
        return 0;
    }

    ring->Data[ring->Head & (ring->Capacity - 1)] = value;
    ++ring->Head;
    return 1;
}

// ====== os_ring_pop ======

int os_ring_pop(OS_RING *ring, uint8_t *value)
{
    if (!valid_private(ring) || !value || ring->Head == ring->Tail) return 0;

    *value = ring->Data[ring->Tail & (ring->Capacity - 1)];
    ++ring->Tail;
    return 1;
}

// ====== os_ring_count ======

uint32_t os_ring_count(const OS_RING *ring)
{
    return valid_private(ring) ? ring->Head - ring->Tail : 0;
}
