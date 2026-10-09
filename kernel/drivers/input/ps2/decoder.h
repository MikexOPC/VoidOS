#ifndef KERNEL_PS2_DECODER_H
#define KERNEL_PS2_DECODER_H

#include <stdint.h>

#define KERNEL_PS2_PENDING (1U << 24)
#define KERNEL_PS2_INVALID (2U << 24)
#define KERNEL_PS2_PRESS   (1U << 16)
#define KERNEL_PS2_RELEASE (2U << 16)
#define KERNEL_PS2_PULSE   (3U << 16)

/* Pure transition. Input state 0 starts/resets. Result high 32 bits: next
   state; low 32 bits: PENDING/INVALID or physical code | action.
   Invalid state/byte resets to 0 and returns INVALID; no truncation.
   No pointer, allocation, global state or callbacks. C owns one u32 state
   per consumer. Reset that state AND modifier tracking on queue LOSS. */
uint64_t kernel_ps2_decode(uint32_t state, uint32_t byte);

#endif
