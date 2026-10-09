#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#include <stdio.h>
#include <stdlib.h>

static X86_LAPIC_TIMER source;
static uint32_t checks, active_index = 1, valid = 1, posts;
static int mode;

// ====== payload_backend_mocks ======

uint32_t kernel_scheduler_payload_open(uint32_t event, uint64_t identity, uint64_t token, uint32_t consumer)
{ return event == 2 && identity == (uint64_t)(uintptr_t)&source && token == 1 && consumer == 0; }
uint32_t kernel_scheduler_payload_push(uint32_t event, uint64_t identity, uint64_t token, uint32_t byte)
{
    if (byte != 1 || !kernel_scheduler_payload_open(event,identity,token,0)) return 0;
    ++posts; return 1;
}
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t identity, uint64_t token,
    uint32_t consumer, uint64_t generation)
{ (void)event; (void)identity; (void)token; (void)consumer; (void)generation; return KERNEL_PAYLOAD_DENIED; }
uint32_t kernel_scheduler_payload_close(uint32_t event, uint64_t identity, uint64_t token)
{ return kernel_scheduler_payload_open(event,identity,token,0); }
extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"IRQ bridge check %u failed\n",checks); exit(1); }
}

// ====== bounded_backend_mocks ======

uint32_t x86_scheduler_index(void) { return active_index; }
uint64_t x86_scheduler_generation(void) { return 2; }
int x86_scheduler_task_valid(void) { return (int)valid; }
uint64_t x86_task_stack_base(uint32_t index) { return UINT64_C(0x700000141000) + index * 73728; }
uint64_t x86_kernel_stack_pointer(void) { return x86_task_stack_base(1) + 32768; }
int x86_scheduler_code_valid(uint64_t target) { return target != 0; }
int x86_timer_owner_valid(const X86_LAPIC_TIMER *timer) { return timer == &source; }
int x86_irq_ready(void) { return 1; }
int x86_scheduler_idle_notify(const X86_LAPIC_TIMER *timer) { (void)timer; return 1; }
int x86_timer_arm(X86_LAPIC_TIMER *timer) { return timer == &source; }
BOOT_U32 x86_pm_read(unsigned short port) { return port; }
BOOT_U32 x86_pm_elapsed(BOOT_U32 start, BOOT_U32 end, BOOT_U32 mask) { return (end - start) & mask; }
uint32_t kernel_scheduler_irq_post(uint32_t event) { ++posts; return event == 2; }
// ====== kernel_scheduler_irq_bind_mock ======

uint64_t kernel_scheduler_irq_bind(uint32_t event, uint64_t identity)
{
    return event == 2 && identity == (uint64_t)(uintptr_t)&source ? 1 : 0;
}
// ====== kernel_scheduler_irq_unbind_mock ======

uint32_t kernel_scheduler_irq_unbind(uint32_t event, uint64_t identity, uint64_t token)
{
    return event == 2 && identity == (uint64_t)(uintptr_t)&source && token == 1;
}
// ====== kernel_scheduler_irq_post_bound_mock ======

uint32_t kernel_scheduler_irq_post_bound(uint32_t event, uint64_t identity, uint64_t token)
{
    if (identity != (uint64_t)(uintptr_t)&source || token != 1) return 0;
    ++posts; return event == 2;
}
// ====== x86_lapic_idle_mock ======

int x86_lapic_idle(const X86_LAPIC_MAPPING *mapping) { (void)mapping; return 1; }
int kernel_text(const char *text) { return text != NULL; }
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(unsigned int level, const char *message) { return level == 0 && message != NULL; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }
KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t event, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = {0};
    (void)reason; (void)event; (void)token;
    return reply;
}

// ====== x86_scheduler_run_irq_mock ======

int x86_scheduler_run_irq(void)
{
    uint64_t rip = (uint64_t)x86_scheduler_irq_compute_begin;
    uint64_t rsp = x86_task_stack_base(1) + 32768;
    check_private(!x86_scheduler_irq_record(0x40,rip,rsp,512));
    check_private(!x86_scheduler_irq_record(0x41,rip,rsp,0));
    check_private(!x86_scheduler_irq_record(0x41,rip,rsp,1536));
    check_private(!x86_scheduler_irq_record(0x41,rip - 1,rsp,512));
    check_private(!x86_scheduler_irq_record(0x41,(uint64_t)x86_scheduler_irq_compute_end,rsp,512));
    check_private(!x86_scheduler_irq_record(0x41,rip,x86_task_stack_base(1),512));
    active_index = 0;
    check_private(!x86_scheduler_irq_record(0x41,rip,rsp,512));
    active_index = 1; valid = 0;
    check_private(!x86_scheduler_irq_record(0x41,rip,rsp,512));
    valid = 1;
    check_private(x86_scheduler_irq_record(0x41,rip,rsp,512));
    check_private(!x86_scheduler_irq_record(0x41,rip,rsp,512));
    check_private(!x86_scheduler_timer_notify(NULL));
    check_private(!x86_scheduler_timer_notify(&source));
    source.Count = 1; source.Phase = X86_TIMER_PENDING; source.Error = 1;
    check_private(!x86_scheduler_timer_notify(&source));
    source.Error = 0;
    check_private(x86_scheduler_timer_notify(&source));
    check_private(!x86_scheduler_timer_notify(&source));
    check_private(posts == 1);
    // Exercise the consumer's verified completion with mocked backend return.
    // The task loop itself and IF/SIMD preservation are proven separately in QEMU.
    if (mode == 1) return 0;
    return 1;
}

// ====== main ======

int main(void)
{
    check_private(!x86_scheduler_timer_tasks(NULL));
    check_private(x86_scheduler_timer_notify(NULL)); // No bound scheduler session.
    check_private(!x86_scheduler_irq_record(0x41,1,1,512));
    source.Phase = X86_TIMER_READY;
    mode = 1;
    check_private(!x86_scheduler_timer_tasks(&source)); // Failed session retains binding.
    check_private(!x86_scheduler_timer_tasks(&source)); // No dangling owner/rebind.
    check_private(!x86_scheduler_timer_notify(NULL));
    printf("PASS: %u IRQ bridge checks, owner/backend MOCK, no hardware IRQ claimed\n",checks);
    return 0;
}
