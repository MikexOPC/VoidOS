#ifndef OS_X86_APIC_PROBE_H
#define OS_X86_APIC_PROBE_H
#include "profile.h"
#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/paging/inventory.h"
typedef struct {
    BOOT_U64 Address, End;
    BOOT_U32 Id, Base, Version, Inputs;
} X86_IO_APIC_CAPACITY;
typedef struct {
    BOOT_U32 LapicId, LapicVersion, IoCount, Ready;
    X86_IO_APIC_CAPACITY Io[16];
} X86_APIC_CAPABILITIES;
_Static_assert(sizeof(X86_APIC_CAPABILITIES) == 528, "Capacita APIC bounded");
BOOT_U32 x86_apic_lapic_version(BOOT_U32 version);
BOOT_U32 x86_apic_io_inputs(BOOT_U32 version);
BOOT_U64 x86_apic_gsi_end(BOOT_U32 base, BOOT_U32 inputs);
int x86_apic_cache_state(void);
int x86_apic_mmio_read(BOOT_U32 offset, BOOT_U32 *output);
int x86_apic_mmio_select(BOOT_U32 value);
// Callback interno: mapping gia verificato, IF zero, prestito non rientrante.
int x86_apic_borrow(X86_PAGE_WINDOW *window, BOOT_U64 physical, BOOT_U64 cache,
    int writable, int (*operation)(void *), void *state);
int x86_apic_mapping_ready(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile);
int x86_apic_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
                     const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
                     const X86_APIC_PROFILE *profile, X86_APIC_CAPABILITIES *output);
#endif
