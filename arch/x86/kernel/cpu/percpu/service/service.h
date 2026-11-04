#ifndef OS_X86_PERCPU_SERVICE_H
#define OS_X86_PERCPU_SERVICE_H
#include <stdint.h>

enum { XIS_PING = 0, XIS_FLUSH_CONTEXT = 1, XIS_ARM_TIMER = 2, XIS_STOP_TIMER = 3 };

enum { XIS_PREPARED = 0, XIS_READY = 1, XIS_FAILED = 2 };
enum { XIS_PHASE = 0, XIS_REQUEST = 1, XIS_CLAIMED = 2, XIS_ACK = 3 };
#define XIS_INVALID UINT64_MAX

typedef int (*XIS_CLOCK_READ)(void *context, uint32_t *now);
typedef struct {
    XIS_CLOCK_READ  Read;
    void  *Context;
    uint32_t  Mask, Ticks, MaxPolls, Reserved;
} XIS_WAIT;
enum { XIS_WAIT_INVALID = 0, XIS_WAIT_ACK = 1, XIS_WAIT_FAILED = 2 };

uint32_t xis_configure(uint32_t count);
uint32_t xis_ready(uint32_t index, uint32_t caller);
uint64_t xis_publish(uint32_t index, uint32_t caller);
uint64_t xis_publish_operation(uint32_t index, uint32_t caller, uint32_t operation);
uint32_t xis_operation(uint32_t index, uint32_t caller, uint64_t generation);
uint64_t xis_claim(uint32_t index, uint32_t caller);
uint32_t xis_complete(uint32_t index, uint32_t caller, uint64_t generation);
uint32_t xis_fail(uint32_t index, uint32_t caller);
uint64_t xis_value(uint32_t index, uint32_t field);
uint32_t xis_wait(uint32_t index, uint64_t generation, const XIS_WAIT *wait);

#endif
