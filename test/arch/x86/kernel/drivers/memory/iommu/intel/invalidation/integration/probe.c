#include "probe.h"
#include "kernel/diagnostics/log.h"
static X86_VTD_MAPPING mapping;
static X86_VTD_DOMAIN domain;
static X86_QI qi;

// ====== number_private ======

static int number_private(const char *label,BOOT_U64 value)
{ return kernel_text(label) && kernel_number(value) && kernel_text("\n"); }

// ====== clock_private ======

static int clock_private(void *opaque,BOOT_U64 physical,BOOT_U32 bytes,unsigned char *output)
{
    X86_VTD_MAPPING *owner=opaque;
    return x86_page_window_read_acpi(owner->Window,owner->Context,physical,bytes,output);
}

// ====== x86_qi_probe ======

int x86_qi_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_DMAR_INFO *description,const KERNEL_ACPI_DIRECTORY *directory)
{
    if (!description || !context->Info.Gpu.Present) return kernel_log(1,"KERNEL_QI_ABSENT");
    BOOT_U64 budget=window->Pool->FreePages; X86_PM_CLOCK clock={0}; X86_VTD_DOMAIN_REQUEST request={0};
    BOOT_U32 buffers=0,pins=0,completed=0,phase=0;
    if (!number_private("KERNEL_QI_BUDGET_BEFORE: ",budget) || description->Units!=1 ||
        !x86_vtd_mapping_bind_access(context,window,lapic,description,inventory,&mapping,1) ||
        !x86_pm_clock_capture(directory,window->Pool->PhysicalBits,clock_private,&mapping,&clock)) goto cleanup;
    BOOT_U32 bits=description->HostBits<window->Pool->PhysicalBits?description->HostBits:window->Pool->PhysicalBits;
    if (!x86_qi_prepare(&mapping,&clock,bits,&qi)) goto cleanup;
    phase=1;
    if (!number_private("KERNEL_QI_ROOT: ",mapping.Authority.Root) || !number_private("KERNEL_QI_PHYSICAL: ",mapping.Authority.Base[0]) ||
        !number_private("KERNEL_QI_CACHE: ",mapping.Authority.Cache) || !number_private("KERNEL_QI_PAT: ",mapping.Authority.Pat) ||
        !number_private("KERNEL_QI_QUEUE: ",qi.Pages[0].Base) || !number_private("KERNEL_QI_STATUS_PAGE: ",qi.Pages[1].Base) ||
        !number_private("KERNEL_QI_CAP: ",qi.Cap)) goto cleanup;
    for (BOOT_U32 table=0; table<3; ++table)
        if (!number_private("KERNEL_QI_TABLE: ",mapping.Authority.Tables[table].Base)) goto cleanup;
    request.Segment=context->Info.Gpu.Segment; request.Bus=context->Info.Gpu.Bus;
    request.Device=context->Info.Gpu.Device; request.Function=context->Info.Gpu.Function; request.PhysicalBits=bits; request.Count=2;
    for (BOOT_U32 index=0; index<2; ++index) {
        if (!kernel_physical_allocate(window->Pool,index?4096:4,&request.Buffers[index].Lease)) goto cleanup;
        ++buffers;
        if (!kernel_physical_pin(window->Pool,&request.Buffers[index].Lease,1)) goto cleanup;
        pins|=1U<<index; request.Buffers[index].Access=index?1:3;
    }
    phase=2;
    if (!x86_vtd_domain_prepare(context,window,lapic,inventory,&request,&domain)) goto cleanup;
    phase=3;
    if (!x86_qi_clean_domain(&domain) ||
        !number_private("KERNEL_QI_CLEAN_TABLES: ",domain.TableCount) || !number_private("KERNEL_QI_CPU_LINE: ",x86_qi_cpu_line()) ||
        !x86_qi_start(&qi)) goto cleanup;
    phase=4;
    for (BOOT_U32 iteration=0; iteration<128; ++iteration)
        if (!x86_qi_submit(&qi) || !x86_qi_finish(&qi)) goto cleanup;
    if (!x86_vtd_domain_verify(&domain) || !number_private("KERNEL_QI_TOKEN: ",qi.Token) ||
        !number_private("KERNEL_QI_TAIL: ",qi.Tail)) goto cleanup;
    completed=1;
cleanup:
    if (!completed && !number_private("KERNEL_QI_FAILURE_PHASE: ",phase)) return 0;
    if (!completed && qi.Self) {
        if (!number_private("KERNEL_QI_FAILURE_DETAIL: ",qi.Failure)) return 0;
        if (!number_private("KERNEL_QI_FAILURE_STATE: ",qi.State) || !number_private("KERNEL_QI_FAILURE_CONFIG: ",qi.Configured) ||
            !number_private("KERNEL_QI_FAILURE_ENABLE: ",qi.EnableIssued) || !number_private("KERNEL_QI_FAILURE_GSTS: ",x86_qi_read(&qi,0x1c,4)) ||
            !number_private("KERNEL_QI_FAILURE_IQA: ",x86_qi_read(&qi,0x90,8)) || !number_private("KERNEL_QI_FAILURE_FSTS: ",x86_qi_read(&qi,0x34,4))) return 0;
    }
    if (domain.Self && !x86_vtd_domain_release(&domain)) return 0;
    for (BOOT_U32 count=buffers; count; --count) {
        if ((pins&(1U<<(count-1))) && !kernel_physical_pin(window->Pool,&request.Buffers[count-1].Lease,0)) return 0;
        if (!kernel_physical_release(window->Pool,&request.Buffers[count-1].Lease)) return 0;
    }
    if (qi.Self && !x86_qi_stop(&qi)) return 0;
    if (mapping.Self && !x86_vtd_mapping_unbind(&mapping)) return 0;
    if (!number_private("KERNEL_QI_BUDGET_AFTER: ",window->Pool->FreePages) || window->Pool->FreePages!=budget) return 0;
    return completed && kernel_log(0,"KERNEL_QI_OK");
}
