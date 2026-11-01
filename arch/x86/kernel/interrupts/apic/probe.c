#include "probe.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/mm/inventory.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#define FRAME_MASK 0x000ffffffffff000ULL
#define NX (1ULL << 63)
#define WINDOW_LEAF ((X86_KERNEL_PAGE_WINDOW >> 12) & 511)

// ====== riserve firmware senza promuovere RAM a memoria dispositivi ===== //

static int device_reserved(const KERNEL_BOOT_CONTEXT *context, BOOT_U64 physical, int lapic)
{
    KERNEL_MEMORY_INVENTORY map;
    BOOT_U32 index, found = 0;
    if (!kernel_inventory_memory(context->MemoryMap, context->Info.MemoryMapBytes,
                                    context->Info.MemoryDescriptorBytes, &map)) return 0;
    for (index = 0; index < map.Regions; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
                                  context->Info.MemoryDescriptorBytes, index, &region)) return 0;
        if (!os_address_ranges_overlap(physical, 4096, region.Base, region.Pages * 4096)) continue;
        if ((region.Type != 0 && region.Type != 11) || (region.Attributes & 0x3000) ||
            (region.Attributes && !(region.Attributes & 1)) || physical < region.Base ||
            physical - region.Base > (region.Pages - 1) * 4096 || ++found != 1) return 0;
    }
    // Alcuni firmware omettono queste pagine: MSR LAPIC o IOAPIC standard soltanto.
    return found == 1 || (found == 0 && (lapic || physical == 0xfec00000));
}

// ====== nessun alias fisico ordinario o pagina tabella con cache differente ===== //

static int aliases_absent(const X86_PAGING_INVENTORY *inventory, BOOT_U64 physical)
{
    BOOT_U32 index, slot;
    for (index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        const volatile BOOT_U64 *entries;
        if (record->Physical == physical) return 0;
        if (record->Level != 3) continue;
        entries = x86_paging_view(record->View);
        if (!entries) return 0;
        for (slot = 0; slot < 512; ++slot) {
            if (entries[slot] && (entries[slot] & FRAME_MASK) == physical) return 0;
        }
    }
    return 1;
}

// ====== snapshot CPU aggiornato prima di esporre i registri del controller ===== //

static int cpu_current(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile)
{
    X86_APIC_RAW raw;
    X86_APIC_PROFILE fresh;
    BOOT_U32 index;
    for (index = 0; index < sizeof(raw); ++index) ((volatile unsigned char *)&raw)[index] = 0;
    return x86_apic_cache_state() && x86_apic_read_profile(&raw) &&
           x86_apic_plan(topology, &raw, profile->Raw.PhysicalBits, &fresh) &&
           fresh.Raw.ApicBase == profile->Raw.ApicBase && fresh.Raw.Pat == profile->Raw.Pat &&
           fresh.InitialId == profile->InitialId && fresh.CacheBits == profile->CacheBits;
}

// ====== mapping temporaneo supervisor NX con ownership di prestito Active due ===== //

int x86_apic_borrow(X86_PAGE_WINDOW *window, BOOT_U64 physical, BOOT_U64 cache,
    int writable, int (*operation)(void *), void *state)
{
    volatile BOOT_U64 *entries;
    BOOT_U64 expected;
    BOOT_U64 budget;
    int result;
    if (!operation || !state || (writable != 0 && writable != 1) ||
        !x86_page_window_idle_valid(window) || (physical & 4095) ||
        physical >= (1ULL << window->Pool->PhysicalBits) || (cache & ~0x98ULL)) return 0;
    entries = x86_window_entries(3);
    if (!entries) return 0;
    expected = physical | NX | cache | (writable ? 3 : 1);
    if (!kernel_physical_budget_read(window->Pool, &budget)) return 0;
    window->Active = 2;
    entries[WINDOW_LEAF] = expected;
    x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
    result = (entries[WINDOW_LEAF] & ~0x60ULL) == expected;
    if (result) result = operation(state);
    result = result && (entries[WINDOW_LEAF] & ~0x60ULL) == expected;
    entries[WINDOW_LEAF] = 0;
    x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
    window->Active = 0;
    return result && kernel_physical_budget_matches(window->Pool, budget) && x86_page_window_idle_valid(window);
}

// ====== sole letture dei registri con ripristino del selettore ===== //

