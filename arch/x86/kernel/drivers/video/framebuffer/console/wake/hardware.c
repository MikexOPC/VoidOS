#include "notify.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/interrupts/apic/icr/icr.h"

// ====== xcn_icr_low ======

// Intel SDM 253668-078US 11.6.1/Fig11-12: UC ICR low300h sends the IPI.
// Fixed delivery10:8=0, physical11=0, assert14=1, edge15=0, vector43h.
// Private transport callback: destination was verified/written before this send.
int xcn_icr_low(uint32_t command) {
    if (ks_irq_state() || xis_native_index() != 1 || command != (0x4000U | XCN_VECTOR) ||
        !ks_atomic_load_acquire(&xcn_record.Active)) return 0;
    *(volatile uint32_t *)(X86_LAPIC_VIRTUAL + X86_ICR_LOW) = command;
    return 1;
}
