#include "routing.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== storage_private ======

static int storage_private(const X86_DMAR_INFO *info,const X86_PCI_TOPOLOGY *topology,
    const X86_VTD_ROUTE_REQUEST *request,const X86_VTD_ROUTE *output)
{
    if (!info || !topology || !request || !output || ((BOOT_U64)info&7) || ((BOOT_U64)request&3) ||
        ((BOOT_U64)topology&7) || ((BOOT_U64)output&7)) return 0;
    BOOT_U64 base=(BOOT_U64)output,bytes=sizeof(*output);
    return !os_address_ranges_overlap(base,bytes,(BOOT_U64)info,sizeof(*info)) &&
        !os_address_ranges_overlap(base,bytes,(BOOT_U64)topology,sizeof(*topology)) &&
        !os_address_ranges_overlap(base,bytes,(BOOT_U64)request,sizeof(*request)) &&
        !os_address_ranges_overlap(base,bytes,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) &&
        !os_address_ranges_overlap(base,bytes,X86_LAYOUT_DEVICES_VIRTUAL,X86_LAYOUT_DEVICES_BYTES) &&
        !os_address_ranges_overlap(base,bytes,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES);
}

// ====== x86_vtd_route_select ======

// Threat: forged firmware paths or overlapping output must not publish a partial remapper selection.
int x86_vtd_route_select(const X86_DMAR_INFO *info,const X86_PCI_TOPOLOGY *topology,
    const X86_VTD_ROUTE_REQUEST *request,X86_VTD_ROUTE *output)
{
    if (!storage_private(info,topology,request,output) || !x86_pci_topology_valid(topology)) return X86_VTD_ROUTE_INVALID;
    BOOT_U64 word=x86_vtd_route_word(info,topology->Nodes,topology->Count,request,topology->Source.Bits);
    if (word<=1) return (int)word;
    BOOT_U32 unit=(BOOT_U32)(word&255),kind=(BOOT_U32)((word>>8)&255),scope=(BOOT_U32)((word>>16)&255),node=(BOOT_U32)(word>>32);
    if ((word&~0x000001ff00ffffffULL) || unit<2 || unit>9 || unit-2>=info->Units ||
        node>=topology->Count || kind<1 || kind>3) return X86_VTD_ROUTE_INVALID;
    unit-=2;
    const X86_DMAR_UNIT *owner=&info->Unit[unit]; const X86_PCI_NODE *gpu=&topology->Nodes[node];
    if (owner->Segment!=request->Segment || gpu->Segment!=request->Segment || gpu->Bus!=request->Bus ||
        gpu->Device!=request->Device || gpu->Function!=request->Function || gpu->Id!=request->Id ||
        (kind==3 && (scope!=32 || owner->Flags!=1)) ||
        (kind!=3 && (scope>=info->Scopes || info->Scope[scope].Unit!=unit || info->Scope[scope].Type!=kind))) return X86_VTD_ROUTE_INVALID;
    X86_VTD_ROUTE draft={0}; draft.Base=owner->Base; draft.Bytes=owner->Bytes;
    draft.Unit=unit; draft.Node=node; draft.Scope=kind==3?0xffffffffU:scope; draft.Kind=kind;
    draft.Segment=owner->Segment; draft.Requester=(unsigned short)((gpu->Bus<<8)|(gpu->Device<<3)|gpu->Function);
    for (BOOT_U32 byte=0; byte<sizeof(draft); ++byte) ((volatile unsigned char *)output)[byte]=((const unsigned char *)&draft)[byte];
    return X86_VTD_ROUTE_FOUND;
}
