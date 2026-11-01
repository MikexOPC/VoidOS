#include "transport.h"

typedef struct {
    const XW_TRANSPORT *Transport;
    XW_RESULT *Result;
    uint32_t Limit;
    uint64_t Token;
} XW_WAIT;

// ====== fail_private ======

static int fail_private(XW_WAIT *wait, uint32_t error) {
    wait->Result->Error = error;
    return 0;
}

// ====== poll_private ======

static int poll_private(XW_WAIT *wait) {
    const XW_TRANSPORT *t = wait->Transport;
    XW_RESULT *r = wait->Result;
    uint32_t now;
    if (r->Polls >= t->MaxPolls) return fail_private(wait, XW_POLLS);
    ++r->Polls;
    if (xs_snapshot() != (wait->Token | XS_ARMED)) return fail_private(wait, XW_STATE);
    if (!t->Safe(t->Context)) return fail_private(wait, XW_GUARD);
    if (!kernel_clock_now(&t->Clock, &now)) return fail_private(wait, XW_CLOCK);
    uint32_t delta = (now - r->Last) & t->Clock.Mask;
    if (delta >= t->Clock.Mask / 2) return fail_private(wait, XW_CLOCK);
    r->Last = now;
    if (delta >= wait->Limit - r->Elapsed) return fail_private(wait, XW_DEADLINE);
    r->Elapsed += delta;
    return 1;
}

// ====== read_private ======

static int read_private(XW_WAIT *wait, uint32_t offset, uint32_t *value) {
    const XW_TRANSPORT *t = wait->Transport;
    if (!poll_private(wait)) return 0;
    return t->Read(t->Context, offset, value) || fail_private(wait, XW_IO);
}

// ====== write_private ======

static int write_private(XW_WAIT *wait, uint32_t offset, uint32_t value) {
    const XW_TRANSPORT *t = wait->Transport;
    if (!poll_private(wait)) return 0;
    return t->Write(t->Context, offset, value) || fail_private(wait, XW_IO);
}

// ====== idle_private ======

// Intel SDM 253668-078US 11.6.1: ICR low +300h, delivery status bit 12.
static int idle_private(XW_WAIT *wait) {
    uint32_t value;
    do {
        if (!read_private(wait, 0x300, &value)) return 0;
        if (!(value & 0x1000)) return 1;
        wait->Transport->Pause(wait->Transport->Context);
    } while (wait->Result->Polls < wait->Transport->MaxPolls);
    return fail_private(wait, XW_POLLS);
}

// ====== errors_private ======

// Intel SDM 078US 11.5.3: ESR +280h must be written before reading its captured errors.
static int errors_private(XW_WAIT *wait) {
    uint32_t value;
    if (!write_private(wait, 0x280, 0) || !read_private(wait, 0x280, &value)) return 0;
    return !value || fail_private(wait, XW_APIC);
}

// ====== send_private ======

// Intel SDM 078US 11.6.1 Figure 11-12: destination [63:56], physical/no shorthand.
// Level bit14=1, trigger bit15=0; INIT mode101, SIPI mode110; vector[7:0].
static int send_private(XW_WAIT *wait, uint32_t id, uint32_t command) {
    return idle_private(wait) && errors_private(wait) &&
        write_private(wait, 0x310, id << 24) && write_private(wait, 0x300, command) &&
        idle_private(wait) && errors_private(wait);
}

// ====== delay_private ======

static int delay_private(XW_WAIT *wait, uint32_t ticks) {
    uint32_t start = wait->Result->Elapsed;
    while (wait->Result->Elapsed - start < ticks) {
        if (!poll_private(wait)) return 0;
        wait->Transport->Pause(wait->Transport->Context);
    }
    return 1;
}

// ====== ready_private ======

static int ready_private(XW_WAIT *wait, uint64_t token, uint32_t *ready) {
    const XW_TRANSPORT *t = wait->Transport;
    if (!poll_private(wait)) return 0;
    if (!t->Ready(t->Context, token, ready)) return fail_private(wait, XW_IO);
    if (*ready > 1 || xs_snapshot() != (token | XS_ARMED)) return fail_private(wait, XW_STATE);
    return 1;
}

// ====== run_private ======

static int run_private(XW_WAIT *wait, uint64_t token, uint64_t code, uint32_t id) {
    const XW_TRANSPORT *t = wait->Transport;
    uint32_t ready, after;
    if (!t->Safe(t->Context)) return fail_private(wait, XW_GUARD);
    if (!kernel_clock_now(&t->Clock, &wait->Result->Last)) return fail_private(wait, XW_CLOCK);
    // Intel SDM 078US 9.4.4.1 Table 9-1: INIT, 10ms, SIPI, up to 200us, optional second SIPI.
    if (!send_private(wait, id, 0x4500) ||
        !delay_private(wait, kernel_clock_us(&t->Clock, 10000)) ||
        !send_private(wait, id, 0x4600U | (uint32_t)(code >> 12))) return 0;
    after = wait->Result->Elapsed;
    uint32_t gap = kernel_clock_us(&t->Clock, 200);
    do {
        if (!ready_private(wait, token, &ready)) return 0;
        if (ready) return 1;
        t->Pause(t->Context);
    } while (wait->Result->Elapsed - after < gap);
    if (!send_private(wait, id, 0x4600U | (uint32_t)(code >> 12))) return 0;
    do {
        if (!ready_private(wait, token, &ready)) return 0;
        if (ready) return 1;
        t->Pause(t->Context);
    } while (wait->Result->Polls < t->MaxPolls);
    return fail_private(wait, XW_POLLS);
}

// ====== xw_wake ======

int xw_wake(const XW_TRANSPORT *t, uint64_t token, uint64_t code,
    uint32_t target_id, uint32_t bsp_id, XW_RESULT *result) {
    if (!result || ((uintptr_t)result & 3) ||
        (uintptr_t)result > UINTPTR_MAX - (sizeof(*result) - 1)) return 0;
    if (t) {
        uintptr_t a = (uintptr_t)result, b = (uintptr_t)t;
        if (b > UINTPTR_MAX - (sizeof(*t) - 1) ||
            (a <= b ? b - a < sizeof(*result) : a - b < sizeof(*t))) return 0;
    }
    *result = (XW_RESULT){XW_INVALID, 0, 0, 0};
    if (!t || !t->Read || !t->Write || !t->Safe || !t->Ready || !t->Pause ||
        !kernel_clock_valid(&t->Clock) || t->MaxPolls == 0 || t->MaxPolls > 1000000 ||
        target_id >= 255 || bsp_id >= 255 || target_id == bsp_id ||
        code < 4096 || code > 0xff000 || (code & 4095) || !token || (token & 7) ||
        xs_snapshot() != (token | XS_COMMITTED)) return 0;
    uint32_t limit = kernel_clock_us(&t->Clock, t->TimeoutUs);
    uint32_t minimum = kernel_clock_us(&t->Clock, 10200);
    if (!limit || limit == UINT32_MAX || minimum == UINT32_MAX || limit <= minimum) return 0;
    if (!xs_advance(token, XS_COMMITTED)) return 0;
    *result = (XW_RESULT){0, 0, 0, 0};
    XW_WAIT wait = {t, result, limit, token};
    if (run_private(&wait, token, code, target_id)) return 1;
    (void)xs_quarantine(token);
    return 0;
}
