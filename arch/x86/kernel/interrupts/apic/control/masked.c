#include "masked.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/kernel/platform.h"
typedef struct { const X86_APIC_CAPABILITIES *Caps; BOOT_U32 Index, Verify, Lint[2]; } MASK_REQUEST;

// ====== capacita legate al profilo e alla topologia gia verificati ===== //

static int capacities(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps)
{
    BOOT_U32 index, previous;
    if (!caps || ((BOOT_U64)caps & 7) || (BOOT_U64)caps > ~0ULL - sizeof(*caps) ||
        caps->Ready != 1 || caps->LapicId != profile->InitialId ||
        !x86_apic_lapic_version(caps->LapicVersion) || (caps->LapicVersion & ~0x01ff00ffU) ||
        caps->IoCount != topology->IoCount) return 0;
    for (index = 0; index < caps->IoCount; ++index) {
        const X86_IO_APIC_CAPACITY *io = &caps->Io[index];
        if (io->Address != topology->Io[index].Address || io->Id != topology->Io[index].Id ||
            io->Base != topology->Io[index].GsiBase || io->Id > 15 ||
            io->Inputs != x86_apic_io_inputs(io->Version) || !io->Inputs ||
            io->End != x86_apic_gsi_end(io->Base, io->Inputs) || !io->End) return 0;
        for (previous = 0; previous < index; ++previous)
            if (io->Base < caps->Io[previous].End && caps->Io[previous].Base < io->End) return 0;
    }
    for (; index < 16; ++index) {
        const X86_IO_APIC_CAPACITY *io = &caps->Io[index];
        if (io->Address || io->End || io->Id || io->Base || io->Version || io->Inputs) return 0;
    }
    return 1;
}

// ====== lettura tramite selettore senza alterare le rotte ===== //

static int io_read(BOOT_U32 reg, BOOT_U32 *value)
{
    BOOT_U32 actual;
    return x86_apic_control_write(0, reg) && x86_apic_control_read(0, &actual) &&
        (actual & 255) == reg && x86_apic_control_read(0x10, value);
}

// ====== scrittura di una sola meta con selettore verificato ===== //

static int io_write(BOOT_U32 reg, BOOT_U32 value)
{
    BOOT_U32 actual;
    return x86_apic_control_write(0, reg) && x86_apic_control_read(0, &actual) &&
        (actual & 255) == reg && x86_apic_control_write(0x10, value);
}

// ====== ogni ingresso prima mascherato poi diretto al BSP senza abilitarlo ===== //

static int io_routes(MASK_REQUEST *request)
{
    const X86_IO_APIC_CAPACITY *io = &request->Caps->Io[request->Index];
    BOOT_U32 input, old, low, high;
    for (input = 0; input < io->Inputs; ++input) {
        BOOT_U32 reg = 0x10 + input * 2;
        BOOT_U32 destination = request->Caps->LapicId << 24;
        if (request->Verify == 2) {
            if (!io_read(reg, &low) || (low & 0x5000)) return 0;
            continue;
        }
        if (!request->Verify) {
            // Prima del cambio destinazione nessun ingresso puo rimanere aperto.
            if (!io_read(reg, &old) || !io_write(reg, old | 0x10000) ||
                !io_read(reg, &low) || !(low & 0x10000) ||
                !io_write(reg + 1, destination) || !io_write(reg, 0x10040)) return 0;
        }
        if (!io_read(reg, &low) || !io_read(reg + 1, &high) ||
            (low & ~0x5000U) != 0x10040 || high != destination) return 0;
        // Delivery Status e Remote IRR sono read-only; un residuo richiede stop.
        if (low & 0x5000) return 0;
    }
    return 1;
}

// ====== identita riletta e selettore ripristinato anche sul fallimento ===== //

static int io_mask(void *state)
{
    MASK_REQUEST *request = state;
    BOOT_U32 saved, restored, id, version;
    int result, restore;
    if (!x86_apic_control_read(0, &saved)) return 0;
    saved &= 255;
    result = io_read(0, &id) && io_read(1, &version) &&
        ((id >> 24) & 15) == request->Caps->Io[request->Index].Id &&
        version == request->Caps->Io[request->Index].Version && io_routes(request);
    restore = x86_apic_control_write(0, saved) && x86_apic_control_read(0, &restored) &&
        (restored & 255) == saved;
    return result && restore;
}

// ====== richieste e servizi pendenti mai cancellati con EOI speculativi ===== //

static int lapic_empty(void)
{
    BOOT_U32 index, value;
    for (index = 0; index < 8; ++index) {
        if (!x86_apic_control_read(0x100 + index * 16, &value) || value ||
            !x86_apic_control_read(0x200 + index * 16, &value) || value) return 0;
    }
    return 1;
}

// ====== tutti i LVT presenti hanno vettore legale maschera e modo fisso ===== //

