#include "probe.h"
#include "kernel/diagnostics/log.h"
static X86_VTD_MAPPING mapping;

// ====== report_private ======

static int report_private(const char *label,BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== capabilities_private ======

static int capabilities_private(const X86_VTD_CAPABILITIES *caps)
{
    return report_private("KERNEL_VTD_VERSION: ",caps->Version) && report_private("KERNEL_VTD_CAP: ",caps->Cap) &&
        report_private("KERNEL_VTD_ECAP: ",caps->Ecap) && report_private("KERNEL_VTD_STATUS: ",caps->Status) &&
        report_private("KERNEL_VTD_ADDRESS_BITS: ",caps->AddressBits) && report_private("KERNEL_VTD_DOMAINS: ",caps->Domains) &&
        report_private("KERNEL_VTD_IOTLB: ",caps->IotlbOffset) && report_private("KERNEL_VTD_FAULT_OFFSET: ",caps->FaultOffset) &&
        report_private("KERNEL_VTD_FAULT_COUNT: ",caps->FaultCount) && report_private("KERNEL_VTD_COMPATIBLE: ",caps->Compatible);
}

// ====== x86_vtd_kernel_probe ======

int x86_vtd_kernel_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    const X86_DMAR_INFO *description,X86_PAGING_INVENTORY *inventory)
{
    if (!description) return kernel_log(1,"KERNEL_VTD_ABSENT");
    BOOT_U64 budget=window->Pool->FreePages;
    X86_VTD_CAPABILITIES initial[8]={0};
    if (!report_private("KERNEL_VTD_BUDGET_BEFORE: ",budget)) return 0;
    for (BOOT_U32 cycle=0; cycle<2; ++cycle) {
        if (!x86_vtd_mapping_bind(context,window,lapic,description,inventory,&mapping)) goto failure;
        if (!report_private("KERNEL_VTD_ROOT: ",mapping.Authority.Root) || !report_private("KERNEL_VTD_VIRTUAL: ",X86_IOMMU_VIRTUAL) ||
            !report_private("KERNEL_VTD_CACHE: ",mapping.Authority.Cache) || !report_private("KERNEL_VTD_PAT: ",mapping.Authority.Pat)) goto failure;
        for (BOOT_U32 table=0; table<3; ++table)
            if (!report_private("KERNEL_VTD_TABLE: ",mapping.Authority.Tables[table].Base)) goto failure;
        for (BOOT_U32 unit=0; unit<description->Units; ++unit) {
            X86_VTD_CAPABILITIES current;
            if (!x86_vtd_capabilities_read(&mapping,unit,&current) || !capabilities_private(&current)) goto failure;
            if (!cycle) initial[unit]=current;
            else for (BOOT_U32 byte=0; byte<sizeof(current); ++byte)
                if (((const unsigned char *)&current)[byte]!=((const unsigned char *)&initial[unit])[byte]) goto failure;
        }
        if (!x86_vtd_mapping_unbind(&mapping) || window->Pool->FreePages!=budget || x86_iommu_owner()) return 0;
    }
    return report_private("KERNEL_VTD_BUDGET_AFTER: ",window->Pool->FreePages) && kernel_log(0,"KERNEL_VTD_REGISTERS_OK");
failure:
    if (mapping.Self) (void)x86_vtd_mapping_unbind(&mapping);
    (void)report_private("KERNEL_VTD_BUDGET_AFTER: ",window->Pool->FreePages);
    return 0;
}
