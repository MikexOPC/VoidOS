#include "arch/x86/kernel/scheduler/idle/idle.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "arch/x86/kernel/scheduler/persistent/persistent.h"
#include "kernel/diagnostics/log.h"
#include <stdio.h>
#include <stdlib.h>

static X86_LAPIC_TIMER source, foreign;
static uint32_t checks, mode, context = 1, index = KERNEL_SCHEDULER_NONE;
static uint32_t retained = 1, arms, waits, posts, drains, result = X86_WATCHDOG_EVENT;
static uint32_t persistent_runs;
static uint64_t binding_token;
static int bound;
static uint32_t payload_enabled, payload_byte;

// ====== payload_backend_mocks ======

uint64_t x86_scheduler_generation(void) { return 2; }
uint32_t kernel_scheduler_payload_open(uint32_t event, uint64_t identity, uint64_t token, uint32_t consumer)
{
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || token != binding_token ||
        !bound || payload_enabled || consumer != 0) return 0;
    payload_enabled = 1; return 1;
}
uint32_t kernel_scheduler_payload_push(uint32_t event, uint64_t identity, uint64_t token, uint32_t byte)
{
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || token != binding_token ||
        !bound || !payload_enabled || payload_byte || byte != source.Count) return 0;
    ++posts;
    if (mode == 3) return 0;
    payload_byte = byte; return 1;
}
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t identity, uint64_t token,
    uint32_t consumer, uint64_t generation)
{
    uint32_t byte;
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || token != binding_token ||
        !payload_enabled || consumer != 0 || consumer != index || generation != 2) return KERNEL_PAYLOAD_DENIED;
    byte = payload_byte; payload_byte = 0;
    return byte ? KERNEL_PAYLOAD_DATA | byte : KERNEL_PAYLOAD_EMPTY;
}
uint32_t kernel_scheduler_payload_close(uint32_t event, uint64_t identity, uint64_t token)
{
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || token != binding_token ||
        !payload_enabled || payload_byte) return 0;
    payload_enabled = 0; return 1;
}

// ====== binding_backend_mocks ======

uint64_t kernel_scheduler_irq_bind(uint32_t event, uint64_t identity)
{
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || bound) return 0;
    bound = 1; return ++binding_token;
}
// ====== kernel_scheduler_irq_unbind_mock ======

uint32_t kernel_scheduler_irq_unbind(uint32_t event, uint64_t identity, uint64_t token)
{
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || !bound || token != binding_token) return 0;
    bound = 0; return 1;
}
// ====== kernel_scheduler_irq_post_bound_mock ======

uint32_t kernel_scheduler_irq_post_bound(uint32_t event, uint64_t identity, uint64_t token)
{
    if (event != 3 || identity != (uint64_t)(uintptr_t)&source || !bound || token != binding_token) return 0;
    ++posts; return mode != 3;
}
// ====== x86_lapic_idle_mock ======

int x86_lapic_idle(const X86_LAPIC_MAPPING *mapping) { (void)mapping; return mode != 8; }

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Idle check %u failed, mode %u\n",checks,mode); exit(1); }
}

// ====== context_and_owner_mocks ======

uint32_t kernel_scheduler_context_valid(void) { return context; }
uint32_t x86_scheduler_index(void) { return index; }
int x86_scheduler_task_valid(void) { return 1; }
int x86_scheduler_idle_retained_valid(void) { return (int)retained; }
int x86_timer_owner_valid(const X86_LAPIC_TIMER *timer) { return timer == &source; }
int x86_irq_ready(void) { return 1; }
int x86_watchdog_valid(void) { return 1; }
int x86_watchdog_budget_end(void) { return !(mode == 7 && waits == 2); }
BOOT_U32 x86_watchdog_result(void) { return result; }
BOOT_U32 x86_pm_read(unsigned short port) { return port; }
BOOT_U32 x86_pm_elapsed(BOOT_U32 start, BOOT_U32 end, BOOT_U32 mask) { return (end-start)&mask; }
int kernel_text(const char *text) { return text != NULL; }
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(unsigned int level, const char *message) { return level == 0 && message != NULL; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== timer_and_gateway_mocks ======

int x86_timer_arm(X86_LAPIC_TIMER *timer)
{
    check_private(timer == &source && timer->Phase == X86_TIMER_READY && !timer->Active);
    ++arms; timer->Active = 1; timer->Phase = X86_TIMER_ARMED;
    return 1;
}

uint32_t kernel_scheduler_irq_post(uint32_t event)
{
    check_private(event == 3); ++posts;
    return mode != 3;
}

KERNEL_EVENT_REPLY kernel_scheduler_irq_drain(void)
{
    KERNEL_EVENT_REPLY reply = {0};
    ++drains; reply.woken = mode == 4 ? 1 : 2; reply.reply.ready = reply.woken;
    return reply;
}

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t event, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = {0};
    check_private((reason == 3 || reason == 4) && event == 3 &&
        token == (reason == 4 && waits == 2 ? 1U : 0U));
    reply.outcome = reason == 4 ? KERNEL_EVENT_WAITING : 0;
    reply.epoch = waits == 2 ? 1 : 0;
    return reply;
}

// ====== x86_watchdog_wait_mock ======

