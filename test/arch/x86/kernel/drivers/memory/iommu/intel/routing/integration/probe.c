#include "probe.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/discovery/find.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_IOMMU_ACTIVATION
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/activation/integration/probe.h"
#endif
static unsigned char scratch[X86_DMAR_MAX_BYTES];
static X86_DMAR_INFO info;
static X86_VTD_ROUTE route;
static X86_VTD_ROUTE_REQUEST request;

// ====== number_private ======

static int number_private(const char *name,BOOT_U64 value)
{ return kernel_text(name) && kernel_number(value) && kernel_text("\n"); }

// ====== acpi_private ======

static int acpi_private(void *state,BOOT_U64 physical,BOOT_U32 bytes,unsigned char *output)
{
    X86_ECAM *reader=state;
    return x86_page_window_read_acpi(reader->Window,reader->Context,physical,bytes,output);
}

// ====== x86_vtd_routing_probe ======

int x86_vtd_routing_probe(X86_ECAM *reader,const KERNEL_ACPI_DIRECTORY *directory,const X86_PCI_TOPOLOGY *topology)
{
    if (!x86_ecam_current(reader) || !kernel_log(1,"KERNEL_ROUTING_BEGIN")) return 0;
    int found=x86_dmar_find(directory,reader->Window->Pool->PhysicalBits,acpi_private,reader,scratch,sizeof(scratch),&info);
    if (found==X86_DMAR_ABSENT) return x86_ecam_current(reader) && kernel_log(1,"KERNEL_ROUTING_DMAR_ABSENT");
    if (found!=X86_DMAR_FOUND) return 0;
    const KERNEL_VGPU_BOOT *gpu=&reader->Context->Info.Gpu;
    if (!gpu->Present) return 0;
    request=(X86_VTD_ROUTE_REQUEST){gpu->Segment,gpu->Bus,gpu->Device,gpu->Function,
        (BOOT_U32)gpu->Config[0]|(BOOT_U32)gpu->Config[1]<<8|(BOOT_U32)gpu->Config[2]<<16|(BOOT_U32)gpu->Config[3]<<24};
    if (x86_vtd_route_select(&info,topology,&request,&route)!=X86_VTD_ROUTE_FOUND || !x86_ecam_current(reader) ||
        !number_private("KERNEL_ROUTING_INFO: ",(BOOT_U64)&info) || !number_private("KERNEL_ROUTING_REQUEST: ",(BOOT_U64)&request) ||
        !number_private("KERNEL_ROUTING_RESULT: ",(BOOT_U64)&route) || !kernel_log(0,"KERNEL_ROUTING_NATIVE_OK")) return 0;
    if (!x86_vtd_routing_matrix(topology,&request) || !x86_ecam_current(reader) || !kernel_log(0,"KERNEL_ROUTING_OK")) return 0;
#ifdef KERNEL_TEST_IOMMU_ACTIVATION
    return x86_vtd_activation_probe(reader,directory,&info,&route,&request) && x86_ecam_current(reader);
#else
    return 1;
#endif
}
