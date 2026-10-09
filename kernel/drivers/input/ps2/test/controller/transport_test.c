#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/drivers/input/ps2/controller/controller.h"

typedef struct {
    uint8_t  Queue[64];
    uint32_t Head, Count, Mode, Pending, Set, Scan;
    uint32_t Calls, Fail, Writes, Tick, Frozen, Busy, Fault, Aux, Resend, BadSet, BadMode, Silent, BadAck;
} MOCK;
static uint32_t checks;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== mock_push ======

static void mock_push(MOCK *mock, uint8_t byte)
{
    CHECK(mock->Count < 64);
    mock->Queue[(mock->Head + mock->Count++) & 63] = byte;
}

// ====== mock_call ======

static int mock_call(MOCK *mock)
{
    return ++mock->Calls != mock->Fail;
}

// ====== mock_read ======

static int mock_read(void *context, uint16_t port, uint8_t *value)
{
    MOCK *mock = context;
    if (!mock_call(mock)) return 0;
    if (port == 0x64) {
        *value = (uint8_t)((mock->Count ? 1 : 0) | (mock->Busy ? 2 : 0) | mock->Fault | mock->Aux);
        return 1;
    }
    CHECK(port == 0x60 && mock->Count);
    *value = mock->Queue[mock->Head]; mock->Head = (mock->Head + 1) & 63; --mock->Count;
    return 1;
}

// ====== mock_write ======

static int mock_write(void *context, uint16_t port, uint8_t byte)
{
    MOCK *mock = context;
    if (!mock_call(mock)) return 0;
    ++mock->Writes;
    if (port == 0x64) {
        switch (byte) {
        case 0xad: mock->Mode |= 0x10; break;
        case 0xa7: mock->Mode |= 0x20; break;
        case 0xae: mock->Mode &= ~0x10U; break;
        case 0x20: mock_push(mock, (uint8_t)(mock->Mode ^ mock->BadMode)); break;
        case 0x60: mock->Pending = 0x60; break;
        case 0xab: mock_push(mock, 0); break;
        default: CHECK(0);
        }
    } else {
        CHECK(port == 0x60);
        if (mock->Pending == 0x60) { mock->Mode = byte; mock->Pending = 0; }
        else if (mock->Silent) { return 1; }
        else if (mock->BadAck) { mock_push(mock, 0x1c); }
        else if (mock->Resend) { --mock->Resend; mock_push(mock, 0xfe); }
        else if (mock->Pending == 0xf0) {
            mock_push(mock, 0xfa);
            if (byte) mock->Set = byte;
            else mock_push(mock, (uint8_t)(mock->BadSet ? 1 : mock->Set));
            mock->Pending = 0;
        } else if (byte == 0xf5) { mock->Scan = 0; mock->Set = 2; mock_push(mock, 0xfa); }
        else if (byte == 0xf4) { mock->Scan = 1; mock_push(mock, 0xfa); }
        else if (byte == 0xf0) { mock->Pending = 0xf0; mock_push(mock, 0xfa); }
        else CHECK(0);
    }
    return 1;
}

// ====== mock_clock ======

static int mock_clock(void *context, uint32_t *ticks)
{
    MOCK *mock = context;
    if (!mock_call(mock)) return 0;
    if (!mock->Frozen) mock->Tick += 17;
    *ticks = mock->Tick;
    return 1;
}

// ====== mock_io ======

static KERNEL_PS2_IO mock_io(MOCK *mock)
{
    KERNEL_PS2_IO io = {mock, mock_read, mock_write, mock_clock, 0xffffff, 100};
    return io;
}

// ====== successful_prepare_close_and_retry ======

static uint32_t successful_prepare_close_and_retry(void)
{
    MOCK mock = {0};
    KERNEL_PS2_CONTROLLER owner = {0};
    KERNEL_PS2_IO io = mock_io(&mock);
    uint32_t calls;
    mock.Mode = 0x47; mock.Scan = 1; mock.Set = 1;
    mock_push(&mock, 0xaa); mock_push(&mock, 0xfa);
    CHECK(kernel_ps2_prepare(&owner, &io));
    CHECK(owner.Phase == KERNEL_PS2_PREPARED && !owner.Error && mock.Mode == 0x34 &&
        mock.Set == 2 && !mock.Scan && !mock.Count);
    calls = mock.Calls;
    CHECK(!kernel_ps2_prepare(&owner, &io) && mock.Calls == calls);
    CHECK(kernel_ps2_close(&owner) && owner.Phase == KERNEL_PS2_CLOSED);
    CHECK(!kernel_ps2_close(&owner));
    return calls;
}

// ====== every_callback_failure_is_reported ======

static void every_callback_failure_is_reported(uint32_t calls)
{
    uint32_t fail;
    for (fail = 1; fail <= calls; ++fail) {
        MOCK mock = {0};
        KERNEL_PS2_CONTROLLER owner = {0};
        KERNEL_PS2_IO io = mock_io(&mock);
        mock.Mode = 0x47; mock.Scan = 1; mock.Set = 1; mock.Fail = fail;
        mock_push(&mock, 0xaa); mock_push(&mock, 0xfa);
        CHECK(!kernel_ps2_prepare(&owner, &io));
        CHECK(owner.Error && (owner.Phase == KERNEL_PS2_CLOSED || owner.Phase == KERNEL_PS2_DIRTY));
        if (owner.Phase == KERNEL_PS2_DIRTY) CHECK(kernel_ps2_close(&owner));
        CHECK(owner.Phase == KERNEL_PS2_CLOSED && (mock.Mode & 0x73) == 0x30);
    }
}

// ====== protocol_faults_and_deadlines ======

