#include <stdio.h>
#include <stdlib.h>
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "arch/x86/kernel/interrupts/apic/icr/icr.h"
#include "test/kernel/console/video/ap/channel.h"

#define CHECK(expression) do { if (!(expression)) { printf("FAIL native: %d\n",__LINE__); return 1; } } while (0)
XP_PANIC_NATIVE xp_panic_native;
KCA_RECORD kca_record;
static X86_IRQ_REGISTRY registry;
static X86_LAPIC_MAPPING mapping;
static X86_IRQ_CALLBACK handler, quiet;
static uint32_t actor, irq, task, now, sent, pending, posted, bound, event_bound, quiesced = 1;
static uint32_t bad_esr, stop_clock, bad_eoi, bad_post;
static uint32_t error;

// ====== native_context_and_hardware_model ======

uint32_t xis_native_index(void) { return actor; }
uint64_t ks_irq_state(void) { return irq; }
uint64_t ks_atomic_load_acquire(const uint64_t *value) { return *value; }
void ks_atomic_store_release(uint64_t *value, uint64_t next) { *value = next; }
void ks_pause(void) {}
uint32_t x86_scheduler_index(void) { return task; }
int xat_task_valid(void) { return actor == 1; }
int xis_native_now(void *context, uint32_t *value) {
    (void)context;
    if (stop_clock) return 0;
    *value = ++now;
    return 1;
}
uint32_t xis_mmio_read(uint32_t offset) {
    if (offset == X86_ICR_LOW) return 0;
    if (offset == X86_ICR_ESR) return bad_esr;
    ++error;
    return UINT32_MAX;
}
void xis_mmio_write(uint32_t offset, uint32_t value) {
    if ((offset == X86_ICR_HIGH && value == (37U << 24)) ||
        (offset == X86_ICR_ESR && !value)) return;
    ++error;
}
int xcn_icr_low(uint32_t command) {
    if (actor != 1 || irq || command != 0x4043U) { ++error; return 0; }
    ++sent;
    pending = 1;
    return 1;
}
int x86_lapic_authority(const X86_LAPIC_MAPPING *owner) { return owner == &mapping; }
int x86_lapic_idle(const X86_LAPIC_MAPPING *owner) { return owner == &mapping && !pending; }
int x86_lapic_eoi_vector(const X86_LAPIC_MAPPING *owner, uint32_t vector) {
    if (owner != &mapping || actor || vector != 0x43 || !pending || bad_eoi) return 0;
    pending = 0;
    return 1;
}
uint32_t xad_collect_allowed(uint32_t cpu, uint32_t caller, uint64_t token, uint64_t group) {
    return cpu == 1 && !caller && token == 1 && group == 123 && quiesced;
}

// ====== registry_and_dispatcher_models ======

X86_IRQ_REGISTRY *x86_irq_registry(void) { return &registry; }
int x86_irq_registry_bind(X86_IRQ_REGISTRY *owner, uint32_t vector, X86_IRQ_CALLBACK handle,
    X86_IRQ_CALLBACK is_quiet, X86_IRQ_TOKEN *token) {
    if (owner != &registry || vector != 0x43 || bound || !is_quiet()) return 0;
    handler = handle; quiet = is_quiet;
    token->Id = 42; token->Vector = vector; bound = 1;
    return 1;
}
int x86_irq_registry_unbind(X86_IRQ_REGISTRY *owner, X86_IRQ_TOKEN *token) {
    if (owner != &registry || token->Id != 42 || !bound || !quiet()) return 0;
    *token = (X86_IRQ_TOKEN){0}; bound = 0;
    return 1;
}
uint64_t x86_irq_registry_deliveries(const X86_IRQ_REGISTRY *owner, const X86_IRQ_TOKEN *token) {
    return owner == &registry && token->Id == 42 ? xcn_record.Delivered : UINT64_MAX;
}
uint64_t kernel_scheduler_irq_bind(uint32_t event, uint64_t source) {
    if (actor || event != 7 || source != (uint64_t)(uintptr_t)&xcn_record || event_bound) return 0;
    event_bound = 1;
    return 19;
}
uint32_t kernel_scheduler_irq_post_bound(uint32_t event, uint64_t source, uint64_t generation) {
    if (actor || event != 7 || source != xcn_record.Self || generation != 19 || !event_bound || bad_post) return 0;
    posted = 1;
    return 1;
}
uint32_t kernel_scheduler_irq_unbind(uint32_t event, uint64_t source, uint64_t generation) {
    if (posted || !event_bound || actor || event != 7 || source != xcn_record.Self || generation != 19) return 0;
    event_bound = 0;
    return 1;
}
KERNEL_EVENT_REPLY kernel_scheduler_irq_drain(void) {
    KERNEL_EVENT_REPLY reply = {0};
    posted = 0;
    return reply;
}
int x86_irq_wait(volatile uint32_t *counts, uint32_t goal, uint32_t budget) {
    (void)goal; (void)budget;
    if (pending && bound && !handler()) { counts[1] = 1; return 0; }
    return 1;
}
__declspec(noreturn) void kernel_console_requests_fail_stop(void) { abort(); }

// ====== main ======

int main(int argc, char **argv) {
    unsigned mode = argc == 2 ? (unsigned)(argv[1][0] - '0') : 0;
    xp_panic_native.Lapic = &mapping;
    xp_panic_native.Ids[0] = 37;
    xp_panic_native.Source.Mask = 0xffffff;
    CHECK(xcn_prepare(123));
    CHECK(!xcn_prepare(123) && !xcn_send(0) && !xcn_release_allowed());
    actor = 1;
    CHECK(!xcn_prepare(123) && !xcn_send(2) && !xcn_poll() && !xcn_close(1));
    irq = 512;
    CHECK(!xcn_send(0));
    irq = 0;
    if (mode == 1) bad_esr = 1;
    if (mode == 2) stop_clock = 1;
    if (mode == 1 || mode == 2) {
        CHECK(!xcn_send(0) && !sent && !error);
        puts("PASS: native send failure retains notification, no ICR send");
        return 0;
    }
    CHECK(xcn_send(0));
    task = 1;
#ifdef KERNEL_TEST_AP_NOTIFY_INFLIGHT
    kca_record.Sent[1] = 1000;
#endif
    CHECK(xcn_send(1) && sent == 1);
    actor = 0;
    if (mode == 3) bad_eoi = 1;
    if (mode == 4) bad_post = 1;
    if (mode == 3 || mode == 4) {
        CHECK(!xcn_poll() && !xcn_release_allowed() && pending && !error);
        puts("PASS: failed handler retains hardware ownership");
        return 0;
    }
    CHECK(xcn_poll() && xcn_record.Delivered == 1 && xcn_record.Eois == 1);
    quiesced = 0;
    CHECK(!xcn_close(1) && bound && event_bound);
    quiesced = 1;
#if defined(KERNEL_TEST_AP_NOTIFY_NO_ACK) || defined(KERNEL_TEST_AP_NOTIFY_INFLIGHT)
    CHECK(!xcn_close(1) && xcn_record.Denied && !xcn_release_allowed() && bound && event_bound);
#else
    CHECK(xcn_close(1) && xcn_release_allowed() && !bound && !event_bound && !error);
    CHECK(!xcn_close(1) && !xcn_send(0) && !xcn_poll());
#endif
    puts("PASS: actual native transport and Rust FFI; hardware/registry/dispatcher are MODELS");
    return error ? 1 : 0;
}
