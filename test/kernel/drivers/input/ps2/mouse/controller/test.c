#include "kernel/drivers/input/ps2/mouse/controller.h"
#include "kernel/drivers/input/ps2/mouse/stream/control.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint8_t   Bytes[64];
    uint8_t   Channels[64];
    uint32_t  Head, Count, Mode, Pending, Commands, Tick, Calls, Fail;
    uint32_t  Resend, WrongChannel, BadBat, BadId, BadAck, Silent, Fault;
    uint32_t  BadInterface, BadMode, Frozen, Busy, Set, Scan, Accepted, BadQuery, MouseScan;
} MOCK;

static uint32_t checks;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== push ======

static void push(MOCK *mock, uint8_t byte, uint8_t channel)
{
    uint32_t  slot = (mock->Head + mock->Count) & 63U;
    CHECK(mock->Count < 64);
    mock->Bytes[slot] = byte;
    mock->Channels[slot] = channel;
    ++mock->Count;
}

// ====== read_port ======

static int read_port(void *context, uint16_t port, uint8_t *value)
{
    MOCK *mock = context;
    if (++mock->Calls == mock->Fail) return 0;
    if (port == 0x64) {
        *value = (uint8_t)(mock->Fault | (mock->Busy ? 2U : 0U) |
            (mock->Count ? (1U | ((uint32_t)mock->Channels[mock->Head] << 5)) : 0U));
    } else {
        CHECK(port == 0x60 && mock->Count);
        *value = mock->Bytes[mock->Head];
        mock->Head = (mock->Head + 1) & 63U;
        --mock->Count;
    }
    return 1;
}

// ====== write_port ======

static int write_port(void *context, uint16_t port, uint8_t byte)
{
    MOCK *mock = context;
    if (++mock->Calls == mock->Fail) return 0;
    if (port == 0x64) {
        switch (byte) {
        case 0xa9: push(mock, (uint8_t)mock->BadInterface, 0); break;
        case 0xab: push(mock, 0, 0); break;
        case 0xad: mock->Mode |= 0x10; break;
        case 0xae: mock->Mode &= ~0x10U; break;
        case 0xa7: mock->Mode |= 0x20; break;
        case 0xa8: mock->Mode &= ~0x20U; break;
        case 0x20: push(mock, (uint8_t)(mock->Mode ^ mock->BadMode), 0); break;
        case 0x60: case 0xd4: mock->Pending = byte; break;
        default: CHECK(0);
        }
        return 1;
    }
    CHECK(port == 0x60);
    if (mock->Pending == 0x60) {
        mock->Mode = byte;
        mock->Pending = 0;
    } else if (mock->Pending == 0xd4) {
        static const uint8_t expected[7] = {0xff, 0xf5, 0xe6, 0xea, 0xf2, 0xf4, 0xff};
        uint8_t  channel = (uint8_t)!mock->WrongChannel;
        mock->Pending = 0;
        mock->Mode &= ~0x20U;
        ++mock->Commands;
        if (mock->Silent) return 1;
        if (mock->Resend) {
            --mock->Resend;
            push(mock, 0xfe, channel);
            return 1;
        }
        CHECK(mock->Accepted < 7 && byte == expected[mock->Accepted]);
        ++mock->Accepted;
        push(mock, (uint8_t)(mock->BadAck ? 0xfc : 0xfa), channel);
        switch (byte) {
        case 0xff:
            mock->MouseScan = mock->Count = mock->Head = 0;
            push(mock, 0xfa, channel);
            push(mock, (uint8_t)(mock->BadBat ? 0xfc : 0xaa), channel);
            push(mock, (uint8_t)mock->BadId, channel);
            break;
        case 0xf2: push(mock, (uint8_t)(mock->BadQuery ? 3 : mock->BadId), channel); break;
        case 0xf4: mock->MouseScan = 1; break;
        case 0xf5: mock->MouseScan = 0; break;
        case 0xe6: case 0xea: break;
        default: CHECK(0);
        }
    } else {
        mock->Mode &= ~0x10U;
        push(mock, 0xfa, 0);
        if (mock->Pending == 0xf0) {
            mock->Pending = 0;
            if (byte) mock->Set = byte;
            else push(mock, (uint8_t)mock->Set, 0);
        } else if (byte == 0xf0) mock->Pending = 0xf0;
        else if (byte == 0xf5) { mock->Scan = 0; mock->Set = 2; }
        else if (byte == 0xf4) mock->Scan = 1;
        else CHECK(0);
    }
    return 1;
}

