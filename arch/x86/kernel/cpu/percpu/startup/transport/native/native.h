#ifndef OS_X86_AP_NATIVE_WAKE_H
#define OS_X86_AP_NATIVE_WAKE_H
#include "../transport.h"
#include "../../commit/commit.h"
#include "../../../admission/group.h"
#include "../../../panic/native/runtime.h"

// ====== sequential_launch_owner ======

typedef struct {
    uint64_t  Self, Token;
    const XJ_GROUP  *Joint;
    XB_INSTALLATION  *Image;
    const XAG_GROUP  *Group;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGE_WINDOW  *Window;
    KERNEL_CLOCK  Clock;
    XW_RESULT  Wake;
    uint32_t  Last, Elapsed, Limit, Polls, Error, Gate, InitWrites, SipiWrites, NmiAttempted, State;
} XN_LAUNCH;
int xn_prepare(XN_LAUNCH *owner, const XJ_GROUP *joint);
int xn_run(XN_LAUNCH *owner, int send_nmi);
int xn_next(XN_LAUNCH *owner);
int xn_private_parked(XN_LAUNCH *owner);
int xn_safe(void *context);
int xn_read(void *context, uint32_t offset, uint32_t *value);
int xn_write(void *context, uint32_t offset, uint32_t value);
int xn_ready(void *context, uint64_t token, uint32_t *value);
void xn_pause(void *context);
uint32_t xn_gate_load(const volatile uint32_t *address);
#endif
