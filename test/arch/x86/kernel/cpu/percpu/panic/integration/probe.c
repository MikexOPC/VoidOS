#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "kernel/diagnostics/log.h"
void xp_panic_test_fault(uint32_t mode);

_Alignas(8) volatile uint64_t xp_panic_test_mode = KERNEL_TEST_SMP_PANIC;

// ====== xp_panic_integration_probe ======

void xp_panic_integration_probe(void) {
    uint64_t mode = xp_panic_test_mode;
    if (mode == 10) {
        const X86_CPU_IDENTITIES *identities = x86_cpu_identities();
        if (!identities || identities->Count < 2) kernel_panic("PANIC_ROLLBACK_REQUIRES_AP");
        static XP_PANIC_RECORD scratch;
        volatile XP_PANIC_RECORD *dirty = (volatile XP_PANIC_RECORD *)(X86_AP_BASE + X86_AP_STRIDE + 48 * 4096 + 256);
        dirty->Reserved[0] = 1;
        if (xp_panic_native_prepare(x86_uart_lapic_mapping()) || xp_panic_native.Count || xp_panic_value(XP_PANIC_READY) ||
            dirty->Reserved[0] != 1 || !xp_panic_record_initialize(&scratch, 0) || !xp_panic_record_retire(&scratch))
            kernel_panic("PANIC_ROLLBACK_FAILED");
        dirty->Reserved[0] = 0;
        if (!kernel_log(0, "KERNEL_PANIC_PREPARE_ROLLBACK_OK")) kernel_panic("PANIC_ROLLBACK_LOG_FAILED");
    }
    if (!xp_panic_native_prepare(x86_uart_lapic_mapping())) kernel_panic("PANIC_PREPARE_FAILED");
    if (!kernel_log(0, "KERNEL_PANIC_READY")) xp_panic_quiet_stop(0x50414e49434c4f47ULL);
    if (mode == 1 || mode == 10) xp_panic_quiet_stop(0x50414e4943505245ULL);
    if (mode == 2) kernel_panic((const char *)(uintptr_t)UINTPTR_MAX);
    if (mode == 5) {
        uint32_t start, now, icr;
        if (!xp_panic_hw_now(&xp_panic_native.Source, &start)) kernel_panic("PANIC_NMI_CLOCK");
        for (uint32_t poll = 0; poll < xp_panic_native.Transport.MaxPolls; ++poll) {
            if (!xp_panic_hw_now(&xp_panic_native.Source, &now) ||
                ((now - start) & xp_panic_native.Source.Mask) >= xp_panic_native.Transport.Ticks) break;
            if (!xp_panic_hw_read(NULL, 0x300, &icr)) break;
            if (!(icr & 0x1000)) {
                uint64_t bits = xp_panic_nmi_bits(xp_panic_native.Ids[0]);
                if (!xp_panic_hw_write(NULL, 0x310, (uint32_t)(bits >> 32)) ||
                    !xp_panic_hw_write(NULL, 0x300, (uint32_t)bits)) break;
                xp_panic_quiet_stop(0x50414e4943574149ULL);
            }
            ks_pause();
        }
        kernel_panic("PANIC_SELF_NMI_FAILED");
    }
    if (mode == 6) {
        uint64_t base = 0;
        for (uint32_t byte = 0; byte < 8; ++byte) base |= (uint64_t)xp_panic_native.BspIdtr.Bytes[byte + 2] << (byte * 8);
        ((volatile unsigned char *)base)[13 * 16 + 5] &= 0x7f;
        xp_panic_test_fault(3);
    }
    if (mode == 7) xp_panic_test_fault(4);
    if (mode == 11) xp_panic_test_fault(5);
    if (mode == 9) {
        static KERNEL_LOCK lock;
        KERNEL_LOCK_GUARD guard = {0};
        if (!kernel_lock_initialize(&lock, 300) || !kernel_lock_acquire(&lock, kernel_lock_current_context(), &guard))
            kernel_panic("PANIC_LOCK_SETUP_FAILED");
        kernel_panic((const char *)(uintptr_t)UINTPTR_MAX);
    }
    xp_panic_test_fault(mode == 4 ? 2 : 1);
}
