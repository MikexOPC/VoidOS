#include "arch/x86/kernel/cpu/percpu/panic/transport/transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { const char *Mode; uint32_t Time, Reads, Writes, Clocks, Pauses; } MODEL;

// ====== now_private ======

static int now_private(void *context, uint32_t *now) {
    MODEL *m = context; ++m->Clocks;
    if (!strcmp(m->Mode, "clock")) return 0;
    if (strcmp(m->Mode, "stalled")) ++m->Time;
    *now = m->Time; return 1;
}

// ====== read_private ======

static int read_private(void *context, uint32_t offset, uint32_t *value) {
    MODEL *m = context; assert(offset == 0x300); ++m->Reads;
    if (!strcmp(m->Mode, "read")) return 0;
    *value = !strcmp(m->Mode, "busy") || (!strcmp(m->Mode, "pending") && m->Reads < 3) ? 0x1000 : 0;
    return 1;
}

// ====== write_private ======

static int write_private(void *context, uint32_t offset, uint32_t value) {
    MODEL *m = context; ++m->Writes;
    assert(offset == (m->Writes == 1 ? 0x310U : 0x300U));
    assert(value == (m->Writes == 1 ? 189U << 24 : 0x4400U));
    if ((!strcmp(m->Mode, "high") && offset == 0x310) || (!strcmp(m->Mode, "low") && offset == 0x300)) return 0;
    if (offset == 0x300 && (!strcmp(m->Mode, "ok") || !strcmp(m->Mode, "pending") || !strcmp(m->Mode, "wrap")))
        assert(xp_panic_acknowledge(63));
    return 1;
}

// ====== pause_private ======

static void pause_private(void *context) { ++((MODEL *)context)->Pauses; }

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2); const char *mode = argv[1];
    assert(xp_panic_configure(64) && xp_panic_admit(0) == 1);
    if (strcmp(mode, "solo")) assert(xp_panic_admit(63) == 1);
    assert(xp_panic_record_claim(63) && xp_panic_record_publish(63));
    assert(xp_panic_elect(0) == 1);
    MODEL model = {mode, !strcmp(mode, "wrap") ? 0xfffffdU : 0, 0, 0, 0, 0};
    XP_PANIC_TRANSPORT t = {read_private, write_private, now_private, pause_private, &model, 0xffffffU, 32, 16};
    if (!strcmp(mode, "busy") || !strcmp(mode, "timeout")) t.Ticks = 4;
    uint32_t ids[64]; for (uint32_t i = 0; i < 64; ++i) ids[i] = i * 3;
    ids[1] = ids[0]; assert(!xp_panic_transport_run(0, ids, 64, &t)); ids[1] = 3;
    ids[1] = 255; assert(!xp_panic_transport_run(0, ids, 64, &t)); ids[1] = 3;
    t.MaxPolls = 0; assert(!xp_panic_transport_run(0, ids, 64, &t)); t.MaxPolls = 16;
    assert(!xp_panic_transport_run(1, ids, 64, &t) && !model.Reads && !model.Writes && !model.Clocks);
    assert(xp_panic_transport_run(0, ids, 64, &t) && xp_panic_value(XP_PANIC_PHASE) == 2);
    assert(model.Clocks <= 17 && model.Reads <= 16 && model.Writes <= 2);
    uint64_t outcome = xp_panic_value(XP_PANIC_OUTCOME);
    int success = !strcmp(mode, "ok") || !strcmp(mode, "pending") || !strcmp(mode, "wrap") || !strcmp(mode, "solo");
    if (success) assert(!outcome && !xp_panic_value(XP_PANIC_MISSED));
    else {
        assert(xp_panic_value(XP_PANIC_MISSED) == (1ULL << 63) && (outcome & XP_PANIC_MISSING));
        assert(xp_panic_acknowledge(63) && xp_panic_value(XP_PANIC_MISSED) == (1ULL << 63));
    }
    if (!strcmp(mode, "stalled")) assert((outcome & 10) == 10 && model.Pauses != 0);
    if (!strcmp(mode, "busy")) assert((outcome & XP_PANIC_DEADLINE) && !model.Writes);
    if (!strcmp(mode, "clock")) assert((outcome & XP_PANIC_CLOCK_FAILED) && !model.Writes);
    if (!strcmp(mode, "read") || !strcmp(mode, "high") || !strcmp(mode, "low")) assert(outcome & XP_PANIC_TRANSPORT_FAILED);
    assert(xp_panic_nmi_bits(0) == 0x4400 && xp_panic_nmi_bits(254) == (254ULL << 56 | 0x4400));
    assert(xp_panic_nmi_bits(255) == UINT64_MAX && xp_panic_nmi_bits(UINT32_MAX) == UINT64_MAX);
    printf("PANIC_TRANSPORT_PASS: %s; bounded global budget, explicit physical destination, frozen missing mask\n", mode);
    return 0;
}
