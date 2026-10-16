#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86_PS2_SESSION owner;
static uint64_t bytes[256];
static uint32_t count, offset, yields, checks;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== boundary_mocks ======

X86_PS2_SESSION *x86_ps2_session(void) { return &owner; }
uint64_t x86_scheduler_generation(void) { return 11; }
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t source, uint64_t token,
    uint32_t consumer, uint64_t generation)
{
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

// ====== run_case ======

static void run_case(uint32_t scenario)
{
    memset(&owner,0,sizeof(owner));
    owner.Mouse.Binding = 7;
    offset = yields = 0;
    count = 3;
    bytes[0] = KERNEL_PAYLOAD_DATA | 8;
    bytes[1] = KERNEL_PAYLOAD_DATA | 1;
    bytes[2] = KERNEL_PAYLOAD_DATA | 2;
    if (scenario == 1) count = 2;
    if (scenario == 2) { bytes[count++] = KERNEL_PAYLOAD_LOSS | (3ULL << 32); }
    if (scenario == 3) bytes[0] = KERNEL_PAYLOAD_DATA;
    if (scenario == 4) bytes[0] = KERNEL_PAYLOAD_DATA | 0xc8;
    if (scenario == 5) {
        count = 255;
        for (uint32_t index = 0; index < count; ++index)
            bytes[index] = KERNEL_PAYLOAD_DATA | (index % 3 == 0 ? 8U : 1U);
    }
    x86_ps2_mouse_task();
    CHECK(owner.Mouse.Done && owner.Mouse.Generation == 11 && !owner.Mouse.Decoder.State && !owner.Mouse.Buttons);
    CHECK(offset == count && yields == (scenario == 5 ? 2U : scenario <= 1 ? 0U :
        scenario == 3 ? 3U : 1U));
    CHECK(owner.Mouse.Events == (scenario == 5 ? 85U : scenario == 0 || scenario == 2 ? 1U : 0U));
    CHECK(owner.Mouse.Losses == (scenario == 2 ? 1U : 0U));
    CHECK(owner.Failed == (scenario >= 2 && scenario <= 4 ? 1U : 0U));
}

// ====== mouse_task_host_main ======

int main(void)
{
    for (uint32_t iteration = 0; iteration < 1000; ++iteration)
        for (uint32_t scenario = 0; scenario < 6; ++scenario) run_case(scenario);
    printf("PASS: mouse task %u checks, 6000 lifecycles including 85-packet bursts; IRQ/payload/output MOCK, actual C/Rust decoder\n",checks);
    return 0;
}
