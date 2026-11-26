#include <intrin.h>
#include "lapic.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"

// Intel SDM Vol. 3A §11.12.1: CPUID.01H:ECX[bit 21] = x2APIC support;
// IA32_APIC_BASE (MSR 1Bh): bit 11 = xAPIC global enable (EN), bit 10 = x2APIC enable (EXTD).
// Intel SDM Vol. 3A §11.12.1.2 / Table 11-6: x2APIC MSR = 800h + (mmio_offset >> 4).
// Intel SDM Vol. 3A §11.12.3 & §11.12.9: ICR is a single 64-bit MSR 830h (dest in 63:32,
// low in 31:0, no Delivery Status bit); WRMSR to ICR requires MFENCE; LFENCE beforehand.
#define MSR_IA32_APIC_BASE  0x1BU
#define APIC_BASE_EXTD      (1ULL << 10)
#define APIC_BASE_EN        (1ULL << 11)
#define CPUID_ECX_X2APIC    (1U << 21)
#define X2APIC_MSR_BASE     0x800U
#define X2APIC_MSR_ID       0x802U
#define X2APIC_MSR_EOI      0x80BU
#define X2APIC_MSR_ICR      0x830U

static volatile BOOT_U32 lapic_x2apic_enabled;
static BOOT_U32 x2apic_icr_dest[X86_PERCPU_CAPACITY];

// ====== icr_slot_private ======

static BOOT_U32 icr_slot_private(void)
{
    const X86_CPU_LOCAL *local = x86_percpu_current_local();
    return (local && local->Index < X86_PERCPU_CAPACITY) ? local->Index : 0U;
}

// ====== x86_lapic_x2apic_active ======

int x86_lapic_x2apic_active(void)
{
    return lapic_x2apic_enabled != 0U;
}

// ====== x86_lapic_x2apic_enable ======

int x86_lapic_x2apic_enable(void)
{
    int regs[4] = {0};
    __cpuid(regs, 1);
    if (((BOOT_U32)regs[2] & CPUID_ECX_X2APIC) == 0U)
        return 0;
    BOOT_U64 base = __readmsr(MSR_IA32_APIC_BASE);
    if ((base & APIC_BASE_EN) == 0ULL)
        return 0;
    if ((base & APIC_BASE_EXTD) == 0ULL) {
        __writemsr(MSR_IA32_APIC_BASE, base | APIC_BASE_EXTD);
    }
    BOOT_U64 confirm = __readmsr(MSR_IA32_APIC_BASE);
    if ((confirm & (APIC_BASE_EN | APIC_BASE_EXTD)) != (APIC_BASE_EN | APIC_BASE_EXTD))
        return 0;
    _ReadWriteBarrier();
    lapic_x2apic_enabled = 1U;
    return 1;
}

// ====== x86_lapic_native_read ======

int x86_lapic_native_read(BOOT_U32 offset, BOOT_U32 *value)
{
    if (lapic_x2apic_enabled) {
        if (offset == 0x300U) {
            *value = 0U;
            return 1;
        }
        if (offset == 0x20U) {
            *value = (BOOT_U32)__readmsr(X2APIC_MSR_ID) << 24;
            return 1;
        }
        *value = (BOOT_U32)__readmsr(X2APIC_MSR_BASE + (offset >> 4));
        return 1;
    }
    *value = *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + offset);
    return 1;
}

// ====== x86_lapic_native_write ======

int x86_lapic_native_write(BOOT_U32 offset, BOOT_U32 value)
{
    if (lapic_x2apic_enabled) {
        if (offset == 0x310U) {
            x2apic_icr_dest[icr_slot_private()] = value >> 24;
            return 1;
        }
        if (offset == 0x300U) {
            BOOT_U64 icr = ((BOOT_U64)x2apic_icr_dest[icr_slot_private()] << 32) | (BOOT_U64)value;
            _mm_mfence();
            _mm_lfence();
            __writemsr(X2APIC_MSR_ICR, icr);
            return 1;
        }
        __writemsr(X2APIC_MSR_BASE + (offset >> 4), (BOOT_U64)value);
        return 1;
    }
    *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + offset) = value;
    return 1;
}

// ====== x86_lapic_native_eoi ======

int x86_lapic_native_eoi(void)
{
    if (lapic_x2apic_enabled) {
        __writemsr(X2APIC_MSR_EOI, 0ULL);
        return 1;
    }
    *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + 0xb0) = 0;
    return 1;
}
