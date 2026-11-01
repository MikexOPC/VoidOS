#include "arch/x86/kernel/cpu/percpu/startup/transport/native/native.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *mode;
static uint32_t tick, phase, phase_reads;
static XAG_GROUP group;
static XB_INSTALLATION image;
static XN_LAUNCH owner;

// ====== clock_private ======

static int clock_private(void *context, uint32_t *value) {
    (void)context;
    if (strcmp(mode, "stopped-clock")) tick += 1000;
    *value = tick & 0xffffff;
    return 1;
}

// ====== modeled_hardware_boundary ======

int xn_safe(void *context) { return context == &owner; }
void xn_pause(void *context) { (void)context; }
int xn_read(void *context, uint32_t offset, uint32_t *value) {
    assert(context == &owner && (offset == 0x280 || offset == 0x300));
    *value = 0;
    return strcmp(mode, "wake-read") != 0;
}
int xn_write(void *context, uint32_t offset, uint32_t value) {
    assert(context == &owner);
    if (offset != 0x300) return 1;
    if (value == 0x4500) { ++owner.InitWrites; return strcmp(mode, "wake-write") != 0; }
    if (value == 0x4608) { ++owner.SipiWrites; return 1; }
    assert(value == 0x4400 && owner.Gate == 1 && !owner.NmiAttempted);
    owner.NmiAttempted = 1;
    if (!strcmp(mode, "nmi-write")) return 0;
    if (!strcmp(mode, "reject")) { phase = 3; return 1; }
    assert(xs_advance(owner.Token, XS_ARMED) && xs_advance(owner.Token, XS_ENTERED) && xs_advance(owner.Token, XS_PRIVATE));
    phase = 2;
    return 1;
}
int xn_ready(void *context, uint64_t token, uint32_t *value) {
    assert(context == &owner && token == owner.Token);
    owner.Gate = 1; *value = 1;
    return 1;
}
uint64_t x86_percpu_ap_phase(uint32_t index) {
    assert(index == 1);
    if (!strcmp(mode, "late-ready") && !phase_reads++) return 0;
    return phase;
}
uint64_t xp_panic_value(uint32_t field) {
    assert(field == XP_PANIC_ONLINE);
    return phase == 2 && strcmp(mode, "bad-online") ? 3 : 1;
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2); mode = argv[1];
    group.Cpus[1].ApicId = 1; group.Header.Count = 4;
    image.Record.Code = 0x8000;
    assert(xs_configure(4));
    owner.Token = xs_reserve(1);
    owner.State = 1; owner.Group = &group; owner.Image = &image;
    owner.Clock = (KERNEL_CLOCK){clock_private, NULL, 0xffffff, 3579545};
    owner.Limit = kernel_clock_us(&owner.Clock, 1000000);
    int send_nmi = strcmp(mode, "no-nmi") != 0;
    int result = xn_run(&owner, send_nmi);
    if (!strcmp(mode, "positive") || !strcmp(mode, "late-ready")) {
        assert(result && owner.State == 4 && !owner.Error && xs_snapshot() == (owner.Token | XS_PARKED));
    } else {
        assert(!result && owner.State == 3);
        uint32_t error = !strcmp(mode, "nmi-write") ? 2 : !strcmp(mode, "reject") ? 3 :
            !strcmp(mode, "bad-online") ? 4 : !strcmp(mode, "no-nmi") ? 5 : 1;
        assert(owner.Error == error);
        assert(xs_snapshot() == (owner.Token | (!strcmp(mode, "bad-online") ? XS_PARKED : XS_QUARANTINED)));
        assert(!xn_run(&owner, send_nmi));
    }
    printf("PASS: native controller %s; real coordinator, modeled I/O and CPU completion\n", mode);
    return 0;
}