BOOT_U32 x86_watchdog_wait(const volatile BOOT_U32 *count, BOOT_U32 goal,
    const volatile BOOT_U32 *stop)
{
    ++waits;
    check_private(count == &source.Count && goal == waits + 1 && stop == NULL && arms == waits);
    if (mode == 1) { result = X86_WATCHDOG_TIMEOUT; return 0; }
    source.Count = goal; source.Phase = X86_TIMER_PENDING; source.Active = 1;
    check_private(!x86_scheduler_idle_notify(&foreign));
    if (mode == 2) source.Error = 1;
    check_private(x86_scheduler_idle_notify(&source) == (mode != 2 && mode != 3));
    check_private(!x86_scheduler_idle_notify(&source));
    return 3;
}

// ====== x86_scheduler_run_idle_mock ======

int x86_scheduler_run_idle(void)
{
    uint32_t before_arms = arms, before_waits = waits;
    check_private(!x86_scheduler_idle_notify(NULL));
    check_private(!x86_scheduler_idle_notify(&source)); // Not armed / not waiting.
    context = 0;
    check_private(!x86_scheduler_idle_wait() && arms == before_arms && waits == before_waits);
    context = 1; retained = 0;
    check_private(!x86_scheduler_idle_wait() && arms == before_arms && waits == before_waits);
    retained = 1; index = 1;
    check_private(!x86_scheduler_idle_wait() && arms == before_arms && waits == before_waits);
    index = KERNEL_SCHEDULER_NONE;
    if (!x86_scheduler_idle_wait()) return 0;
    check_private(!x86_scheduler_idle_wait()); // Exactly one hardware wait per session.
    check_private(x86_scheduler_idle_slept() == 3);
    index = 0; x86_scheduler_idle_task(index);
    index = 1; x86_scheduler_idle_task(index);
    index = KERNEL_SCHEDULER_NONE;
    check_private(x86_scheduler_idle_verified());
    return 1;
}

// ====== x86_scheduler_run_persistent_mock ======

int x86_scheduler_run_persistent(void)
{
    ++persistent_runs;
    check_private(x86_scheduler_index() == KERNEL_SCHEDULER_NONE && arms == 1 && waits == 1 && drains == 1);
    return mode != 5;
}

// ====== x86_scheduler_run_idle_repeat_mock ======

int x86_scheduler_run_idle_repeat(void) { return x86_scheduler_run_idle(); }

// ====== main ======

int main(int argc, char **argv)
{
    check_private(argc == 2);
    mode = (uint32_t)strtoul(argv[1],NULL,10);
    check_private(mode <= 8);
    check_private(!x86_scheduler_idle_wait());
    check_private(x86_scheduler_idle_notify(NULL)); // Inactive bridge ignores other workloads.
    check_private(!x86_scheduler_timer_idle_tasks(NULL));
    check_private(!x86_scheduler_timer_idle_tasks(&foreign));
    check_private(!x86_scheduler_timer_idle_repeat(&source));
    source.Count = 1; source.Consumed = 1; source.Phase = X86_TIMER_READY;
    source.Clock.Port = 0x608; source.Clock.Mask = 0xFFFFFF;
    if (mode == 8) {
        check_private(!x86_scheduler_timer_idle_tasks(&source));
        check_private(bound && arms == 1 && waits == 1 && posts == 1 && drains == 1);
        check_private(x86_scheduler_idle_failure() == 6 && persistent_runs == 0);
        check_private(!x86_scheduler_timer_idle_tasks(&source));
        check_private(!x86_scheduler_idle_notify(&foreign));
        printf("PASS: %u idle quiescence rejection checks, hardware MOCK\n",checks);
        return 0;
    }
    check_private(x86_scheduler_timer_idle_tasks(&source) == (mode == 0 || mode >= 6));
    check_private(arms == 1 && waits == 1);
    check_private(!x86_scheduler_timer_idle_tasks(&source));
    check_private(persistent_runs == ((mode == 0 || mode == 5 || mode >= 6) ? 1U : 0U));
    if (mode == 0 || mode == 5 || mode >= 6) {
        check_private(posts == 1 && drains == 1 && x86_scheduler_idle_notify(NULL));
    } else {
        check_private(!x86_scheduler_idle_notify(NULL)); // Failed session retains binding.
        check_private(drains == (mode == 4 ? 1U : 0U));
    }
    if (mode >= 6) {
        source.Consumed = 2; source.Phase = X86_TIMER_READY; source.Active = 0;
        check_private(!x86_scheduler_timer_idle_repeat(NULL));
        check_private(!x86_scheduler_timer_idle_repeat(&foreign));
        check_private(x86_scheduler_timer_idle_repeat(&source) == (mode == 6));
        check_private(arms == 2 && waits == 2 && posts == 2 && drains == 2 && persistent_runs == 1);
        check_private(!x86_scheduler_timer_idle_repeat(&source));
        check_private(x86_scheduler_idle_notify(NULL) == (mode == 6));
        check_private(mode == 6 ? x86_scheduler_idle_failure() == 0 : x86_scheduler_idle_failure() == 5);
    }
    printf("PASS: %u dispatcher idle checks, mode %u, hardware/gateway MOCK\n",checks,mode);
    return 0;
}
