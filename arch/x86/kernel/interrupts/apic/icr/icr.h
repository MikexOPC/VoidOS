#ifndef OS_X86_APIC_ICR_H
#define OS_X86_APIC_ICR_H
#include <stdint.h>

// Intel SDM 253668-078US 11.5.3: ESR at +280h, written before each read.
// 11.6.1 Figure 11-12: ICR high at +310h (destination [31:24]), low at +300h,
// delivery status bit 12. xAPIC MMIO only; x2APIC uses MSRs and is not covered.
#define X86_ICR_LOW             0x300U
#define X86_ICR_HIGH            0x310U
#define X86_ICR_ESR             0x280U
#define X86_ICR_PENDING         (1U << 12)

#define X86_ICR_CHECK_ESR       1U
#define X86_ICR_WAIT_SENT       2U

enum {
    X86_ICR_OK      = 0,
    X86_ICR_INVALID = 1,
    X86_ICR_STOPPED = 2,
    X86_ICR_IO      = 3,
    X86_ICR_APIC    = 4,
};

// Register access, budget and pause belong to the caller.
// Poll runs before every register access; returning 0 stops the send.
typedef struct {
    int   (*Read)(void *device, uint32_t offset, uint32_t *value);
    int   (*Write)(void *device, uint32_t offset, uint32_t value);
    int   (*Poll)(void *budget);
    void  (*Pause)(void *device);
    void  *Device;
    void  *Budget;
} X86_ICR_PORT;

uint32_t x86_icr_send(const X86_ICR_PORT *port, uint32_t destination, uint32_t command, uint32_t flags);
#endif
