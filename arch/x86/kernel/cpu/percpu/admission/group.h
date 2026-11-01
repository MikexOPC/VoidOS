#ifndef OS_X86_AP_ADMISSION_GROUP_H
#define OS_X86_AP_ADMISSION_GROUP_H
#include "../clock/clock.h"

// ====== immutable_group_abi ======

typedef struct {
    uint32_t  Version, Bytes, Count, PhysicalBits;
    uint64_t  Self, Root, Pat;
} XAG_HEADER;
typedef struct {
    uint32_t  Index, ApicId;
    uint64_t  Local, Wait;
    KERNEL_LOCK_WAIT  Expected;
    X86_PM_CLOCK  Source;
} XAG_CPU;
typedef struct {
    XAG_HEADER  Header;
    XAG_CPU  Cpus[64];
} XAG_GROUP;
_Static_assert(sizeof(XAG_HEADER) == 40 && sizeof(XAG_CPU) == 80 && sizeof(XAG_GROUP) == 5160,
    "Admission group ABI");
_Static_assert(offsetof(XAG_GROUP, Cpus) == 40 && offsetof(XAG_CPU, Expected) == 24 &&
    offsetof(XAG_CPU, Source) == 56, "Admission CPU offsets");

uint32_t xag_header_valid(XAG_HEADER header);
uint32_t xag_cpu_valid(XAG_CPU cpu, uint32_t bits);
// Trusted mapped input, exclusively owned until return; runtime entry uses xag_native_publish.
int xag_publish(const XAG_GROUP *input);
const XAG_GROUP *xag_group(void);
const XAG_CPU *xag_match(uint32_t index, uint32_t apic_id, uint64_t local);
#endif
