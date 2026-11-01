#ifndef OS_X86_AP_STARTUP_H
#define OS_X86_AP_STARTUP_H
#include <stdint.h>

// ====== startup_protocol ======

#define XS_PREPARED 1ULL
#define XS_COMMITTED 2ULL
#define XS_ARMED 3ULL
#define XS_ENTERED 4ULL
#define XS_PRIVATE 5ULL
#define XS_PARKED 6ULL
#define XS_QUARANTINED 7ULL

uint32_t xs_configure(uint32_t count);
uint64_t xs_reserve(uint32_t index);
uint32_t xs_advance(uint64_t token, uint64_t phase);
uint32_t xs_cancel(uint64_t token);
uint32_t xs_quarantine(uint64_t token);
uint64_t xs_snapshot(void);
#endif
