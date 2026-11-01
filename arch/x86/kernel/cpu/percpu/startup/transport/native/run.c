#include "native.h"
#include "../../../internal.h"

// ====== failed_private ======

static int failed_private(XN_LAUNCH *m, uint32_t error) {
    m->Error = error; m->State = 3;
    (void)xs_quarantine(m->Token);
    return 0;
}

// ====== poll_private ======

static int poll_private(XN_LAUNCH *m) {
    uint32_t now;
    if (++m->Polls > 1000000 || !xn_safe(m) || !kernel_clock_now(&m->Clock, &now)) return 0;
    uint32_t delta = (now - m->Last) & m->Clock.Mask;
    if (delta >= m->Clock.Mask / 2 || delta >= m->Limit - m->Elapsed) return 0;
    m->Last = now; m->Elapsed += delta;
    uint64_t state = xs_snapshot();
    return (state & ~7ULL) == m->Token && (state & 7) >= XS_ARMED && (state & 7) <= XS_PARKED;
}

// ====== idle_private ======

static int idle_private(XN_LAUNCH *m) {
    uint32_t value;
    while (poll_private(m)) {
        if (!xn_read(m, 0x300, &value)) return 0;
        if (!(value & 0x1000)) return 1;
        xn_pause(m);
    }
    return 0;
}

// ====== nmi_private ======

// SDM 078US 11.5.3/11.6.1: refresh ESR, idle ICR, then one physical NMI.
static int nmi_private(XN_LAUNCH *m) {
    uint32_t errors;
    return idle_private(m) && xn_write(m, 0x280, 0) && xn_read(m, 0x280, &errors) && !errors &&
        xn_write(m, 0x310, m->Group->Cpus[1].ApicId << 24) && xn_write(m, 0x300, 0x4400) &&
        idle_private(m) && xn_write(m, 0x280, 0) && xn_read(m, 0x280, &errors) && !errors;
}

// ====== xn_run ======

int xn_run(XN_LAUNCH *m, int send_nmi) {
    if (!m || m->State != 1 || (send_nmi != 0 && send_nmi != 1) || !xn_safe(m) ||
        !kernel_clock_now(&m->Clock, &m->Last) || !xs_advance(m->Token, XS_PREPARED)) return 0;
    m->State = 2;
    XW_TRANSPORT transport = {xn_read, xn_write, xn_safe, xn_ready, xn_pause, m, m->Clock, 1000000, 1000000};
    if (!xw_wake(&transport, m->Token, m->Image->Record.Code, m->Group->Cpus[1].ApicId,
        m->Group->Cpus[0].ApicId, &m->Wake)) return failed_private(m, 1);
    if (send_nmi && !nmi_private(m)) return failed_private(m, 2);
    while (poll_private(m)) {
        uint64_t phase = x86_percpu_ap_phase(1);
        if (phase == 3) return failed_private(m, 3);
        if (xs_snapshot() == (m->Token | XS_PARKED)) {
            if (!send_nmi || x86_percpu_ap_phase(1) != 2 || xp_panic_value(XP_PANIC_ONLINE) != 3)
                return failed_private(m, 4);
            m->State = 4;
            return 1;
        }
        xn_pause(m);
    }
    return failed_private(m, 5);
}
