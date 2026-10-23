#include "probe.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
static X86_VTD_MAPPING mapping;
static X86_VTD_DOMAIN domain;
static X86_QI qi;
static X86_VTD_ACTIVATION act;
static X86_VTD_ROUTE route;
static X86_VTD_ROUTE_REQUEST request;
static X86_VTD_DMA_SESSION session;
typedef struct { BOOT_U32 Offset, Width; BOOT_U64 Value; } EDU_ACCESS;

// ====== number_private ======

static int number_private(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== clock_private ======

static int clock_private(void *opaque, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    X86_VTD_MAPPING *owner = opaque;
    return x86_page_window_read_acpi(owner->Window, owner->Context, physical, bytes, output);
}

// ====== register_private ======

static int register_private(void *opaque, int write)
{
    EDU_ACCESS *access = opaque;
    volatile unsigned char *page = x86_window_bytes();

    if (!page || (access->Width != 4 && access->Width != 8) || access->Offset > 4096 - access->Width) return 0;

    if (access->Width == 4) {
        volatile BOOT_U32 *word = (volatile BOOT_U32 *)(page + access->Offset);

        if (write) *word = (BOOT_U32)access->Value;
        else access->Value = *word;
    }
    else {
        volatile BOOT_U64 *word = (volatile BOOT_U64 *)(page + access->Offset);

        if (write) *word = access->Value;
        else access->Value = *word;
    }

    return 1;
}

static int load_private(void *opaque) { return register_private(opaque, 0); }

static int store_private(void *opaque) { return register_private(opaque, 1); }

// ====== edu_read_private ======

// BAR0 page through an uncached borrow of the ECAM owner (validated not RAM by x86_ecam_safe).
static int edu_read_private(void *state, BOOT_U32 offset, BOOT_U32 width, BOOT_U64 *value)
{
    X86_VTD_DMA_SESSION *owner = state;
    EDU_ACCESS access = {offset, width, 0};

    if (!value || !x86_apic_borrow(owner->Reader->Window, owner->Bar, owner->Reader->Cache, 0, load_private, &access) ||
        !x86_page_window_idle_valid(owner->Reader->Window)
    )
    return 0;

    *value = access.Value;
    return 1;
}

// ====== edu_write_private ======

static int edu_write_private(void *state, BOOT_U32 offset, BOOT_U32 width, BOOT_U64 value)
{
    X86_VTD_DMA_SESSION *owner = state;
    EDU_ACCESS access = {offset, width, value};

    return x86_apic_borrow(owner->Reader->Window, owner->Bar, owner->Reader->Cache, 1, store_private, &access) &&
        x86_page_window_idle_valid(owner->Reader->Window);
}

// ====== edu_ticks_private ======

static BOOT_U64 edu_ticks_private(void *state)
{
    const X86_VTD_DMA_SESSION *owner = state;
    return x86_qi_ticks(owner->Qi);
}

// ====== locate_private ======

// PCI Local Bus 3.0 6.2.5.1: BAR0 10h, bit0=0 memory, bits2:1=00 32-bit; edu registers 1 MiB.
static int locate_private(X86_ECAM *reader, BOOT_U32 *command)
{
    BOOT_U32 id = 0, bar = 0;

    if (!x86_ecam_read(reader, 0, 0, X86_VTD_DMA_DEVICE, 0, 0, &id) || id != EDU_ID ||
        !x86_ecam_read(reader, 0, 0, X86_VTD_DMA_DEVICE, 0, 16, &bar) || (bar & 7) ||
        !(bar & ~15U) || (bar & 0xfffffU) ||
        !x86_ecam_read(reader, 0, 0, X86_VTD_DMA_DEVICE, 0, 4, command) ||
        !x86_ecam_safe(reader, bar & ~15ULL, 4096)
    )
    return 0;

    session.Bar = bar & ~15ULL;
    return number_private("KERNEL_DMA_BAR: ", session.Bar) && number_private("KERNEL_DMA_COMMAND_BEFORE: ", *command & 0xffff);
}

// ====== buffers_private ======

static int buffers_private(KERNEL_PHYSICAL_ALLOCATOR *pool, X86_VTD_DOMAIN_REQUEST *demand, BOOT_U32 *buffers, BOOT_U32 *pins)
{
    static const BOOT_U32 access[X86_VTD_DMA_BUFFERS] = {1, 3, 3, 1};

    for (BOOT_U32 index = 0; index < X86_VTD_DMA_BUFFERS; ++index) {
        if (!kernel_physical_allocate(pool, 1, &demand->Buffers[index].Lease)) return 0;

        ++*buffers;

        if (!kernel_physical_pin(pool, &demand->Buffers[index].Lease, 1)) return 0;

        *pins |= 1U << index;
        demand->Buffers[index].Access = access[index];
    }

    demand->Count = X86_VTD_DMA_BUFFERS;
    return 1;
}

// ====== report_private ======

static int report_private(void)
{
    if (!number_private("KERNEL_DMA_SOURCE: ", act.Source) ||
        !number_private("KERNEL_DMA_ACKNOWLEDGED: ", act.Acknowledged) ||
        !number_private("KERNEL_DMA_OWNED: ", act.Owned) ||
        !number_private("KERNEL_DMA_BLOCKED: ", act.Blocked) ||
        !number_private("KERNEL_DMA_LOGGED: ", act.Logged)
    )
    return 0;

    for (BOOT_U32 index = 0; index < act.Logged; ++index)
        if (!number_private("KERNEL_DMA_FAULT_SOURCE: ", act.Log[index].Source) ||
            !number_private("KERNEL_DMA_FAULT_REASON: ", act.Log[index].Reason) ||
            !number_private("KERNEL_DMA_FAULT_READ: ", act.Log[index].Read) ||
            !number_private("KERNEL_DMA_FAULT_ADDRESS: ", act.Log[index].Address)
        )
        return 0;

    return 1;
}

// ====== x86_vtd_dma_probe ======

// Diagnostic only: owned edu requester, private domain with A (read), B and C (read/write),
// P1-P5 under translation, then TE off, invalidation and full release with the budget restored.
int x86_vtd_dma_probe(X86_ECAM *reader, const KERNEL_ACPI_DIRECTORY *directory, const X86_DMAR_INFO *info,
    const X86_PCI_TOPOLOGY *topology)
{
    const KERNEL_BOOT_CONTEXT *context = reader->Context;
    X86_PAGE_WINDOW *window = reader->Window;
    // The ECAM reader keeps a const view of the kernel's mutable paging inventory (startup object).
    X86_PAGING_INVENTORY *inventory = (X86_PAGING_INVENTORY *)reader->Inventory;
    BOOT_U64 budget = window->Pool->FreePages;
    X86_PM_CLOCK clock = {0};
    X86_VTD_DOMAIN_REQUEST demand = {0};
    BOOT_U32 buffers = 0, pins = 0, phase = 0, completed = 0, command = 0, master = 0;

    if (!kernel_log(1, "KERNEL_DMA_BEGIN") || !number_private("KERNEL_DMA_BUDGET_BEFORE: ", budget) ||
        info->Units != 1 || !locate_private(reader, &command)
    )
    goto cleanup;

    phase = 1;
    request = (X86_VTD_ROUTE_REQUEST){0, 0, X86_VTD_DMA_DEVICE, 0, EDU_ID};

    if (x86_vtd_route_select(info, topology, &request, &route) != X86_VTD_ROUTE_FOUND || route.Unit) goto cleanup;

    // ECAM accepts accesses only while the pool budget equals the one it opened with, so the
    // owned function is configured before any VT-d allocation and restored after the release.
    phase = 2;

    // Memory Space bit1 and Bus Master bit2 of the owned function only; restored in cleanup.
    if (!x86_ecam_command(reader, 0, 0, X86_VTD_DMA_DEVICE, 0, 6, 6)) goto cleanup;

    master = 1;
    session.Reader = reader;
    session.Qi = &qi;
    session.Domain = &domain;
    session.Activation = &act;
    // Identification needs no clock; the PM mask is taken from the captured clock below.
    session.Bus = (EDU_BUS){&session, edu_read_private, edu_write_private, edu_ticks_private, X86_PM_CLOCK_HZ, 0xffffff};
    phase = 3;

    if (!edu_identify(&session.Bus)) goto cleanup;

    phase = 4;

    if (!x86_vtd_mapping_bind_access(context, window, 0, info, inventory, &mapping, 1) ||
        !x86_pm_clock_capture(directory, window->Pool->PhysicalBits, clock_private, &mapping, &clock)
    )
    goto cleanup;

    BOOT_U32 bits = info->HostBits < window->Pool->PhysicalBits ? info->HostBits : window->Pool->PhysicalBits;

    if (!x86_qi_prepare(&mapping, &clock, bits, &qi)) goto cleanup;

    session.Bus.Mask = qi.Clock.Mask;
    phase = 5;
    demand.Device = X86_VTD_DMA_DEVICE;
    demand.PhysicalBits = bits;

    if (!buffers_private(window->Pool, &demand, &buffers, &pins)) goto cleanup;

    phase = 6;

    if (!x86_vtd_domain_prepare(context, window, 0, inventory, &demand, &domain) ||
        !x86_vtd_dma_fill(&session, X86_VTD_DMA_A, 0xa1) ||
        !x86_vtd_dma_fill(&session, X86_VTD_DMA_B, 0) ||
        !x86_vtd_dma_fill(&session, X86_VTD_DMA_C, 0x5c) ||
        !x86_vtd_dma_fill(&session, X86_VTD_DMA_D, 0xd4)
    )
    goto cleanup;

    phase = 7;

    if (!x86_qi_start(&qi) || !x86_vtd_activation_begin(&qi, &domain, &route, &act) ||
        !number_private("KERNEL_DMA_ROOT: ", act.Root) ||
        !number_private("KERNEL_DMA_IOVA_A: ", domain.Iova[X86_VTD_DMA_A]) ||
        !number_private("KERNEL_DMA_IOVA_B: ", domain.Iova[X86_VTD_DMA_B]) ||
        !number_private("KERNEL_DMA_IOVA_C: ", domain.Iova[X86_VTD_DMA_C]) ||
        !number_private("KERNEL_DMA_IOVA_D: ", domain.Iova[X86_VTD_DMA_D]) ||
        !x86_vtd_activation_enable(&act)
    )
    goto cleanup;

    phase = 8;

    if (!x86_vtd_dma_phases(&session)) goto cleanup;

    phase = 9;

    if (!x86_vtd_activation_disable(&act) || !report_private()) goto cleanup;

    completed = 1;

cleanup:
    if (!completed &&
        (!number_private("KERNEL_DMA_FAILURE_PHASE: ", phase) ||
            (act.Self &&
                (!number_private("KERNEL_DMA_FAILURE_STATE: ", act.State) ||
                    !number_private("KERNEL_DMA_FAILURE_DETAIL: ", act.Failure) || !report_private())))
    )
    return 0;

    if (act.Self && act.State == X86_VTD_ACT_TRANSLATING) (void)x86_vtd_activation_disable(&act);

    // RETAINED or an unacknowledged owned fault: tables, queue and mapping stay owned; stop here.
    if (act.Self && !x86_vtd_activation_end(&act)) return kernel_log(3, "KERNEL_DMA_RETAINED") && 0;

    if (domain.Self && !x86_vtd_domain_release(&domain)) return 0;

    for (BOOT_U32 count = buffers; count; --count) {
        if ((pins & (1U << (count - 1))) && !kernel_physical_pin(window->Pool, &demand.Buffers[count - 1].Lease, 0)) return 0;

        if (!kernel_physical_release(window->Pool, &demand.Buffers[count - 1].Lease)) return 0;
    }

    if (qi.Self && !x86_qi_stop(&qi)) return 0;

    if (mapping.Self && !x86_vtd_mapping_unbind(&mapping)) return 0;

    if (master && !x86_ecam_command(reader, 0, 0, X86_VTD_DMA_DEVICE, 0, 6, command & 6)) return 0;

    if (!number_private("KERNEL_DMA_BUDGET_AFTER: ", window->Pool->FreePages) || window->Pool->FreePages != budget) return 0;

    return completed && kernel_log(0, "KERNEL_DMA_OK");
}
