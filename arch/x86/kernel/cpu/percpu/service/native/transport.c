#include "native.h"

// ====== xis_mmio_read ======

// Intel SDM 078US 11.4-11.8: 32-bit UC local APIC registers on the pinned mapping.
uint32_t xis_mmio_read(uint32_t offset) {
    if (!ks_atomic_load_acquire(&xis_native.Published) || (offset & 15) ||
        (offset != 0x20 && offset != 0x30 && offset != 0x80 && offset != 0xf0 &&
        !(offset >= 0x100 && offset <= 0x270) && offset != 0x300 &&
        offset != 0x2f0 && !(offset >= 0x320 && offset <= 0x370)))
        xis_native_abort(0x4b34495049524400ULL);
    return *(volatile uint32_t *)(X86_LAPIC_VIRTUAL + offset);
}

// ====== xis_mmio_write ======

// SDM 078US 11.5.1: LVT mask bit16; 11.8.5 EOI +b0=0; 11.9 SVR enable bit8.
// 11.6.1: ICR high dest31:24; fixed low delivery10:8=0, physical11=0, assert14, edge15=0.
void xis_mmio_write(uint32_t offset, uint32_t value) {
    if (!ks_atomic_load_acquire(&xis_native.Published) || !(
        (offset == 0xb0 && value == 0) || (offset == 0x80 && value == 0) ||
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

// ====== idle_private ======

static int idle_private(void) {
    uint32_t start, now;
    if (!xis_native_now(NULL, &start)) return 0;
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        if (!xis_native_now(NULL, &now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
            return 0;
        if (!(xis_mmio_read(0x300) & 0x1000)) return 1;
        ks_pause();
    }
    return 0;
}

// ====== xis_native_send ======

int xis_native_send(uint32_t index) {
    if (ks_irq_state() || xis_native_index() != 0 || index == 0 || index >= xis_native.Count ||
        xis_value(index, XIS_PHASE) != XIS_READY || !idle_private()) return 0;
    xis_mmio_write(0x310, xp_panic_native.Ids[index] << 24);
    xis_mmio_write(0x300, 0x4000 | XIS_VECTOR);
    return idle_private();
}