static void protocol_faults_and_deadlines(void)
{
    uint32_t scenario;
    for (scenario = 0; scenario < 11; ++scenario) {
        MOCK mock = {0};
        KERNEL_PS2_CONTROLLER owner = {0};
        KERNEL_PS2_IO io = mock_io(&mock);
        if (scenario == 0) mock.Busy = 1;
        if (scenario == 1) { mock.Busy = 1; mock.Frozen = 1; }
        if (scenario == 2) mock.Fault = 0x40;
        if (scenario == 3) mock.Fault = 0x80;
        if (scenario == 4) mock.Aux = 0x20;
        if (scenario == 5) mock.Resend = 3;
        if (scenario == 6) mock.BadSet = 1;
        if (scenario == 7) mock.BadMode = 1;
        if (scenario == 8) for (uint32_t byte = 0; byte < 33; ++byte) mock_push(&mock, 7);
        if (scenario == 9) mock.Silent = 1;
        if (scenario == 10) mock.BadAck = 1;
        CHECK(!kernel_ps2_prepare(&owner, &io));
        CHECK(owner.Error);
        CHECK(mock.Calls < 5000010);
    }
    for (scenario = 0; scenario < 3; ++scenario) {
        MOCK mock = {0};
        KERNEL_PS2_CONTROLLER owner = {0};
        KERNEL_PS2_IO io = mock_io(&mock);
        mock.Resend = scenario; mock.Tick = 0xfffffff0;
        CHECK(kernel_ps2_prepare(&owner, &io));
        CHECK(kernel_ps2_close(&owner));
    }
}

// ====== invalid_contracts_do_not_touch_ports ======

static void invalid_contracts_do_not_touch_ports(void)
{
    MOCK mock = {0};
    KERNEL_PS2_CONTROLLER owner = {0};
    KERNEL_PS2_IO io = mock_io(&mock);
    CHECK(!kernel_ps2_prepare(0, &io)); CHECK(!kernel_ps2_prepare(&owner, 0));
    io.Mask = 0xff; CHECK(!kernel_ps2_prepare(&owner, &io));
    io.Mask = 0xffffff; io.Timeout = 0; CHECK(!kernel_ps2_prepare(&owner, &io));
    io.Timeout = 357956; CHECK(!kernel_ps2_prepare(&owner, &io));
    io.Timeout = 100; io.Read = 0; CHECK(!kernel_ps2_prepare(&owner, &io));
    CHECK(!mock.Calls && !mock.Writes);
    CHECK(!kernel_ps2_start(0) && !kernel_ps2_stop(0) && !kernel_ps2_close(0));
    CHECK(!kernel_ps2_start(&owner) && !kernel_ps2_stop(&owner) && !kernel_ps2_close(&owner));
    CHECK(!mock.Calls && !mock.Writes);
}

// ====== repeated_lifecycle ======

static void repeated_lifecycle(void)
{
    for (uint32_t cycle = 0; cycle < 10000; ++cycle) {
        MOCK mock = {0};
        KERNEL_PS2_CONTROLLER owner = {0};
        KERNEL_PS2_IO io = mock_io(&mock);
        mock.Tick = cycle & 1 ? 0xfffffff0U : 0;
        CHECK(kernel_ps2_prepare(&owner, &io));
        CHECK(kernel_ps2_start(&owner));
        uint32_t calls = mock.Calls;
        CHECK(!kernel_ps2_start(&owner) && mock.Calls == calls);
        CHECK(kernel_ps2_stop(&owner));
        CHECK(owner.Phase == KERNEL_PS2_CLOSED && !owner.Error && !mock.Scan &&
            !mock.Count && mock.Mode == 0x30);
        calls = mock.Calls;
        CHECK(!kernel_ps2_start(&owner) && !kernel_ps2_stop(&owner) &&
            !kernel_ps2_close(&owner) && !kernel_ps2_prepare(&owner, &io));
        CHECK(mock.Calls == calls);
    }
}

// ====== start_stop_and_callback_failures ======

static void start_stop_and_callback_failures(void)
{
    uint32_t start_calls = 0, stop_calls = 0;
    for (uint32_t stage = 0; stage < 2; ++stage) {
        for (uint32_t fail = 0; fail <= (stage ? stop_calls : start_calls); ++fail) {
            MOCK mock = {0};
            KERNEL_PS2_CONTROLLER owner = {0};
            KERNEL_PS2_IO io = mock_io(&mock);
            CHECK(kernel_ps2_prepare(&owner,&io));
            if (stage) CHECK(kernel_ps2_start(&owner));
            uint32_t before = mock.Calls;
            mock.Fail = fail ? before + fail : 0;
            int result = stage ? kernel_ps2_stop(&owner) : kernel_ps2_start(&owner);
            if (!fail) {
                CHECK(result);
                if (stage) {
                    stop_calls = mock.Calls - before;
                    CHECK(!mock.Scan && mock.Mode == 0x30 && owner.Phase == KERNEL_PS2_CLOSED);
                } else {
                    start_calls = mock.Calls - before;
                    CHECK(mock.Scan && mock.Mode == 0x21 && !mock.Count && owner.Phase == KERNEL_PS2_DIRTY);
                    CHECK(kernel_ps2_stop(&owner));
                }
            } else {
                CHECK(!result && owner.Error && owner.Phase == KERNEL_PS2_DIRTY);
                mock.Fail = 0;
                CHECK(kernel_ps2_close(&owner) && mock.Mode == 0x30);
            }
        }
    }
}

// ====== controller_host_main ======

int main(void)
{
    invalid_contracts_do_not_touch_ports();
    every_callback_failure_is_reported(successful_prepare_close_and_retry());
    protocol_faults_and_deadlines();
    start_stop_and_callback_failures();
    repeated_lifecycle();
    printf("PASS: controller %u checks, callback rollback, protocol and deadlines\n", checks);
    return 0;
}
