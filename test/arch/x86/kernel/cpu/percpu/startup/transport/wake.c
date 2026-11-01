#include "arch/x86/kernel/cpu/percpu/startup/transport/transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *Mode;
    uint32_t Time, Reads, Writes, Clocks, Guards, Ready, Ipis, Sipi, Mask;
    uint32_t Values[32], At[32], IpiAt[4];
} MODEL;

// ====== fault_private ======

static int fault_private(const MODEL *m, const char *name, uint32_t count) {
    size_t bytes = strlen(name);
    const char *mode = m->Mode;
    if (!strncmp(mode, "second-", 7)) mode += 7;
    return strncmp(mode, name, bytes) == 0 && mode[bytes] == '-' &&
        strtoul(mode + bytes + 1, NULL, 10) == count;
}

// ====== now_private ======

static int now_private(void *context, uint32_t *value) {
    MODEL *m = context;
    ++m->Clocks;
    if (!strcmp(m->Mode, "clock") || fault_private(m, "clock", m->Clocks)) return 0;
    if (!strcmp(m->Mode, "backward")) --m->Time;
    else if (strcmp(m->Mode, "stalled")) m->Time += 100;
    *value = m->Time & m->Mask;
    return 1;
}

// ====== read_private ======

static int read_private(void *context, uint32_t offset, uint32_t *value) {
    MODEL *m = context;
    ++m->Reads;
    assert(offset == 0x300 || offset == 0x280);
    if (fault_private(m, "read", m->Reads)) return 0;
    if (offset == 0x300) *value = !strcmp(m->Mode, "busy") ? 0x1000U : 0;
    else *value = (!strcmp(m->Mode, "esr-before") ||
        (!strcmp(m->Mode, "esr-after") && m->Ipis)) ? 0x20U : 0;
    return 1;
}

// ====== write_private ======

static int write_private(void *context, uint32_t offset, uint32_t value) {
    MODEL *m = context;
    assert(m->Writes < 32);
    m->Values[m->Writes] = value; m->At[m->Writes] = offset;
    ++m->Writes;
    if (fault_private(m, "write", m->Writes)) return 0;
    if (offset == 0x280) assert(value == 0);
    else if (offset == 0x310) assert(value == 189U << 24);
    else {
        assert(offset == 0x300 && (value == 0x4500 || value == 0x4608));
        assert(m->Ipis < 4);
        m->IpiAt[m->Ipis++] = m->Time & m->Mask;
        if (value == 0x4608) ++m->Sipi;
        if (!strcmp(m->Mode, "state-io")) assert(xs_quarantine(520));
    }
    return 1;
}

// ====== safe_private ======

static int safe_private(void *context) {
    MODEL *m = context;
    ++m->Guards;
    return strcmp(m->Mode, "guard") && !fault_private(m, "guard", m->Guards);
}

// ====== ready_private ======

static int ready_private(void *context, uint64_t token, uint32_t *ready) {
    MODEL *m = context;
    assert(token == 520);
    ++m->Ready;
    if (!strcmp(m->Mode, "ready-io")) return 0;
    if (!strcmp(m->Mode, "ready-invalid")) { *ready = 2; return 1; }
    *ready = strcmp(m->Mode, "never") && (m->Sipi >= (!strncmp(m->Mode, "second", 6) ? 2U : 1U));
    return 1;
}

// ====== pause_private ======

static void pause_private(void *context) { (void)context; }

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2);
    MODEL model = {0}; model.Mode = argv[1];
    model.Mask = !strcmp(model.Mode, "wrap32") ? UINT32_MAX : 0xffffffU;
    if (!strcmp(model.Mode, "wrap")) model.Time = 0xffff00U;
    if (!strcmp(model.Mode, "wrap32")) model.Time = 0xffffff00U;
    XW_TRANSPORT t = {read_private, write_private, safe_private, ready_private, pause_private, &model,
        {now_private, &model, model.Mask, 1000000}, 100000, 10000};
    XW_RESULT result;
    assert(xs_configure(4) == 1);
    uint64_t token = xs_reserve(1);
    assert(token == 520 && xs_advance(token, XS_PREPARED) == 1);
    XW_TRANSPORT saved = t;
    unsigned char before[sizeof(t)];
    memcpy(before, &t, sizeof(t));
    assert(!xw_wake(&t, token, 0x8000, 189, 0, (XW_RESULT *)&t) && memcmp(before, &t, sizeof(t)) == 0);
    assert(!xw_wake(&t, token, 0x8000, 189, 0, NULL));
    t.MaxPolls = 0;
    assert(!xw_wake(&t, token, 0x8000, 189, 0, &result));
    t.MaxPolls = saved.MaxPolls;
    assert(!xw_wake(&t, token, 0, 189, 0, &result));
    assert(!xw_wake(&t, token, 0x8000, 0, 0, &result));
    assert(!xw_wake(&t, token, 0x8000, 255, 0, &result));
    assert(!xw_wake(&t, token, 0x100000, 189, 0, &result));
    assert(!xw_wake(&t, token, 0x8001, 189, 0, &result));
    assert(!xw_wake(&t, token + 512, 0x8000, 189, 0, &result));
    t.TimeoutUs = 10200;
    assert(!xw_wake(&t, token, 0x8000, 189, 0, &result));
    t.TimeoutUs = 100000;
    assert(!model.Clocks && !model.Reads && !model.Writes && xs_snapshot() == (token | XS_COMMITTED));
    int passed = xw_wake(&t, token, 0x8000, 189, 0, &result);
    int expected = !strcmp(model.Mode, "fast") || !strcmp(model.Mode, "second") ||
        !strcmp(model.Mode, "wrap") || !strcmp(model.Mode, "wrap32");
    assert(passed == expected && result.Polls <= t.MaxPolls && model.Reads <= t.MaxPolls && model.Writes <= 12);
    if (expected) {
        assert(!result.Error && xs_snapshot() == (token | XS_ARMED));
        assert(model.Ipis == (!strcmp(model.Mode, "second") ? 3U : 2U));
        assert(((model.IpiAt[1] - model.IpiAt[0]) & model.Mask) >= 10000);
        if (model.Ipis == 3) assert(((model.IpiAt[2] - model.IpiAt[1]) & model.Mask) >= 200);
    } else {
        assert(result.Error && xs_snapshot() == (token | XS_QUARANTINED));
        assert(!xs_advance(token, XS_ARMED) && !xs_cancel(token) && !xs_reserve(2));
        if (!strcmp(model.Mode, "busy") || !strcmp(model.Mode, "never")) assert(result.Error == XW_DEADLINE);
        if (!strcmp(model.Mode, "stalled")) assert(result.Error == XW_POLLS);
        if (!strncmp(model.Mode, "esr-", 4)) assert(result.Error == XW_APIC);
        if (!strncmp(model.Mode, "guard", 5)) assert(result.Error == XW_GUARD);
        if (!strncmp(model.Mode, "clock", 5) || !strcmp(model.Mode, "backward")) assert(result.Error == XW_CLOCK);
        if (!strcmp(model.Mode, "state-io") || !strcmp(model.Mode, "ready-invalid")) assert(result.Error == XW_STATE);
    }
    uint32_t writes = model.Writes;
    assert(!xw_wake(&t, token, 0x8000, 189, 0, &result) && model.Writes == writes);
    printf("PASS: wake %s; reads=%u writes=%u clocks=%u\n", model.Mode, model.Reads, model.Writes, model.Clocks);
    return 0;
}
