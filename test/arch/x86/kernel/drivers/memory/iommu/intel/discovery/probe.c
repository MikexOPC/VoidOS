#include "probe.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/discovery/find.h"
#include "kernel/diagnostics/log.h"
typedef struct { const KERNEL_BOOT_CONTEXT *Context; X86_PAGE_WINDOW *Window; } DMAR_READER;
static unsigned char snapshot[X86_DMAR_MAX_BYTES];
static X86_DMAR_INFO information;

// ====== x86_dmar_kernel_information ======

const X86_DMAR_INFO *x86_dmar_kernel_information(void) { return information.Physical?&information:0; }

// ====== read_private ======

static int read_private(void *opaque,OS_ACPI_U64 physical,OS_ACPI_U32 bytes,unsigned char *output)
{
    const DMAR_READER *reader=opaque;
    return x86_page_window_read_acpi(reader->Window,reader->Context,physical,bytes,output);
}

// ====== report_private ======

static int report_private(const char *label,BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== x86_dmar_kernel_probe ======

int x86_dmar_kernel_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const KERNEL_ACPI_DIRECTORY *directory)
{
    if (!context || !window || !window->Pool || !x86_page_window_idle_valid(window) || window->Root!=context->Info.PageTableRoot) return 0;
    DMAR_READER reader={context,window};
    BOOT_U64 budget=window->Pool->FreePages,root=window->Root;
    if (!report_private("KERNEL_DMAR_BUDGET_BEFORE: ",budget)) return 0;
    int status=x86_dmar_find(directory,window->Pool->PhysicalBits,read_private,&reader,snapshot,sizeof(snapshot),&information);
    if (!x86_page_window_idle_valid(window) || root!=window->Root || budget!=window->Pool->FreePages ||
        !report_private("KERNEL_DMAR_BUDGET_AFTER: ",window->Pool->FreePages)) return 0;
    if (status==X86_DMAR_ABSENT) return kernel_log(1,"KERNEL_DMAR_ABSENT");
    if (status!=X86_DMAR_FOUND || !report_private("KERNEL_DMAR_PHYSICAL: ",information.Physical) ||
        !report_private("KERNEL_DMAR_HOST_BITS: ",information.HostBits) || !report_private("KERNEL_DMAR_FLAGS: ",information.Flags) ||
        !report_private("KERNEL_DMAR_UNITS: ",information.Units) || !report_private("KERNEL_DMAR_SCOPES: ",information.Scopes) ||
        !report_private("KERNEL_DMAR_OTHER: ",information.OtherRecords)) return 0;
    for (uint32_t index=0; index<information.Units; ++index) {
        const X86_DMAR_UNIT *unit=&information.Unit[index];
        if (!report_private("KERNEL_DMAR_UNIT_BASE: ",unit->Base) || !report_private("KERNEL_DMAR_UNIT_BYTES: ",unit->Bytes) ||
            !report_private("KERNEL_DMAR_UNIT_SEGMENT: ",unit->Segment) || !report_private("KERNEL_DMAR_UNIT_FLAGS: ",unit->Flags) ||
            !report_private("KERNEL_DMAR_UNIT_SCOPES: ",unit->ScopeCount)) return 0;
    }
    return kernel_log(0,"KERNEL_DMAR_DISCOVERY_OK");
}
