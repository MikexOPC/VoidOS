#include "probe.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_PCI_TOPOLOGY
#include "test/arch/x86/kernel/drivers/bus/pci/topology/integration/probe.h"
#endif
static X86_ECAM owner;
static X86_MCFG info;
static unsigned char scratch[X86_MCFG_BYTES];
typedef struct { const KERNEL_BOOT_CONTEXT *Context; X86_PAGE_WINDOW *Window; } ECAM_ACPI;

// ====== acpi_private ======

static int acpi_private(void *state,BOOT_U64 physical,BOOT_U32 bytes,unsigned char *out)
{
    const ECAM_ACPI *reader=state;
    if (!x86_page_window_read_acpi(reader->Window,reader->Context,physical,bytes,out)) return 0;
#ifdef KERNEL_TEST_PCI_ECAM_ABSENT
    if (bytes==36 && out[0]=='M' && out[1]=='C' && out[2]=='F' && out[3]=='G') out[0]='X';
#endif
    return 1;
}

// ====== number_private ======

static int number_private(const char *name,BOOT_U64 value)
{ return kernel_text(name) && kernel_number(value) && kernel_text("\n"); }

// ====== x86_ecam_probe ======

int x86_ecam_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_PAGING_INVENTORY *inventory,
    const KERNEL_ACPI_DIRECTORY *directory)
{
    ECAM_ACPI reader={context,window}; BOOT_U64 budget=window->Pool->FreePages; int completed=0;
#ifdef KERNEL_TEST_PCI_ECAM_ABSENT
    if (!kernel_log(1,"KERNEL_ECAM_FILTERED_ABSENCE_TEST")) return 0;
#endif
    if (!number_private("KERNEL_ECAM_BUDGET_BEFORE: ",budget)) return 0;
    int found=x86_mcfg_find(directory,window->Pool->PhysicalBits,acpi_private,&reader,scratch,sizeof(scratch),&info);
    if (found==2) {
        return x86_page_window_idle_valid(window) && budget==window->Pool->FreePages &&
            number_private("KERNEL_ECAM_BUDGET_AFTER: ",budget) && kernel_log(1,"KERNEL_ECAM_ABSENT");
    }
    if (found!=1 || !number_private("KERNEL_ECAM_MCFG: ",info.Physical) || !number_private("KERNEL_ECAM_RANGES: ",info.Count)) goto cleanup;
    for (BOOT_U32 index=0; index<info.Count; ++index) {
        const X86_ECAM_RANGE *range=&info.Ranges[index];
        if (!number_private("KERNEL_ECAM_BASE: ",range->Base) || !number_private("KERNEL_ECAM_SEGMENT: ",range->Segment) ||
            !number_private("KERNEL_ECAM_FIRST: ",range->First) || !number_private("KERNEL_ECAM_LAST: ",range->Last)) return 0;
    }
    if (!x86_ecam_open(context,window,0,inventory,&info,&owner)) goto cleanup;
    if (!number_private("KERNEL_ECAM_ROOT: ",owner.Root) || !number_private("KERNEL_ECAM_PAT: ",owner.Pat) ||
        !number_private("KERNEL_ECAM_CACHE: ",owner.Cache)) goto cleanup;
    BOOT_U32 segment=context->Info.Gpu.Present?context->Info.Gpu.Segment:info.Ranges[0].Segment;
    BOOT_U32 bus=context->Info.Gpu.Present?context->Info.Gpu.Bus:info.Ranges[0].First;
    BOOT_U32 device=context->Info.Gpu.Present?context->Info.Gpu.Device:0;
    BOOT_U32 function=context->Info.Gpu.Present?context->Info.Gpu.Function:0;
    BOOT_U64 page=0;
    for (BOOT_U32 index=0; index<info.Count; ++index) {
        const X86_ECAM_RANGE *range=&info.Ranges[index];
        if (range->Segment==segment && bus>=range->First && bus<=range->Last) page=x86_ecam_page(range->Base,bus,device,function);
    }
    if (!number_private("KERNEL_ECAM_TARGET_SEGMENT: ",segment) || !number_private("KERNEL_ECAM_TARGET_BUS: ",bus) ||
        !number_private("KERNEL_ECAM_TARGET_DEVICE: ",device) || !number_private("KERNEL_ECAM_TARGET_FUNCTION: ",function) ||
        !number_private("KERNEL_ECAM_PAGE: ",page)) goto cleanup;
    for (BOOT_U32 pass=0; pass<2; ++pass) for (BOOT_U32 at=0; at<4; ++at) {
        const BOOT_U32 offsets[]={0,8,12,24}; BOOT_U32 value=0;
        if (!x86_ecam_read(&owner,segment,bus,device,function,offsets[at],&value) || !number_private("KERNEL_ECAM_VALUE: ",value)) goto cleanup;
    }
    BOOT_U32 value=0x12345678;
    if (x86_ecam_read(&owner,segment,bus,device,function,4,&value) || value!=0x12345678) goto cleanup;
#ifdef KERNEL_TEST_PCI_TOPOLOGY
    if (!x86_pci_topology_probe(&owner,directory)) goto cleanup;
#endif
    completed=1;
cleanup:
    if (owner.Self && !x86_ecam_close(&owner)) return 0;
    if (!x86_page_window_idle_valid(window) || window->Pool->FreePages!=budget ||
        !number_private("KERNEL_ECAM_BUDGET_AFTER: ",window->Pool->FreePages)) return 0;
    return completed && kernel_log(0,"KERNEL_ECAM_OK");
}
