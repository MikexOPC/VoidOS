#include "runtime.h"

// ====== xp_panic_hw_read ======

// Intel SDM 078US 11.6.1: ICR low +300h, pending bit 12. The permanent UC mapping was checked before arming.
int xp_panic_hw_read(void *context, uint32_t offset, uint32_t *value) {
    (void)context;
    if (!value || offset != 0x300) return 0;
    *value = *(volatile uint32_t *)(X86_LAPIC_VIRTUAL + offset);
    return 1;
}

// ====== xp_panic_hw_write ======

// Intel SDM 078US 11.6.1: +310h destination [31:24], +300h dispatch; NMI asserted/physical/edge, no shorthand.
int xp_panic_hw_write(void *context, uint32_t offset, uint32_t value) {
    (void)context;
    if ((offset != 0x310 || (value & 0xffffffU) || (value >> 24) >= 255) &&
        (offset != 0x300 || value != 0x4400)) return 0;
    *(volatile uint32_t *)(X86_LAPIC_VIRTUAL + offset) = value;
    return 1;
}

// ====== xp_panic_hw_now ======

// ACPI 6.5 4.8.3.3: PM_TMR dword, 24/32-bit mask already validated and copied from the BSP wait.
int xp_panic_hw_now(void *context, uint32_t *value) {
    const X86_PM_CLOCK *source = context;
    if (!source || !value) return 0;
    *value = x86_pm_read((unsigned short)source->Port) & source->Mask;
    return 1;
}

// ====== xp_panic_hw_pause ======

void xp_panic_hw_pause(void *context) { (void)context; ks_pause(); }

// ====== xp_panic_lapic_held ======

int xp_panic_lapic_held(const X86_LAPIC_MAPPING *lapic) {
    return ks_atomic_load_acquire(&xp_panic_armed) && xp_panic_native.Lapic == lapic;
}

// ====== xp_panic_hw_port_read ======

uint8_t xp_panic_hw_port_read(void *context, uint16_t port) {
    (void)context;
    return port == 0x3fb || port == 0x3fd ? x86_read_port(port) : 255;
}

// ====== xp_panic_hw_port_write ======

int xp_panic_hw_port_write(void *context, uint16_t port, uint8_t value) {
    (void)context;
    if (port != 0x3f8) return 0;
    x86_write_port(port, value);
    return 1;
}

// ====== xp_panic_report ======

void xp_panic_report(uint32_t index) {
    uint32_t status = xp_panic_output_report(index, xp_panic_native.Records[index], &xp_panic_native.Sink);
    ks_atomic_store_release(&xp_panic_output, status);
}