typedef struct { int Io; BOOT_U32 *Id, *Version; } READ_REQUEST;
static int read_registers(void *state)
{
    READ_REQUEST *request = state;
    BOOT_U32 saved = 0, restored = 0, valid = 0, selected = 0;
    int result = 1;
    if (request->Io) {
        result = x86_apic_mmio_read(0, &saved);
        if (result) {
            saved &= 255;
            selected = 1;
            result = x86_apic_mmio_select(0) && x86_apic_mmio_read(0x10, request->Id) &&
                     x86_apic_mmio_select(1) && x86_apic_mmio_read(0x10, request->Version);
        }
        if (selected) {
            valid = x86_apic_mmio_select(saved) && x86_apic_mmio_read(0, &restored) &&
                    (restored & 255) == saved;
            result = result && valid;
        }
    } else {
        result = x86_apic_mmio_read(0x20, request->Id) && x86_apic_mmio_read(0x30, request->Version);
    }
    return result;
}
static int read_controller(X86_PAGE_WINDOW *window, BOOT_U64 physical, BOOT_U64 cache,
    int io, BOOT_U32 *id, BOOT_U32 *version)
{
    READ_REQUEST request = {io, id, version};
    return x86_apic_borrow(window, physical, cache, io, read_registers, &request);
}

// ====== conteggi hardware e intervalli GSI distinti per tutti gli IOAPIC ===== //

static int collect(X86_PAGE_WINDOW *window, const KERNEL_MADT_TOPOLOGY *topology,
                     const X86_APIC_PROFILE *profile, X86_APIC_CAPABILITIES *draft)
{
    BOOT_U32 index, previous, id, version;
    if (!read_controller(window, profile->Lapic, profile->CacheBits, 0, &id, &version) ||
        (id >> 24) != profile->InitialId || !x86_apic_lapic_version(version)) return 0;
    draft->LapicId = id >> 24; draft->LapicVersion = version;
    for (index = 0; index < topology->IoCount; ++index) {
        const KERNEL_MADT_IO_APIC *source = &topology->Io[index];
        X86_IO_APIC_CAPACITY *target = &draft->Io[index];
        if (source->Id > 15 || !read_controller(window, source->Address, profile->CacheBits, 1, &id, &version) ||
            ((id >> 24) & 15) != source->Id) return 0;
        target->Address = source->Address; target->Id = source->Id; target->Base = source->GsiBase;
        target->Version = version; target->Inputs = x86_apic_io_inputs(version);
        target->End = x86_apic_gsi_end(target->Base, target->Inputs);
        if (!target->End) return 0;
        for (previous = 0; previous < index; ++previous) {
            if (target->Base < draft->Io[previous].End && draft->Io[previous].Base < target->End) return 0;
        }
    }
    draft->IoCount = topology->IoCount;
    return 1;
}

// ====== override ISA e NMI globali coperti da un solo intervallo hardware ===== //

static int routes_covered(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *draft)
{
    BOOT_U32 index, io;
    for (index = 0; index < topology->IsoCount + topology->NmiCount; ++index) {
        BOOT_U32 gsi, count = 0;
        if (index < topology->IsoCount) gsi = topology->Iso[index].Gsi;
        else {
            const KERNEL_MADT_NMI_SOURCE *nmi = &topology->Nmi[index - topology->IsoCount];
            if (nmi->Kind != 3) continue;
            gsi = nmi->Value;
        }
        for (io = 0; io < draft->IoCount; ++io) {
            if (gsi >= draft->Io[io].Base && gsi < draft->Io[io].End) ++count;
        }
        if (count != 1) return 0;
    }
    return 1;
}

// ====== output persistente disgiunto da tutti gli owner letti e modificati ===== //

static int output_valid(const KERNEL_BOOT_CONTEXT *context, const X86_PAGE_WINDOW *window,
                         const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
                         const X86_APIC_PROFILE *profile, X86_APIC_CAPABILITIES *output)
{
    BOOT_U64 target = (BOOT_U64)output;
    if (!context || !window || !window->Pool || !inventory || !topology || !profile || !output ||
        (target & 7) || target > ~0ULL - sizeof(*output) ||
        context->Info.MemoryMapCapacity < context->Info.MemoryMapBytes ||
        context->Info.MemoryMapCapacity > 1048576) return 0;
    return !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)context, sizeof(*context)) &&
        !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)window, sizeof(*window)) &&
        !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)window->Pool, sizeof(*window->Pool)) &&
        !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)inventory, sizeof(*inventory)) &&
        !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)topology, sizeof(*topology)) &&
        !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)profile, sizeof(*profile)) &&
        !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)context->MemoryMap, context->Info.MemoryMapCapacity) &&
        !os_address_ranges_overlap(target, sizeof(*output), X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(target, sizeof(*output), X86_LAYOUT_RECURSIVE_VIRTUAL, X86_LAYOUT_SLOT_BYTES);
}

