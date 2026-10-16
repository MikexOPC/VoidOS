#ifndef OS_X86_LAPIC_MAPPING_H
#define OS_X86_LAPIC_MAPPING_H
#include "arch/x86/kernel/interrupts/apic/probe.h"
#define X86_LAPIC_VIRTUAL 0x0000700000120000ULL
#define X86_LAPIC_LEAF 288U
typedef struct X86_LAPIC_MAPPING {
    BOOT_U64 Self, Root, Physical, Cache;
    BOOT_U32 Ready, PhysicalBits;
} X86_LAPIC_MAPPING;
_Static_assert(sizeof(X86_LAPIC_MAPPING) == 40, "Owner LAPIC bounded");
_Static_assert(((X86_LAPIC_VIRTUAL >> 12) & 511) == X86_LAPIC_LEAF,
    "Slot dedicato dopo arena e stack");
_Static_assert((X86_LAPIC_VIRTUAL >> 21) == (X86_KERNEL_PAGE_WINDOW >> 21),
    "Stessa PT della vista ricorsiva iniziale");
BOOT_U64 x86_lapic_pte(BOOT_U64 physical, BOOT_U64 cache, BOOT_U32 bits);
int x86_lapic_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, X86_LAPIC_MAPPING *owner);
int x86_lapic_validate(const X86_LAPIC_MAPPING *owner);
int x86_lapic_authority(const X86_LAPIC_MAPPING *owner);
int x86_lapic_unbind(X86_LAPIC_MAPPING *owner);
int x86_lapic_read(const X86_LAPIC_MAPPING *owner, BOOT_U32 offset, BOOT_U32 *value);
int x86_lapic_eoi(const X86_LAPIC_MAPPING *owner);
int x86_lapic_eoi_vector(const X86_LAPIC_MAPPING *owner, BOOT_U32 vector);
int x86_lapic_idle(const X86_LAPIC_MAPPING *owner);
/* ISR empty; IRR may hold only the bank2 bits in pending (vectors 40h..42h). */
int x86_lapic_idle_except(const X86_LAPIC_MAPPING *owner, BOOT_U32 pending);
int x86_lapic_report(const X86_LAPIC_MAPPING *owner);
int x86_lapic_native_read(BOOT_U32 offset, BOOT_U32 *value);
int x86_lapic_native_eoi(void);
#endif
