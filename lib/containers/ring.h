#ifndef OS_LIB_CONTAINERS_RING_H
#define OS_LIB_CONTAINERS_RING_H

#include <stdint.h>

// G1: fixed byte ring over caller storage; Capacity is a power of two (1..2^31). Head and Tail
// run freely and wrap modulo 2^32. Not synchronized: one owner at a time, or a single producer
// and consumer with the ordering provided by the caller.
typedef struct {
    uint8_t   *Data;
    uint32_t  Capacity;
    uint32_t  Head;
    uint32_t  Tail;
    uint32_t  Overflows;
} OS_RING;

int       os_ring_init(OS_RING *ring, uint8_t *storage, uint32_t capacity);
int       os_ring_push(OS_RING *ring, uint8_t value);
int       os_ring_pop(OS_RING *ring, uint8_t *value);
uint32_t  os_ring_count(const OS_RING *ring);

#endif
