#ifndef OS_KERNEL_SCHEDULER_PAYLOAD_H
#define OS_KERNEL_SCHEDULER_PAYLOAD_H
#include <stdint.h>

#define KERNEL_PAYLOAD_EMPTY UINT64_C(1)
#define KERNEL_PAYLOAD_LOSS UINT64_C(2)
#define KERNEL_PAYLOAD_DENIED UINT64_C(4)
#define KERNEL_PAYLOAD_BUSY UINT64_C(8)
#define KERNEL_PAYLOAD_DATA UINT64_C(0x100)

// ====== private_bound_payload_contract ======

// One BSP IRQ producer and one native task consumer, fixed 256-byte capacity.
// open/close: verified dispatcher/end-session IF0; source+binding token required.
// close refuses data/LOSS/pending. Stop/EOI/quiescence must be proved by backend.
// push: trusted bound ISR IF0, result 1=data accepted, 2=loss recorded, 0=rejected.
// pop: native RUNNING task IF0, index+generation verified before Rust borrows.
// First valid pop claims the consumer generation for this channel lifetime.
// Returns DATA|byte, EMPTY, LOSS|(saturating cumulative loss count<<32),
// DENIED or BUSY. LOSS follows pre-loss bytes; intake closed until LOSS observed.
// On EMPTY: OBSERVE, recheck data/LOSS, then WAIT on unchanged epoch.
// No buffer pointers cross this ABI; private trusted kernel API, not a syscall.
uint32_t kernel_scheduler_payload_open(uint32_t event, uint64_t source, uint64_t token, uint32_t consumer);
uint32_t kernel_scheduler_payload_push(uint32_t event, uint64_t source, uint64_t token, uint32_t byte);
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t source, uint64_t token,
    uint32_t consumer, uint64_t generation);
uint32_t kernel_scheduler_payload_close(uint32_t event, uint64_t source, uint64_t token);
uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation);
#endif
