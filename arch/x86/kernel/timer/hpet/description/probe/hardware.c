#include "probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"

typedef struct {
    BOOT_U32  Id;
    BOOT_U32  Period;
} HPET_RAW;

// ====== read_capabilities ======

/* Intel HPET 1.0a sections 2.3.2, 2.3.4: 32-bit reads at 0x00 and 0x04
 * are permitted; ID occupies bits 31:0, period bits 63:32 in femtoseconds.
 * Re-read the static register to reject inconsistent snapshots. No counter
 * or configuration write and no access across register boundaries. */
static int read_capabilities(void *state)
{
    HPET_RAW *raw = state;
    const volatile BOOT_U32 *registers = (const volatile BOOT_U32 *)x86_window_bytes();
    raw->Id = registers[0];
    raw->Period = registers[1];
    return registers[0] == raw->Id && registers[1] == raw->Period;
}

// ====== x86_hpet_probe ======

int x86_hpet_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const X86_HPET_DESCRIPTION *description)
{
    X86_APIC_RAW cpu = {0};
    HPET_RAW raw = {0, 0};
    BOOT_U32 slot;
    BOOT_U64 cache;
    if (!description || !window || !window->Pool || !x86_page_window_idle_valid(window) ||
        !x86_hpet_description_fields(description->BlockId, 0, description->Physical,
            window->Pool->PhysicalBits, description->PageAttributes) ||
        !x86_hpet_probe_preflight(context, window->Pool, inventory, description->Physical) ||
        !x86_apic_cache_state() || !x86_apic_read_profile(&cpu) || cpu.Reserved ||
        cpu.PhysicalBits != window->Pool->PhysicalBits || (cpu.Rflags & 0x202) != 2) { return 0; }
    /* Existing CPU service verifies PAT state. A UC entry is byte value 0
     * (Intel SDM Vol.3A section 13.12.2, Table 13-10, December 2024);
     * reuse its checked index encoding (PWT bit 3, PCD bit 4, PAT bit 7). */
    slot = x86_apic_uc_slot(cpu.Pat);
    if (!slot || slot > 8) { return 0; }
    cache = x86_apic_cache_bits(slot - 1);
    if (!x86_apic_borrow(window, description->Physical, cache, 0, read_capabilities, &raw) ||
        !x86_hpet_capabilities_valid(description->BlockId, raw.Id, raw.Period) ||
        !x86_paging_inventory_verify(context, window->Pool, inventory)) { return 0; }
    return kernel_section("HPET mapping UC read-only revocato") &&
        kernel_text("KERNEL_HPET_PERIOD_FS: ") && kernel_number(raw.Period) &&
        kernel_text("\nKERNEL_HPET_COMPARATORS: ") && kernel_number(((raw.Id >> 8) & 31) + 1) &&
        kernel_text("\nKERNEL_HPET_PROBE_CACHE: ") && kernel_number(cache) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_HPET_PROBE_OK");
}
