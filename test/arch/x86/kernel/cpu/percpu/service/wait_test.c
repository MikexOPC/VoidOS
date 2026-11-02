#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "arch/x86/kernel/cpu/percpu/service/service.h"

typedef struct { uint32_t Calls, Mode, Mask; } CLOCK_MODEL;

// ====== clock_private ======

static int clock_private(void *context, uint32_t *now) {
    CLOCK_MODEL *clock = context;
    ++clock->Calls;
    if (clock->Mode == 1) return 0;
    if (clock->Mode == 2) { *now = 0; return 1; }
    if (clock->Mode == 3) { *now = clock->Calls == 1 ? 10 : 9; return 1; }
    if (clock->Mode == 4) { *now = UINT32_MAX; return 1; }
    if ((clock->Mode == 0 || clock->Mode == 5) && clock->Calls == 3) {
        assert(xis_claim(1, 1) == 1);
        assert(xis_complete(1, 1, 1) == 1);
    }
    *now = clock->Mode == 5 ? (clock->Mask - 1 + clock->Calls) & clock->Mask : clock->Calls;
    return 1;
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2);
    uint32_t mode = 0;
    if (!strcmp(argv[1], "clock-error")) mode = 1;
    else if (!strcmp(argv[1], "stopped")) mode = 2;
    else if (!strcmp(argv[1], "backwards")) mode = 3;
    else if (!strcmp(argv[1], "mask")) mode = 4;
    else if (!strcmp(argv[1], "wrap") || !strcmp(argv[1], "wrap32")) mode = 5;
    else if (!strcmp(argv[1], "timeout")) mode = 6;
    else assert(!strcmp(argv[1], "ack") || !strcmp(argv[1], "invalid"));
    assert(xis_configure(4) == 1);
    assert(xis_ready(1, 1) == 1);
    assert(xis_publish(1, 0) == 1);
    CLOCK_MODEL clock = {0, mode, 0xffffffU};
    XIS_WAIT wait = {clock_private, &clock, 0xffffffU, 5, 8, 0};
    if (!strcmp(argv[1], "wrap32")) { clock.Mask = UINT32_MAX; wait.Mask = UINT32_MAX; }
    if (!strcmp(argv[1], "invalid")) {
        assert(xis_wait(1, 1, NULL) == XIS_WAIT_INVALID);
        assert(xis_wait(1, 0, &wait) == XIS_WAIT_INVALID);
        assert(xis_wait(1, XIS_INVALID, &wait) == XIS_WAIT_INVALID);
        assert(xis_wait(4, 1, &wait) == XIS_WAIT_INVALID);
        assert(xis_wait(1, 2, &wait) == XIS_WAIT_INVALID);
        wait.MaxPolls = 0; assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        wait.MaxPolls = 1000001; assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        wait.MaxPolls = 8; wait.Ticks = wait.Mask / 2;
        assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        wait.Ticks = 0; assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        wait.Ticks = 5; wait.Reserved = 1;
        assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        wait.Reserved = 0; wait.Mask = 0xffff;
        assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        wait.Mask = 0xffffff; wait.Read = NULL;
        assert(xis_wait(1, 1, &wait) == XIS_WAIT_INVALID);
        assert(clock.Calls == 0 && xis_value(1, XIS_PHASE) == XIS_READY);
    } else {
        uint32_t expected = mode == 0 || mode == 5 ? XIS_WAIT_ACK : XIS_WAIT_FAILED;
        assert(xis_wait(1, 1, &wait) == expected);
        assert(clock.Calls <= wait.MaxPolls + 1);
        if (expected == XIS_WAIT_FAILED) {
            assert(xis_value(1, XIS_PHASE) == XIS_FAILED);
            assert(xis_publish(1, 0) == XIS_INVALID);
        } else assert(xis_value(1, XIS_ACK) == 1);
    }
    printf("IPI_WAIT_PASS: %s\n", argv[1]);
    return 0;
}
