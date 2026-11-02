#include "arch/x86/kernel/interrupts/apic/icr/icr.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t  Busy, Esr, FailRead, FailWrite, StopAt;
    uint32_t  Reads, Writes, Polls, Pauses;
    uint32_t  Offsets[16], Values[16];
} MODEL;

// ====== read_private ======

static int read_private(void *device, uint32_t offset, uint32_t *value) {
    MODEL *m = device;
    ++m->Reads;
    if (m->Reads == m->FailRead) return 0;
    if (offset == X86_ICR_LOW) *value = m->Busy ? (--m->Busy, X86_ICR_PENDING) : 0;
    else { assert(offset == X86_ICR_ESR); *value = m->Esr; }
    return 1;
}

// ====== write_private ======

static int write_private(void *device, uint32_t offset, uint32_t value) {
    MODEL *m = device;
    assert(m->Writes < 16);
    m->Offsets[m->Writes] = offset; m->Values[m->Writes] = value;
    ++m->Writes;
    return m->Writes != m->FailWrite;
}

// ====== poll_private ======

static int poll_private(void *budget) {
    MODEL *m = budget;
    ++m->Polls;
    return m->Polls != m->StopAt;
}

// ====== pause_private ======

static void pause_private(void *device) { ++((MODEL *)device)->Pauses; }

// ====== send_private ======

static uint32_t send_private(MODEL *m, uint32_t destination, uint32_t flags) {
    X86_ICR_PORT port = {read_private, write_private, poll_private, pause_private, m, m};
    return x86_icr_send(&port, destination, 0x4400, flags);
}

// ====== main ======

int main(void) {
    MODEL m = {0};
    X86_ICR_PORT bad = {0};
    assert(x86_icr_send(NULL, 1, 0x4400, 0) == X86_ICR_INVALID);
    assert(x86_icr_send(&bad, 1, 0x4400, 0) == X86_ICR_INVALID);
    assert(send_private(&m, 256, 0) == X86_ICR_INVALID && !m.Polls);
    assert(send_private(&m, 1, 4) == X86_ICR_INVALID && !m.Polls);

    // Minimal send: idle check, destination first, then the low half.
    m = (MODEL){0};
    assert(send_private(&m, 189, 0) == X86_ICR_OK);
    assert(m.Reads == 1 && m.Writes == 2 && m.Polls == 3);
    assert(m.Offsets[0] == X86_ICR_HIGH && m.Values[0] == 189U << 24);
    assert(m.Offsets[1] == X86_ICR_LOW && m.Values[1] == 0x4400);

    // Full send: ESR refreshed before and after, idle waited on both sides.
    m = (MODEL){0}; m.Busy = 3;
    assert(send_private(&m, 7, X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT) == X86_ICR_OK);
    assert(m.Pauses == 3 && m.Writes == 4 && m.Offsets[0] == X86_ICR_ESR && m.Values[0] == 0);
    assert(m.Offsets[1] == X86_ICR_HIGH && m.Offsets[2] == X86_ICR_LOW && m.Offsets[3] == X86_ICR_ESR);

    // APIC errors, register faults and a stopped budget are distinct outcomes.
    m = (MODEL){0}; m.Esr = 0x20;
    assert(send_private(&m, 7, X86_ICR_CHECK_ESR) == X86_ICR_APIC && m.Writes == 1);
    for (uint32_t at = 1; at <= 4; ++at) {
        m = (MODEL){0}; m.FailRead = at;
        uint32_t status = send_private(&m, 7, X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT);
        assert(status == X86_ICR_IO);
        m = (MODEL){0}; m.FailWrite = at;
        assert(send_private(&m, 7, X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT) == X86_ICR_IO);
    }
    for (uint32_t at = 1; at <= 8; ++at) {
        m = (MODEL){0}; m.StopAt = at;
        assert(send_private(&m, 7, X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT) == X86_ICR_STOPPED);
        assert(m.Polls == at && m.Reads + m.Writes == at - 1);
    }
    m = (MODEL){0}; m.Busy = UINT32_MAX; m.StopAt = 1000;
    assert(send_private(&m, 7, 0) == X86_ICR_STOPPED && !m.Writes && m.Pauses == 999);
    puts("ICR_HOST_PASS: argument checks, ordering, ESR, register faults and caller budget");
    return 0;
}
