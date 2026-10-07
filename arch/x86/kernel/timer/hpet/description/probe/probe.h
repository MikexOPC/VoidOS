#ifndef X86_HPET_READONLY_PROBE_H
#define X86_HPET_READONLY_PROBE_H
#include "../scan.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"

/* Read-only temporary UC/NX/supervisor mapping, revoked before return.
 * Caller supplies immutable owned description and verified kernel inputs. */
int x86_hpet_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const X86_HPET_DESCRIPTION *description);
int x86_hpet_probe_preflight(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *inventory,
    BOOT_U64 physical);
uint32_t x86_hpet_capabilities_valid(uint32_t expected_id, uint32_t id,
    uint32_t period);
#endif
