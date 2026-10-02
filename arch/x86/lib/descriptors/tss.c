#include "tss.h"
#include "lib/memory/address_ranges.h"

// ====== durate esterne e intervalli numerici distinti ====== //

static int valid_arguments(const X86_BOOT_TSS *tss, const X86_TSS_DESCRIPTOR *descriptor,
                           uint64_t base, uint64_t privilege, uint64_t exception)
{
    const uint64_t limit = UINT64_C(1) << 47;
    const uint64_t bytes = X86_BOOT_EXCEPTION_STACK_BYTES;
    if (!tss || !descriptor || base < 4096 || (base & 15) || base > limit - sizeof(*tss) ||
        privilege < 4096 || exception < 4096 || (privilege & 4095) || (exception & 4095) ||
        privilege >= limit - bytes || exception >= limit - bytes)
        return 0;
    return !os_address_ranges_overlap((uint64_t)(uintptr_t)tss, sizeof(*tss),
                                      (uint64_t)(uintptr_t)descriptor, sizeof(*descriptor)) &&
           !os_address_ranges_overlap(privilege, bytes, exception, bytes) &&
           !os_address_ranges_overlap(base, sizeof(*tss), privilege, bytes) &&
           !os_address_ranges_overlap(base, sizeof(*tss), exception, bytes);
}

// ====== RSP0 e IST1 senza scritture non allineate ====== //

static uint8_t tss_byte(uint32_t byte, uint64_t privilege, uint64_t exception)
{
    if (byte >= 4 && byte < 12)
        return (uint8_t)((privilege + X86_BOOT_EXCEPTION_STACK_BYTES) >> ((byte - 4) * 8));
    if (byte >= 36 && byte < 44)
        return (uint8_t)((exception + X86_BOOT_EXCEPTION_STACK_BYTES) >> ((byte - 36) * 8));
    if (byte == 102) return 104;
    return 0;
}

// ====== TSS disponibile ring zero senza bitmap I/O ====== //

static uint8_t descriptor_byte(uint32_t byte, uint64_t base)
{
    if (byte == 0) return 103;
    if (byte >= 2 && byte <= 4) return (uint8_t)(base >> ((byte - 2) * 8));
    if (byte == 5) return 0x89;
    if (byte == 7) return (uint8_t)(base >> 24);
    if (byte >= 8 && byte < 12) return (uint8_t)(base >> ((byte - 4) * 8));
    return 0;
}

// ====== preparazione inattiva prima del futuro LTR ====== //

int x86_prepare_boot_tss(X86_BOOT_TSS *tss, X86_TSS_DESCRIPTOR *descriptor,
                         uint64_t tss_base, uint64_t privilege_stack, uint64_t exception_stack)
{
    uint32_t byte;
    if (!valid_arguments(tss, descriptor, tss_base, privilege_stack, exception_stack)) return 0;
    for (byte = 0; byte < sizeof(*tss); ++byte)
        tss->Bytes[byte] = tss_byte(byte, privilege_stack, exception_stack);
    for (byte = 0; byte < sizeof(*descriptor); ++byte)
        descriptor->Bytes[byte] = descriptor_byte(byte, tss_base);
    return x86_validate_boot_tss(tss, descriptor, tss_base, privilege_stack, exception_stack);
}

// ====== verifica esatta del descrittore ancora disponibile ====== //

int x86_validate_boot_tss(const X86_BOOT_TSS *tss, const X86_TSS_DESCRIPTOR *descriptor,
                          uint64_t tss_base, uint64_t privilege_stack, uint64_t exception_stack)
{
    uint32_t byte;
    if (!valid_arguments(tss, descriptor, tss_base, privilege_stack, exception_stack)) return 0;
    for (byte = 0; byte < sizeof(*tss); ++byte)
        if (tss->Bytes[byte] != tss_byte(byte, privilege_stack, exception_stack)) return 0;
    for (byte = 0; byte < sizeof(*descriptor); ++byte)
        if (descriptor->Bytes[byte] != descriptor_byte(byte, tss_base)) return 0;
    return 1;
}
