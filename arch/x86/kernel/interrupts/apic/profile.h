#ifndef OS_X86_APIC_PROFILE_H
#define OS_X86_APIC_PROFILE_H
#include "kernel/boot/contract.h"
#include <stddef.h>
#include "kernel/firmware/acpi/madt/topology.h"
typedef struct {
    BOOT_U64 ApicBase, Pat, Rflags;
    BOOT_U32 Leaf1Ebx, Leaf1Ecx, Leaf1Edx, MaxLeaf, PhysicalBits, Reserved;
} X86_APIC_RAW;
typedef struct {
    X86_APIC_RAW Raw;
    BOOT_U64 Lapic, CacheBits;
    BOOT_U32 Uid, InitialId, UcIndex, Ready;
} X86_APIC_PROFILE;
_Static_assert(sizeof(X86_APIC_RAW) == 48, "Layout lettore assembly APIC");
_Static_assert(offsetof(X86_APIC_RAW, Pat) == 8 && offsetof(X86_APIC_RAW, Rflags) == 16, "Offset MSR e flags");
_Static_assert(offsetof(X86_APIC_RAW, Leaf1Ebx) == 24 && offsetof(X86_APIC_RAW, Leaf1Ecx) == 28 &&
               offsetof(X86_APIC_RAW, Leaf1Edx) == 32 && offsetof(X86_APIC_RAW, MaxLeaf) == 36, "Offset CPUID");
_Static_assert(offsetof(X86_APIC_RAW, PhysicalBits) == 40 && offsetof(X86_APIC_RAW, Reserved) == 44, "Offset limite fisico");
_Static_assert(sizeof(X86_APIC_PROFILE) == 80, "Profilo APIC bounded");
BOOT_U32 x86_apic_mode(BOOT_U64 base, BOOT_U32 ecx, BOOT_U32 edx, BOOT_U32 bits);
BOOT_U32 x86_apic_uc_slot(BOOT_U64 pat);
BOOT_U64 x86_apic_cache_bits(BOOT_U32 index);
int x86_apic_read_profile(X86_APIC_RAW *output);
int x86_apic_enable_pat_wc(void);
int x86_apic_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_RAW *raw,
                   BOOT_U32 bits, X86_APIC_PROFILE *output);
int x86_apic_profile_valid(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, BOOT_U32 bits);
int x86_apic_initialize(const KERNEL_MADT_TOPOLOGY *topology, BOOT_U32 bits, X86_APIC_PROFILE *output);
#endif