static int lapic_mask(void *state)
{
    MASK_REQUEST *request = state;
    BOOT_U32 id, version, index, value;
    BOOT_U32 maximum = (request->Caps->LapicVersion >> 16) & 255;
    if (!x86_apic_control_read(0x20, &id) || (id >> 24) != request->Caps->LapicId ||
        !x86_apic_control_read(0x30, &version) || version != request->Caps->LapicVersion ||
        !lapic_empty()) return 0;
    for (index = 0; index <= maximum; ++index) {
        BOOT_U32 offset = x86_irq_lvt(maximum, index);
        BOOT_U32 expected = index == 1 ? request->Lint[0] : index == 2 ? request->Lint[1] : 0x10040;
        if (!offset || (!request->Verify && !x86_apic_control_write(offset, expected)) ||
            !x86_apic_control_read(offset, &value) || value != expected) return 0;
    }
    if (!request->Verify && (!x86_apic_control_write(0x80, 0) ||
        !x86_apic_control_write(0xf0, 0x1ff))) return 0;
    return x86_apic_control_read(0x80, &value) && value == 0 &&
        x86_apic_control_read(0xf0, &value) && value == 0x1ff && lapic_empty();
}

// ====== soli NMI locali pertinenti al BSP senza conflitti sullo stesso LINT ===== //

static int lint_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile,
    MASK_REQUEST *request)
{
    BOOT_U32 index, used = 0;
    request->Lint[0] = request->Lint[1] = 0x10040;
    for (index = 0; index < topology->NmiCount; ++index) {
        const KERNEL_MADT_NMI_SOURCE *nmi = &topology->Nmi[index];
        BOOT_U32 value;
        if (nmi->Kind == 3 || (nmi->Uid != ~0U && nmi->Uid != profile->Uid)) continue;
        value = x86_irq_nmi_lint(nmi->Value,nmi->Flags);
        if (!value || (used & (1U << nmi->Value))) return 0;
        used |= 1U << nmi->Value;
        request->Lint[nmi->Value] = value;
    }
    return 1;
}

// ====== preflight locale prima di modificare PIC o ingressi IOAPIC ===== //

static int lapic_preflight(void *state)
{
    MASK_REQUEST *request = state;
    BOOT_U32 id, version;
    return x86_apic_control_read(0x20, &id) && (id >> 24) == request->Caps->LapicId &&
        x86_apic_control_read(0x30, &version) && version == request->Caps->LapicVersion && lapic_empty();
}

// ====== PIC mascherato solo sulle piattaforme che dichiarano PCAT ===== //

static int pic_mask(const KERNEL_MADT_TOPOLOGY *topology)
{
    if (!(topology->Pcat & 1)) return 1;
    x86_write_port(0x21, 255);
    x86_write_port(0xa1, 255);
    return x86_read_port(0x21) == 255 && x86_read_port(0xa1) == 255;
}

// ====== risultato indicato dopo tutte le verifiche hardware ===== //

static int report(const X86_APIC_CAPABILITIES *caps)
{
    BOOT_U32 index, inputs = 0;
    for (index = 0; index < caps->IoCount; ++index) inputs += caps->Io[index].Inputs;
    return kernel_section("controller interrupt mascherati") &&
        kernel_text("KERNEL_MASKED_IO_INPUTS: ") && kernel_number(inputs) &&
        kernel_text("\nKERNEL_MASKED_LVT_COUNT: ") &&
        kernel_number(((caps->LapicVersion >> 16) & 255) + 1) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_APIC_MASKED_OK");
}

// ====== buffer letto disgiunto dai metadati temporaneamente modificati ===== //

static int caps_separate(const X86_APIC_CAPABILITIES *caps, const X86_PAGE_WINDOW *window)
{
    return caps && window && window->Pool &&
        !os_address_ranges_overlap((BOOT_U64)caps, sizeof(*caps), (BOOT_U64)window, sizeof(*window)) &&
        !os_address_ranges_overlap((BOOT_U64)caps, sizeof(*caps), (BOOT_U64)window->Pool, sizeof(*window->Pool)) &&
        !os_address_ranges_overlap((BOOT_U64)caps, sizeof(*caps), X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES);
}

// ====== controller mascherati verificati due volte prima del successo ===== //

int x86_apic_quiesce(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, const X86_APIC_CAPABILITIES *caps)
{
    MASK_REQUEST request = {caps, 0, 0, {0,0}};
    BOOT_U32 pass;
    if (!caps_separate(caps, window) ||
        !x86_apic_mapping_ready(context, window, inventory, topology, profile) ||
        !capacities(topology, profile, caps) || !lint_plan(topology,profile,&request)) return 0;
    if (!x86_apic_borrow(window, profile->Lapic, profile->CacheBits, 0, lapic_preflight, &request)) return 0;
    request.Verify = 2;
    for (request.Index = 0; request.Index < caps->IoCount; ++request.Index) {
        if (!x86_apic_borrow(window, caps->Io[request.Index].Address, profile->CacheBits,
            1, io_mask, &request)) return 0;
    }
    if (!pic_mask(topology)) return 0;
    for (pass = 0; pass < 2; ++pass) {
        request.Verify = pass;
        for (request.Index = 0; request.Index < caps->IoCount; ++request.Index) {
            if (!x86_apic_borrow(window, caps->Io[request.Index].Address, profile->CacheBits,
                1, io_mask, &request)) return 0;
        }
        if (!x86_apic_borrow(window, profile->Lapic, profile->CacheBits, 1, lapic_mask, &request)) return 0;
    }
    // Le maschere legacy devono restare chiuse anche dopo i prestiti MMIO.
    if ((topology->Pcat & 1) &&
        (x86_read_port(0x21) != 255 || x86_read_port(0xa1) != 255)) return 0;
    if (!x86_apic_mapping_ready(context, window, inventory, topology, profile)) return 0;
    return report(caps);
}
