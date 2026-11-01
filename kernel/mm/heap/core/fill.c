#include "../internal.h"

// ====== kh_fill ======

void kh_fill(KERNEL_HEAP *heap, const KH_TRANSFER *transfer) {
#ifndef KERNEL_HEAP_TEST_FILL
    (void)heap;
#else
    kh_test_fill(heap, transfer, 0);
#endif
    volatile uint8_t *data = (volatile uint8_t *)transfer->Address;
    uint64_t bytes = transfer->Bytes - (transfer->Small && !transfer->Release ? 8 : 0);
    uint8_t value = transfer->Release ? KERNEL_HEAP_POISON : 0;
    for (uint64_t index = 0; index < bytes; ++index) data[index] = value;
    if (transfer->Small && !transfer->Release)
        *(volatile uint64_t *)(transfer->Address + transfer->Bytes - 8) = KERNEL_HEAP_CANARY;
#ifdef KERNEL_HEAP_TEST_FILL
    kh_test_fill(heap, transfer, 1);
#endif
}
