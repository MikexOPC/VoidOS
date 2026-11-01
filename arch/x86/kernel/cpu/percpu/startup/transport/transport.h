#ifndef OS_X86_AP_WAKE_TRANSPORT_H
#define OS_X86_AP_WAKE_TRANSPORT_H
#include "../startup.h"
#include "kernel/time/clock.h"

// ====== wake_transport_contract ======

#define XW_INVALID 1U
#define XW_CLOCK 2U
#define XW_DEADLINE 3U
#define XW_IO 4U
#define XW_GUARD 5U
#define XW_STATE 6U
#define XW_APIC 7U
#define XW_POLLS 8U

typedef struct {
    int (*Read)(void *, uint32_t, uint32_t *);
    int (*Write)(void *, uint32_t, uint32_t);
    int (*Safe)(void *);
    int (*Ready)(void *, uint64_t, uint32_t *);
    void (*Pause)(void *);
    void *Context;
    KERNEL_CLOCK Clock;
    uint32_t TimeoutUs, MaxPolls;
} XW_TRANSPORT;

typedef struct { uint32_t Error, Polls, Elapsed, Last; } XW_RESULT;
int xw_wake(const XW_TRANSPORT *transport, uint64_t token, uint64_t code,
    uint32_t target_id, uint32_t bsp_id, XW_RESULT *result);
#endif