// ====== probe completo prima della pubblicazione nessuna rotta IRQ configurata ===== //

static int log_capabilities(const X86_APIC_CAPABILITIES *output)
{
    BOOT_U32 index;
    if (!kernel_section("capacita hardware APIC e MMIO UC") ||
        !kernel_text("KERNEL_APIC_CAPABILITIES: ") || !kernel_number((BOOT_U64)output) ||
        !kernel_text("\nKERNEL_LAPIC_CURRENT_ID: ") || !kernel_number(output->LapicId) ||
        !kernel_text("\nKERNEL_LAPIC_VERSION: ") || !kernel_number(output->LapicVersion) ||
        !kernel_text("\nKERNEL_IO_APIC_COUNT: ") || !kernel_number(output->IoCount) || !kernel_text("\n")) return 0;
    for (index = 0; index < output->IoCount; ++index) {
        const X86_IO_APIC_CAPACITY *io = &output->Io[index];
        if (!kernel_text("KERNEL_IO_APIC_ADDRESS: ") || !kernel_number(io->Address) ||
            !kernel_text("\nKERNEL_IO_APIC_ID: ") || !kernel_number(io->Id) ||
            !kernel_text("\nKERNEL_IO_APIC_BASE: ") || !kernel_number(io->Base) ||
            !kernel_text("\nKERNEL_IO_APIC_END: ") || !kernel_number(io->End) ||
            !kernel_text("\nKERNEL_IO_APIC_INPUTS: ") || !kernel_number(io->Inputs) ||
            !kernel_text("\nKERNEL_IO_APIC_VERSION: ") || !kernel_number(io->Version) || !kernel_text("\n")) return 0;
    }
    return kernel_log(0, "KERNEL_APIC_MMIO_OK");
}

// ====== bozza locale verificata prima del successo del probe completo ===== //

int x86_apic_mapping_ready(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile)
{
    X86_APIC_CAPABILITIES unused;
    BOOT_U32 index;
    if (!output_valid(context, window, inventory, topology, profile, &unused) ||
        !x86_apic_profile_valid(topology, profile, window->Pool->PhysicalBits) ||
        !x86_page_window_idle_valid(window) || !x86_paging_inventory_verify(context, window->Pool, inventory) ||
        !cpu_current(topology, profile)) return 0;
    if (!device_reserved(context, profile->Lapic, 1) || !aliases_absent(inventory, profile->Lapic)) return 0;
    for (index = 0; index < topology->IoCount; ++index) {
        if (!device_reserved(context, topology->Io[index].Address, 0) ||
            !aliases_absent(inventory, topology->Io[index].Address)) return 0;
    }
    return 1;
}

// ====== bozza locale pubblicata dopo controlli condivisi e letture ===== //

int x86_apic_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
                     const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
                     const X86_APIC_PROFILE *profile, X86_APIC_CAPABILITIES *output)
{
    X86_APIC_CAPABILITIES draft;
    BOOT_U32 index;
    BOOT_U64 budget;
    if (!output_valid(context, window, inventory, topology, profile, output) ||
        !x86_apic_mapping_ready(context, window, inventory, topology, profile)) return 0;
    for (index = 0; index < sizeof(draft); ++index) ((volatile unsigned char *)&draft)[index] = 0;
    if (!kernel_physical_budget_read(window->Pool, &budget)) return 0;
    if (!collect(window, topology, profile, &draft) || !routes_covered(topology, &draft) ||
        !kernel_physical_budget_matches(window->Pool, budget) || !x86_page_window_idle_valid(window) ||
        !cpu_current(topology, profile) || !x86_paging_inventory_verify(context, window->Pool, inventory)) return 0;
    draft.Ready = 1;
    for (index = 0; index < sizeof(draft); ++index)
        ((volatile unsigned char *)output)[index] = ((const unsigned char *)&draft)[index];
    return log_capabilities(output);
}
