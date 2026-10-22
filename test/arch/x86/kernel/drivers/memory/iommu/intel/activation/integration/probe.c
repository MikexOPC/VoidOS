#include "probe.h"
#include "kernel/diagnostics/log.h"
static X86_VTD_MAPPING mapping;
static X86_VTD_DOMAIN domain;
static X86_QI qi;
static X86_VTD_ACTIVATION act;

// ====== number_private ======

static int number_private(const char *label,BOOT_U64 value)
{ return kernel_text(label) && kernel_number(value) && kernel_text("\n"); }

// ====== clock_private ======

static int clock_private(void *opaque,BOOT_U64 physical,BOOT_U32 bytes,unsigned char *output)
{
    X86_VTD_MAPPING *owner=opaque;
    return x86_page_window_read_acpi(owner->Window,owner->Context,physical,bytes,output);
}

// ====== registers_private ======

static int registers_private(const char *phase)
{
    return kernel_text("KERNEL_ACTIVATION_PHASE: ") && kernel_text(phase) && kernel_text("\n") &&
        number_private("KERNEL_ACTIVATION_GSTS: ",x86_vtd_activation_read(&act,0x1c,4)) &&
        number_private("KERNEL_ACTIVATION_RTADDR: ",x86_vtd_activation_read(&act,0x20,8)) &&
        number_private("KERNEL_ACTIVATION_FSTS: ",x86_vtd_activation_read(&act,0x34,4));
}

// ====== report_private ======

static int report_private(void)
{
    if (!number_private("KERNEL_ACTIVATION_OWNED: ",act.Owned) || !number_private("KERNEL_ACTIVATION_BLOCKED: ",act.Blocked) ||
        !number_private("KERNEL_ACTIVATION_OVERFLOWS: ",act.Overflows) || !number_private("KERNEL_ACTIVATION_LOGGED: ",act.Logged)) return 0;
    for (BOOT_U32 index=0; index<act.Logged; ++index)
        if (!number_private("KERNEL_ACTIVATION_FAULT_SOURCE: ",act.Log[index].Source) ||
            !number_private("KERNEL_ACTIVATION_FAULT_REASON: ",act.Log[index].Reason) ||
            !number_private("KERNEL_ACTIVATION_FAULT_ADDRESS: ",act.Log[index].Address)) return 0;
    return 1;
}

// ====== x86_vtd_activation_probe ======

// Diagnostic only: publish the GPU domain on the routed unit, translate, observe faults, tear down.
int x86_vtd_activation_probe(X86_ECAM *reader,const KERNEL_ACPI_DIRECTORY *directory,const X86_DMAR_INFO *info,
    const X86_VTD_ROUTE *route,const X86_VTD_ROUTE_REQUEST *request)
{
    const KERNEL_BOOT_CONTEXT *context=reader->Context; X86_PAGE_WINDOW *window=reader->Window;
    // The ECAM reader keeps a const view of the kernel's mutable paging inventory (main.c object).
    X86_PAGING_INVENTORY *inventory=(X86_PAGING_INVENTORY *)reader->Inventory;
    BOOT_U64 budget=window->Pool->FreePages; X86_PM_CLOCK clock={0}; X86_VTD_DOMAIN_REQUEST demand={0};
    BOOT_U32 buffers=0,pins=0,phase=0,completed=0;
    if (!kernel_log(1,"KERNEL_ACTIVATION_BEGIN") || !number_private("KERNEL_ACTIVATION_BUDGET_BEFORE: ",budget) ||
        info->Units!=1 || route->Unit || !x86_vtd_mapping_bind_access(context,window,0,info,inventory,&mapping,1) ||
        !x86_pm_clock_capture(directory,window->Pool->PhysicalBits,clock_private,&mapping,&clock)) goto cleanup;
    BOOT_U32 bits=info->HostBits<window->Pool->PhysicalBits?info->HostBits:window->Pool->PhysicalBits;
    if (!x86_qi_prepare(&mapping,&clock,bits,&qi)) goto cleanup;
    phase=1; demand.Segment=request->Segment; demand.Bus=request->Bus; demand.Device=request->Device;
    demand.Function=request->Function; demand.PhysicalBits=bits; demand.Count=2;
    for (BOOT_U32 index=0; index<2; ++index) {
        if (!kernel_physical_allocate(window->Pool,index?4096:4,&demand.Buffers[index].Lease)) goto cleanup;
        ++buffers;
        if (!kernel_physical_pin(window->Pool,&demand.Buffers[index].Lease,1)) goto cleanup;
        pins|=1U<<index; demand.Buffers[index].Access=index?1:3;
    }
    phase=2; if (!x86_vtd_domain_prepare(context,window,0,inventory,&demand,&domain)) goto cleanup;
    phase=3; if (!x86_qi_start(&qi)) goto cleanup;
    phase=4; if (!x86_vtd_activation_begin(&qi,&domain,route,&act) || !number_private("KERNEL_ACTIVATION_ROOT: ",act.Root) ||
        !number_private("KERNEL_ACTIVATION_SOURCE: ",act.Source) || !number_private("KERNEL_ACTIVATION_FRO: ",act.FaultOffset) ||
        !number_private("KERNEL_ACTIVATION_NFR: ",act.FaultCount) || !number_private("KERNEL_ACTIVATION_CAP: ",qi.Cap) ||
        !number_private("KERNEL_ACTIVATION_TABLES: ",domain.TableCount) || !registers_private("READY") ||
        !x86_vtd_activation_faults(&act)) goto cleanup;
    phase=5; if (!x86_vtd_activation_enable(&act) || !registers_private("TRANSLATING") || !x86_vtd_activation_faults(&act)) goto cleanup;
    phase=6; if (!x86_vtd_activation_disable(&act) || !registers_private("UNPUBLISHED") || !x86_vtd_activation_faults(&act)) goto cleanup;
    phase=7; if (!report_private()) goto cleanup;
    completed=1;
cleanup:
    if (!completed && (!number_private("KERNEL_ACTIVATION_FAILURE_PHASE: ",phase) ||
        (act.Self && (!number_private("KERNEL_ACTIVATION_FAILURE_STATE: ",act.State) ||
        !number_private("KERNEL_ACTIVATION_FAILURE_DETAIL: ",act.Failure) || !report_private())))) return 0;
    if (act.Self && act.State==X86_VTD_ACT_TRANSLATING) (void)x86_vtd_activation_disable(&act);
    // RETAINED or an owned fault: tables, queue and mapping stay owned; the session stops here.
    if (act.Self && !x86_vtd_activation_end(&act)) return kernel_log(3,"KERNEL_ACTIVATION_RETAINED") && 0;
    if (domain.Self && !x86_vtd_domain_release(&domain)) return 0;
    for (BOOT_U32 count=buffers; count; --count) {
        if ((pins&(1U<<(count-1))) && !kernel_physical_pin(window->Pool,&demand.Buffers[count-1].Lease,0)) return 0;
        if (!kernel_physical_release(window->Pool,&demand.Buffers[count-1].Lease)) return 0;
    }
    if (qi.Self && !x86_qi_stop(&qi)) return 0;
    if (mapping.Self && !x86_vtd_mapping_unbind(&mapping)) return 0;
    if (!number_private("KERNEL_ACTIVATION_BUDGET_AFTER: ",window->Pool->FreePages) || window->Pool->FreePages!=budget) return 0;
    return completed && kernel_log(0,"KERNEL_ACTIVATION_OK");
}
