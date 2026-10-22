#include "probe.h"
#include "kernel/diagnostics/log.h"
static X86_VTD_DOMAIN domain;
static X86_VTD_MAPPING registers;

// ====== number_private ======

static int number_private(const char *label,BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== x86_vtd_domain_probe ======

int x86_vtd_domain_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_DMAR_INFO *description)
{
    if (!description || !context->Info.Gpu.Present) return kernel_log(1,"KERNEL_VTD_DOMAIN_ABSENT");
    BOOT_U64 budget=window->Pool->FreePages;
    X86_VTD_DOMAIN_REQUEST request={0}; X86_VTD_CAPABILITIES caps;
    BOOT_U32 buffers=0,pins=0,completed=0;
    if (!number_private("KERNEL_VTD_DOMAIN_BUDGET_BEFORE: ",budget) || description->Units!=1 || description->HostBits<48 ||
        !x86_vtd_mapping_bind(context,window,lapic,description,inventory,&registers)) goto cleanup;
    if (!x86_vtd_capabilities_read(&registers,0,&caps) || caps.Status || caps.AddressBits<48 || !(caps.Cap&(1ULL<<10)) ||
        !x86_vtd_mapping_unbind(&registers)) goto cleanup;
    request.Bus=context->Info.Gpu.Bus; request.Device=context->Info.Gpu.Device; request.Function=context->Info.Gpu.Function;
    request.Segment=context->Info.Gpu.Segment; request.PhysicalBits=description->HostBits;
    if (request.PhysicalBits>window->Pool->PhysicalBits) request.PhysicalBits=window->Pool->PhysicalBits;
    request.Count=2;
    for (BOOT_U32 index=0; index<2; ++index) {
        if (!kernel_physical_allocate(window->Pool,index?4096:4,&request.Buffers[index].Lease)) goto cleanup;
        ++buffers;
        if (!kernel_physical_pin(window->Pool,&request.Buffers[index].Lease,1)) goto cleanup;
        pins|=1U<<index; request.Buffers[index].Access=index?1:3;
    }
    for (BOOT_U32 cycle=0; cycle<2; ++cycle) {
        if (!x86_vtd_domain_prepare(context,window,lapic,inventory,&request,&domain) || !x86_vtd_domain_verify(&domain) ||
            !number_private("KERNEL_VTD_DOMAIN_BUS: ",request.Bus) || !number_private("KERNEL_VTD_DOMAIN_DEVFN: ",request.Device*8+request.Function) ||
            !number_private("KERNEL_VTD_DOMAIN_BITS: ",request.PhysicalBits) || !number_private("KERNEL_VTD_DOMAIN_TABLES: ",domain.TableCount)) goto cleanup;
        for (BOOT_U32 table=0; table<domain.TableCount; ++table)
            if (!number_private("KERNEL_VTD_DOMAIN_TABLE: ",domain.Tables[table].Base)) goto cleanup;
        for (BOOT_U32 range=0; range<2; ++range)
            if (!number_private("KERNEL_VTD_DOMAIN_IOVA: ",domain.Iova[range]) ||
                !number_private("KERNEL_VTD_DOMAIN_PHYSICAL: ",request.Buffers[range].Lease.Base) ||
                !number_private("KERNEL_VTD_DOMAIN_PAGES: ",request.Buffers[range].Lease.Pages) ||
                !number_private("KERNEL_VTD_DOMAIN_ACCESS: ",request.Buffers[range].Access)) goto cleanup;
        if (!x86_vtd_domain_release(&domain)) goto cleanup;
        ++completed;
    }
cleanup:
    if (domain.Self && !x86_vtd_domain_release(&domain)) return 0;
    if (registers.Self && !x86_vtd_mapping_unbind(&registers)) return 0;
    for (BOOT_U32 count=buffers; count; --count) {
        if ((pins&(1U<<(count-1))) && !kernel_physical_pin(window->Pool,&request.Buffers[count-1].Lease,0)) return 0;
        if (!kernel_physical_release(window->Pool,&request.Buffers[count-1].Lease)) return 0;
    }
    if (!number_private("KERNEL_VTD_DOMAIN_BUDGET_AFTER: ",window->Pool->FreePages) || window->Pool->FreePages!=budget) return 0;
    return completed==2 && domain.Self==0 && kernel_log(0,"KERNEL_VTD_DOMAIN_PRIVATE_OK");
}