// ====== read_clock ======

static int read_clock(void *context, uint32_t *value)
{
    MOCK *mock = context;
    if (++mock->Calls == mock->Fail) return 0;
    if (!mock->Frozen) ++mock->Tick;
    *value = mock->Tick;
    return 1;
}

// ====== prepared_owner ======

static KERNEL_PS2_CONTROLLER prepared_owner(MOCK *mock)
{
    KERNEL_PS2_CONTROLLER  owner = {0};
    owner.Io.Context = mock;
    owner.Io.Read = read_port;
    owner.Io.Write = write_port;
    owner.Io.Clock = read_clock;
    owner.Io.Mask = 0xffffff;
    owner.Io.Timeout = 100;
    owner.Phase = KERNEL_PS2_PREPARED;
    owner.Mode = mock->Mode = 0x34;
    return owner;
}

// ====== lifecycle_and_retries ======

static void lifecycle_and_retries(void)
{
    uint32_t  iteration;
    for (iteration = 0; iteration < 1000; ++iteration) {
        MOCK  mock = {0};
        KERNEL_PS2_CONTROLLER  owner = prepared_owner(&mock);
        KERNEL_PS2_IO  io = owner.Io;
        owner = (KERNEL_PS2_CONTROLLER){0};
        mock.Mode = 0x47; mock.Scan = 1; mock.Set = 1;
        CHECK(kernel_ps2_prepare(&owner, &io));
        mock.Resend = iteration % 3;
        CHECK(kernel_ps2_mouse_prepare(&owner));
        CHECK(owner.Auxiliary == 1 && owner.Phase == KERNEL_PS2_PREPARED && !owner.Error);
        CHECK(mock.Mode == 0x34 && !mock.Count && mock.Commands == 5 + iteration % 3);
        uint32_t calls = mock.Calls;
        CHECK(!kernel_ps2_mouse_prepare(&owner) && mock.Calls == calls);
        CHECK(kernel_ps2_start(&owner) && mock.Mode == 0x25 && mock.Scan);
        CHECK(kernel_ps2_stop(&owner) && owner.Phase == KERNEL_PS2_CLOSED);
        CHECK(!owner.Auxiliary && !mock.Count && !mock.Scan && mock.Mode == 0x34);
    }
}

// ====== callback_failure_sweep ======

static void callback_failure_sweep(void)
{
    MOCK  baseline = {0};
    KERNEL_PS2_CONTROLLER  owner = prepared_owner(&baseline);
    CHECK(kernel_ps2_mouse_prepare(&owner));
    uint32_t total = baseline.Calls;
    for (uint32_t call = 1; call <= total; ++call) {
        MOCK  mock = {0};
        owner = prepared_owner(&mock);
        mock.Fail = call;
        CHECK(!kernel_ps2_mouse_prepare(&owner) && owner.Error && !owner.Auxiliary);
        if (owner.Phase == KERNEL_PS2_DIRTY) CHECK(kernel_ps2_close(&owner));
        CHECK(owner.Phase == KERNEL_PS2_CLOSED && (mock.Mode & 0x73U) == 0x30U);
    }
}

// ====== protocol_and_deadline_failures ======

static void protocol_and_deadline_failures(void)
{
    for (uint32_t scenario = 0; scenario < 12; ++scenario) {
        MOCK  mock = {0};
        KERNEL_PS2_CONTROLLER  owner = prepared_owner(&mock);
        switch (scenario) {
        case 0: mock.WrongChannel = 1; break;
        case 1: mock.BadBat = 1; break;
        case 2: mock.BadId = 3; break;
        case 3: mock.BadAck = 1; break;
        case 4: mock.Silent = 1; break;
        case 5: mock.Resend = 3; break;
        case 6: mock.BadInterface = 1; break;
        case 7: mock.BadMode = 1; break;
        case 8: mock.Fault = 0x80; break;
        case 9: mock.Fault = 0x40; break;
        case 10: mock.Frozen = 1; mock.Busy = 1; break;
        case 11: mock.BadQuery = 1; break;
        }
        CHECK(!kernel_ps2_mouse_prepare(&owner) && owner.Error && !owner.Auxiliary);
        mock.Fault = mock.BadMode = mock.Busy = mock.Frozen = 0;
        if (owner.Phase == KERNEL_PS2_DIRTY) CHECK(kernel_ps2_close(&owner));
        CHECK(owner.Phase == KERNEL_PS2_CLOSED && !mock.Count);
    }
}

