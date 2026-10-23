#include "probe.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/discovery/find.h"
#include "arch/x86/kernel/drivers/bus/pci/topology/topology.h"
#include "kernel/diagnostics/log.h"
static unsigned char scratch[X86_DMAR_MAX_BYTES];
static X86_PCI_TOPOLOGY topology, work;
static X86_ECAM reader;
static X86_MCFG mcfg;
static X86_DMAR_INFO info;
static X86_VTD_ROUTE route;
static X86_VTD_ROUTE_REQUEST request;
static X86_VTD_MAPPING mapping;
static X86_VTD_DOMAIN domain;
static X86_QI qi;
static X86_VTD_ACTIVATION act;
static X86_VGPU_MAPPING *owner;
static const KERNEL_ACPI_DIRECTORY *tables;

// ====== number_private ======

static int number_private(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== acpi_private ======

static int acpi_private(void *state, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    const X86_VGPU_MAPPING *gpu = state;
    return x86_page_window_read_acpi(gpu->Window, gpu->Context, physical, bytes, output);
}

// ====== clock_private ======

static int clock_private(void *opaque, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    X86_VTD_MAPPING *registers = opaque;
    return x86_page_window_read_acpi(registers->Window, registers->Context, physical, bytes, output);
}

// ====== x86_vgpu_iommu_route ======

// The PCI graph is read through a short-lived ECAM owner, closed before any VT-d allocation.
int x86_vgpu_iommu_route(X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory)
{
    const KERNEL_VGPU_BOOT *boot = 0;
    int captured = 0;

    if (!gpu || owner || !directory || !gpu->Context || !gpu->Window || !gpu->Window->Pool) return 0;

    boot = &gpu->Context->Info.Gpu;

    if (!boot->Present || !kernel_log(1, "KERNEL_VGPU_IOMMU_BEGIN") ||
        x86_mcfg_find(directory, gpu->Window->Pool->PhysicalBits, acpi_private, gpu, scratch, sizeof(scratch), &mcfg) != 1 ||
        !x86_ecam_open(gpu->Context, gpu->Window, gpu->Lapic, gpu->Inventory, &mcfg, &reader)
    )
    return 0;

    captured = x86_pci_topology_native(&reader, &work, &topology);

    if (!x86_ecam_close(&reader) || !captured ||
        x86_dmar_find(directory, gpu->Window->Pool->PhysicalBits, acpi_private, gpu, scratch, sizeof(scratch), &info) != X86_DMAR_FOUND ||
        info.Units != 1
    )
    return 0;

    request = (X86_VTD_ROUTE_REQUEST){boot->Segment, boot->Bus, boot->Device, boot->Function,
        (BOOT_U32)boot->Config[0] | (BOOT_U32)boot->Config[1] << 8 | (BOOT_U32)boot->Config[2] << 16 | (BOOT_U32)boot->Config[3] << 24};

    if (x86_vtd_route_select(&info, &topology, &request, &route) != X86_VTD_ROUTE_FOUND || route.Unit) return 0;

    owner = gpu;
    tables = directory;
    return number_private("KERNEL_VGPU_IOMMU_SOURCE: ", route.Requester) &&
        number_private("KERNEL_VGPU_IOMMU_UNIT: ", route.Base);
}

// ====== x86_vgpu_iommu_enable ======

// Private domain for the GPU requester only, QI, root publication and TE=1 (blocks 1-2).
int x86_vgpu_iommu_enable(const KERNEL_PHYSICAL_LEASE *queue, const KERNEL_PHYSICAL_LEASE *surface)
{
    X86_PM_CLOCK clock = {0};
    X86_VTD_DOMAIN_REQUEST demand = {0};
    X86_PAGING_INVENTORY *inventory = 0;

    if (!owner || !queue || !surface || mapping.Self || qi.Self || domain.Self || act.Self) return 0;

    inventory = owner->Inventory;

    BOOT_U32 bits = info.HostBits < owner->Window->Pool->PhysicalBits ? info.HostBits : owner->Window->Pool->PhysicalBits;

    demand.Segment = request.Segment; demand.Bus = request.Bus; demand.Device = request.Device; demand.Function = request.Function;
    demand.PhysicalBits = bits; demand.Count = 2;
    demand.Buffers[X86_VGPU_IOMMU_QUEUE] = (X86_VTD_BUFFER){*queue, 3, 0};
    demand.Buffers[X86_VGPU_IOMMU_SURFACE] = (X86_VTD_BUFFER){*surface, 1, 0};

    if (!x86_vtd_mapping_bind_access(owner->Context, owner->Window, owner->Lapic, &info, inventory, &mapping, 1)) return 0;

    if (!x86_pm_clock_capture(tables, owner->Window->Pool->PhysicalBits, clock_private, &mapping, &clock) ||
        !x86_qi_prepare(&mapping, &clock, bits, &qi) ||
        !x86_vtd_domain_prepare(owner->Context, owner->Window, owner->Lapic, inventory, &demand, &domain) ||
        !x86_qi_start(&qi) || !x86_vtd_activation_begin(&qi, &domain, &route, &act) ||
        !number_private("KERNEL_VGPU_IOMMU_ROOT: ", act.Root) ||
        !number_private("KERNEL_VGPU_IOMMU_QUEUE: ", domain.Iova[X86_VGPU_IOMMU_QUEUE]) ||
        !number_private("KERNEL_VGPU_IOMMU_SURFACE: ", domain.Iova[X86_VGPU_IOMMU_SURFACE]) ||
        !x86_vtd_activation_enable(&act)
    )
    return 0;

    return kernel_log(0, "KERNEL_VGPU_IOMMU_TRANSLATING");
}

// ====== x86_vgpu_iommu_device ======

BOOT_U64 x86_vgpu_iommu_device(BOOT_U64 physical, BOOT_U64 bytes, BOOT_U32 access)
{
    return act.Self && act.State == X86_VTD_ACT_TRANSLATING ? x86_vtd_domain_translate(&domain, physical, bytes, access) : 0;
}

// ====== x86_vgpu_iommu_revoke ======

// Leaf cleared, page-walk lines flushed, then the global context/IOTLB invalidation and wait.
int x86_vgpu_iommu_revoke(BOOT_U32 index)
{
    return act.Self && act.State == X86_VTD_ACT_TRANSLATING &&
        x86_vtd_domain_revoke(&domain, index) && x86_qi_clean_domain(&domain) &&
        x86_qi_submit(&qi) && x86_qi_finish(&qi);
}

// ====== x86_vgpu_iommu_expect ======

int x86_vgpu_iommu_expect(BOOT_U32 reason, BOOT_U32 read, BOOT_U64 address)
{
    return x86_vtd_activation_expect(&act, reason, read, address);
}

// ====== x86_vgpu_iommu_quiet ======

// No pending record and no owned fault: every DMA of the GPU was translated and allowed.
int x86_vgpu_iommu_quiet(void)
{
    return act.Self && x86_vtd_activation_faults(&act);
}

// ====== report_private ======

static int report_private(void)
{
    if (!number_private("KERNEL_VGPU_IOMMU_OWNED: ", act.Owned) ||
        !number_private("KERNEL_VGPU_IOMMU_ACKNOWLEDGED: ", act.Acknowledged) ||
        !number_private("KERNEL_VGPU_IOMMU_BLOCKED: ", act.Blocked) ||
        !number_private("KERNEL_VGPU_IOMMU_LOGGED: ", act.Logged)
    )
    return 0;

    for (BOOT_U32 index = 0; index < act.Logged; ++index)
        if (!number_private("KERNEL_VGPU_IOMMU_FAULT_SOURCE: ", act.Log[index].Source) ||
            !number_private("KERNEL_VGPU_IOMMU_FAULT_REASON: ", act.Log[index].Reason) ||
            !number_private("KERNEL_VGPU_IOMMU_FAULT_READ: ", act.Log[index].Read) ||
            !number_private("KERNEL_VGPU_IOMMU_FAULT_ADDRESS: ", act.Log[index].Address)
        )
        return 0;

    return 1;
}

// ====== x86_vgpu_iommu_disable ======

// RETAINED or an unacknowledged owned fault: tables, queue and mapping stay owned (return 0).
int x86_vgpu_iommu_disable(void)
{
    if (act.Self && !report_private()) return 0;

    if (act.Self && act.State == X86_VTD_ACT_TRANSLATING) (void)x86_vtd_activation_disable(&act);

    if (act.Self && !x86_vtd_activation_end(&act)) {
        (void)number_private("KERNEL_VGPU_IOMMU_FAILURE_STATE: ", act.State);
        (void)number_private("KERNEL_VGPU_IOMMU_FAILURE_DETAIL: ", act.Failure);
        (void)number_private("KERNEL_VGPU_IOMMU_FAILURE_QI: ", qi.Failure);
        return kernel_log(3, "KERNEL_VGPU_IOMMU_RETAINED") && 0;
    }

    if (domain.Self && !x86_vtd_domain_release(&domain)) return 0;

    if (qi.Self && !x86_qi_stop(&qi)) return 0;

    if (mapping.Self && !x86_vtd_mapping_unbind(&mapping)) return 0;

    owner = 0;
    return kernel_log(0, "KERNEL_VGPU_IOMMU_RELEASED");
}
