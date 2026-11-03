#include "native.h"
#include "arch/x86/kernel/interrupts/apic/icr/icr.h"

// ====== xis_mmio_read ======

// Intel SDM 078US 11.4-11.8: 32-bit UC local APIC registers on the pinned mapping.
uint32_t xis_mmio_read(uint32_t offset) {
    if (!ks_atomic_load_acquire(&xis_native.Published) || (offset & 15) ||
        (offset != 0x20 && offset != 0x30 && offset != 0x80 && offset != 0xf0 &&
        !(offset >= 0x100 && offset <= 0x270) && offset != 0x280 && offset != 0x300 &&
        offset != 0x2f0 && !(offset >= 0x320 && offset <= 0x370)))
        xis_native_abort(0x4b34495049524400ULL);
    return *(volatile uint32_t *)(X86_LAPIC_VIRTUAL + offset);
}

// ====== xis_mmio_write ======

// SDM 078US 11.5.1: LVT mask bit16; 11.8.5 EOI +b0=0; 11.9 SVR enable bit8.
// 11.5.3: ESR +280h accepts only the write of 0 that latches errors.
// 11.6.1: ICR high dest31:24; fixed low delivery10:8=0, physical11=0, assert14, edge15=0.
void xis_mmio_write(uint32_t offset, uint32_t value) {
    if (!ks_atomic_load_acquire(&xis_native.Published) || !(
        (offset == 0xb0 && value == 0) || (offset == 0x80 && value == 0) || (offset == 0x280 && value == 0) ||
        (offset == 0xf0 && value == 0x1ff) ||
        ((offset == 0x2f0 || (offset >= 0x320 && offset <= 0x370 && !(offset & 15))) && value == 0x10040) ||
        (offset == 0x310 && !(value & 0xffffffU) && (value >> 24) < 255) ||
        (offset == 0x300 && value == (0x4000 | XIS_VECTOR))))
        xis_native_abort(0x4b34495049575200ULL);
    *(volatile uint32_t *)(X86_LAPIC_VIRTUAL + offset) = value;
}

// ====== xis_native_now ======

int xis_native_now(void *context, uint32_t *now) {
    (void)context;
    return xp_panic_hw_now(&xp_panic_native.Source, now);
}

// ====== xis_native_wait ======

int xis_native_wait(uint32_t index, uint32_t field, uint64_t value) {
    uint32_t start, now;
    if (!index || index >= xis_native.Count || index >= 64 || field > 2 ||
        !xis_native_now(NULL, &start)) return 0;
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        if (!xis_native_now(NULL, &now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
            return 0;
        uint64_t actual = field == 0 ? xis_value(index, XIS_PHASE) :
            ks_atomic_load_acquire(field == 1 ? &xis_native.Cpus[index].Verified : &xis_native.Cpus[index].Entered);
        if (actual == value) return 1;
        ks_pause();
    }
    return 0;
}

// ====== budget_private ======

typedef struct { uint32_t Start, Polls; } XIS_BUDGET;

// One second of PM timer and at most 1,000,000 register accesses for the whole send.
static int budget_private(void *budget) {
    XIS_BUDGET *b = budget;
    uint32_t now;
    if (++b->Polls > 1000000 || !xis_native_now(NULL, &now)) return 0;
    return ((now - b->Start) & xp_panic_native.Source.Mask) < KERNEL_PM_TIMER_HZ;
}

// ====== port_read ======

static int port_read(void *device, uint32_t offset, uint32_t *value) {
    (void)device;
    *value = xis_mmio_read(offset);
    return 1;
}

// ====== port_write ======

static int port_write(void *device, uint32_t offset, uint32_t value) {
    (void)device;
    xis_mmio_write(offset, value);
    return 1;
}

// ====== port_pause ======

static void port_pause(void *device) { (void)device; ks_pause(); }

// ====== xis_native_send ======

// SDM 078US 11.6.1: fixed delivery, physical destination, assert, edge; ESR checked around it.
int xis_native_send(uint32_t index) {
    if (ks_irq_state() || xis_native_index() != 0 || index == 0 || index >= xis_native.Count ||
        xis_value(index, XIS_PHASE) != XIS_READY) return 0;
    XIS_BUDGET budget = {0, 0};
    if (!xis_native_now(NULL, &budget.Start)) return 0;
    X86_ICR_PORT port = {port_read, port_write, budget_private, port_pause, NULL, &budget};
    return x86_icr_send(&port, xp_panic_native.Ids[index], 0x4000 | XIS_VECTOR,
        X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT) == X86_ICR_OK;
}

// ====== xis_native_shootdown ======

int xis_native_shootdown(void) {
    if (!ks_atomic_load_acquire(&xis_native.Published)) return 0;
    uint64_t saved_if = ks_irq_save();
    if (xis_native_index() != 0 || !xis_native_flush_context())
        xis_native_abort(0x4b34544c42524f4fULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index) {
        uint64_t generation = xis_publish_operation(index, 0, XIS_FLUSH_CONTEXT);
        XIS_WAIT wait = {xis_native_now, NULL, xp_panic_native.Source.Mask, KERNEL_PM_TIMER_HZ, 1000000, 0};
        if (!generation || generation == XIS_INVALID || !xis_native_send(index) ||
            xis_wait(index, generation, &wait) != XIS_WAIT_ACK)
            xis_native_abort(0x4b34544c4241434bULL);
    }
    // BSP-only writer: counts flushes acknowledged by every AP, for teardown evidence.
    ks_atomic_store_release(&xis_native.Shootdowns, xis_native.Shootdowns + 1);
    ks_irq_restore(saved_if);
    return 1;
}

// ====== xis_native_shootdowns ======

uint64_t xis_native_shootdowns(void) { return ks_atomic_load_acquire(&xis_native.Shootdowns); }
