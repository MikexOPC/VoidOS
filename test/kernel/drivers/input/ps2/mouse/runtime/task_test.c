#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86_PS2_SESSION owner;
static uint64_t interrupt_flag = 0x200;
static uint64_t bytes[1024];
static uint32_t count, offset, yields, checks;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== simulated_preempt_section ======

uint64_t kernel_preempt_section_enter(void)
{
    uint64_t saved_if = interrupt_flag;
    interrupt_flag = 0;
    return saved_if;
}

void kernel_preempt_section_leave(uint64_t saved_if)
{
    assert(!interrupt_flag && saved_if == 0x200);
    interrupt_flag = saved_if;
}

// ====== boundary_mocks ======

X86_PS2_SESSION *x86_ps2_session(void) { return &owner; }
uint64_t x86_scheduler_generation(void) { return 11; }
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t source, uint64_t token,
    uint32_t consumer, uint64_t generation)
{
    CHECK(!interrupt_flag);
    CHECK(event == 6 && source == (uint64_t)&owner.Mouse && token == 7 && consumer == 3 && generation == 11);
    if (offset < count) return bytes[offset++];
    owner.Stop = owner.Quiet = 1;
    return KERNEL_PAYLOAD_EMPTY;
}

// ====== terminal_failure ======

__declspec(noreturn) void kernel_scheduler_fail_stop(void) { abort(); }

// ====== serialized_task_yield ======

void x86_scheduler_yield(void)
{
    CHECK(interrupt_flag == 0x200);
    ++yields;
    if (owner.Stop) owner.Quiet = 1;
}

// ====== no_wait_expected_in_fixture ======

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t operation, uint32_t event, uint64_t epoch)
{
    (void)operation; (void)event; (void)epoch;
    abort();
}

// ====== output_mocks ======

int kernel_text(const char *text) { CHECK(text != NULL); return 1; }
int kernel_number(uint64_t number) { (void)number; return 1; }

// ====== scenario_builders ======

static void put_data(uint32_t value) { bytes[count++] = KERNEL_PAYLOAD_DATA | value; }
static void put_loss(void) { bytes[count++] = KERNEL_PAYLOAD_LOSS | (3ULL << 32); }
static void put_packet(uint32_t header) { put_data(header); put_data(1); put_data(2); }
static void put_garbage(uint32_t amount) { while (amount--) put_data(0); }

typedef struct {
    uint32_t  Events, Losses, Rejected, Saturated, Failed, Yields;
} EXPECTED;

// ====== build_scenario ======

static EXPECTED build_scenario(uint32_t scenario)
{
    const uint32_t limit = X86_PS2_MOUSE_RESYNC_LIMIT;
    EXPECTED want = {0, 0, 0, 0, 0, 0};
    count = 0;
    switch (scenario) {
    case 0: put_packet(8); want.Events = 1; break;
    case 1: put_data(8); put_data(1); break;
    case 2: put_packet(8); put_loss(); put_packet(8);
        want.Events = 2; want.Losses = 1; want.Yields = 1; break;
    case 3: put_data(0); put_packet(8); want.Events = 1; want.Rejected = 1; break;
    case 4: put_packet(0xc8); put_packet(8); want.Events = 1; want.Saturated = 1; break;
    case 5: for (uint32_t index = 0; index < 255; ++index) put_data(index % 3 == 0 ? 8U : 1U);
        want.Events = 85; want.Yields = 2; break;
    case 6: put_data(8); put_data(1); put_loss(); put_packet(8);
        want.Events = 1; want.Losses = 1; want.Yields = 1; break;
    case 7: put_loss(); put_data(1); put_data(2); put_packet(8); put_packet(8); put_packet(8);
        want.Events = 3; want.Losses = 1; want.Rejected = 2; want.Yields = 1; break;
    case 8: put_garbage(limit + 1);
        want.Rejected = limit + 1; want.Failed = 1; want.Yields = 3; break;
    case 9: put_garbage(limit - 1); put_packet(8);
        want.Events = 1; want.Rejected = limit - 1; want.Yields = 2; break;
    case 10: put_garbage(limit - 1); put_packet(8); put_garbage(limit - 1); put_packet(8);
        want.Events = 2; want.Rejected = 2 * (limit - 1); want.Yields = 4; break;
    case 11: for (uint32_t index = 0; index < 200; ++index) put_packet(0xc8);
        want.Saturated = 200; want.Yields = 6; break;
    default: for (uint32_t index = 0; index <= limit; ++index) put_loss();
        want.Losses = limit + 1; want.Failed = 1; want.Yields = limit + 1; break;
    }
    return want;
}

// ====== run_case ======

static void run_case(uint32_t scenario)
{
    EXPECTED want;
    memset(&owner,0,sizeof(owner));
    owner.Mouse.Binding = 7;
    offset = yields = 0;
    want = build_scenario(scenario);
    x86_ps2_mouse_task();
    CHECK(owner.Mouse.Done && owner.Mouse.Generation == 11 && !owner.Mouse.Decoder.State && !owner.Mouse.Buttons);
    CHECK(offset == count);
    CHECK(owner.Mouse.Events == want.Events);
    CHECK(owner.Mouse.Losses == want.Losses);
    CHECK(owner.Mouse.Rejected == want.Rejected);
    CHECK(owner.Mouse.Saturated == want.Saturated);
    CHECK(owner.Failed == want.Failed);
    CHECK(yields == want.Yields);
}

// ====== mouse_task_host_main ======

int main(void)
{
    for (uint32_t iteration = 0; iteration < 1000; ++iteration)
        for (uint32_t scenario = 0; scenario < 13; ++scenario) run_case(scenario);
    printf("PASS: mouse task %u checks, 13000 lifecycles: loss, rejected header, overflow and resync tolerance, failure only after %u consecutive framing errors; IRQ/payload/output MOCK, actual C/Rust decoder\n",
        checks,X86_PS2_MOUSE_RESYNC_LIMIT);
    return 0;
}