// ====== invalid_owners ======

static void invalid_owners(void)
{
    CHECK(!kernel_ps2_mouse_prepare(0));
    for (uint32_t scenario = 0; scenario < 9; ++scenario) {
        MOCK  mock = {0};
        KERNEL_PS2_CONTROLLER  owner = prepared_owner(&mock);
        switch (scenario) {
        case 0: owner.Phase = KERNEL_PS2_DIRTY; break;
        case 1: owner.Error = 1; break;
        case 2: owner.Reserved = 1; break;
        case 3: owner.Auxiliary = 1; break;
        case 4: owner.Mode = 0x21; break;
        case 5: owner.Io.Read = 0; break;
        case 6: owner.Io.Mask = 0xff; break;
        case 7: owner.Io.Timeout = 0; break;
        case 8: owner.Io.Timeout = 357956; break;
        }
        CHECK(!kernel_ps2_mouse_prepare(&owner) && !mock.Calls);
    }
}

// ====== pair_stream_lifecycle ======

static void pair_stream_lifecycle(void)
{
    uint32_t start_calls = 0, stop_calls = 0;
    for (uint32_t iteration = 0; iteration < 1000; ++iteration) {
        MOCK mock = {0};
        KERNEL_PS2_CONTROLLER owner = prepared_owner(&mock);
        CHECK(kernel_ps2_mouse_prepare(&owner));
        uint32_t calls = mock.Calls;
        CHECK(kernel_ps2_pair_start(&owner));
        start_calls = mock.Calls - calls;
        CHECK(owner.Phase == KERNEL_PS2_DIRTY && mock.Mode == 7 && mock.Scan && mock.MouseScan);
        CHECK(!mock.Count && mock.Accepted == 6);
        push(&mock,0xfa,1); push(&mock,0x12,1); push(&mock,0x23,1);
        calls = mock.Calls;
        CHECK(kernel_ps2_pair_stop(&owner));
        stop_calls = mock.Calls - calls;
        CHECK(owner.Phase == KERNEL_PS2_CLOSED && !owner.Auxiliary && mock.Mode == 0x34);
        CHECK(!mock.Scan && !mock.MouseScan && !mock.Count && mock.Accepted == 7);
    }
    for (uint32_t stage = 0; stage < 2; ++stage) {
        uint32_t count = stage ? stop_calls : start_calls;
        for (uint32_t call = 1; call <= count; ++call) {
            MOCK mock = {0};
            KERNEL_PS2_CONTROLLER owner = prepared_owner(&mock);
            CHECK(kernel_ps2_mouse_prepare(&owner));
            if (stage) {
                CHECK(kernel_ps2_pair_start(&owner));
                push(&mock,0xfa,1); push(&mock,0x12,1); push(&mock,0x23,1);
            }
            mock.Fail = mock.Calls + call;
            CHECK(!(stage ? kernel_ps2_pair_stop(&owner) : kernel_ps2_pair_start(&owner)));
            CHECK(owner.Phase == KERNEL_PS2_DIRTY && owner.Error);
        }
    }
    CHECK(!kernel_ps2_pair_start(NULL) && !kernel_ps2_pair_stop(NULL));
}

// ====== mouse_controller_host_main ======

int main(void)
{
    lifecycle_and_retries();
    pair_stream_lifecycle();
    callback_failure_sweep();
    protocol_and_deadline_failures();
    invalid_owners();
    printf("PASS: mouse controller %u checks, 1000 shared-owner lifecycles, rollback and channel isolation\n", checks);
    return 0;
}
