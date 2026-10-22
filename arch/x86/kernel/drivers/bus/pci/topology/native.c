#include "topology.h"
#include "lib/memory/address_ranges.h"

// ====== protected_private ======

static int protected_private(const X86_ECAM *reader,const X86_PCI_TOPOLOGY *buffer)
{
    BOOT_U64 base=(BOOT_U64)buffer,bytes=sizeof(*buffer);
    return os_address_ranges_overlap(base,bytes,(BOOT_U64)reader,sizeof(*reader)) ||
        os_address_ranges_overlap(base,bytes,(BOOT_U64)reader->Context,sizeof(*reader->Context)) ||
        os_address_ranges_overlap(base,bytes,(BOOT_U64)reader->Window,sizeof(*reader->Window)) ||
        os_address_ranges_overlap(base,bytes,(BOOT_U64)reader->Window->Pool,sizeof(*reader->Window->Pool)) ||
        os_address_ranges_overlap(base,bytes,(BOOT_U64)reader->Inventory,sizeof(*reader->Inventory)) ||
        os_address_ranges_overlap(base,bytes,(BOOT_U64)reader->Lapic,reader->Lapic?sizeof(*reader->Lapic):0) ||
        os_address_ranges_overlap(base,bytes,(BOOT_U64)reader->Context->MemoryMap,reader->Context->Info.MemoryMapCapacity);
}

// ====== read_private ======

static int read_private(void *state,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function,BOOT_U32 offset,BOOT_U32 *value)
{ return x86_ecam_read(state,segment,bus,device,function,offset,value); }

// ====== x86_pci_topology_native ======

int x86_pci_topology_native(X86_ECAM *reader,X86_PCI_TOPOLOGY *scratch,X86_PCI_TOPOLOGY *output)
{
    if (!x86_ecam_current(reader) || !scratch || !output || protected_private(reader,scratch) || protected_private(reader,output)) return 0;
    return x86_pci_topology_capture(&reader->Info,read_private,reader,scratch,output);
}
