#ifndef X86_VTD_ROUTING_H
#define X86_VTD_ROUTING_H
#include "../discovery/table.h"
#include "arch/x86/kernel/drivers/bus/pci/topology/topology.h"
#define X86_VTD_ROUTE_INVALID 0
#define X86_VTD_ROUTE_ABSENT 1
#define X86_VTD_ROUTE_FOUND 2
typedef struct { BOOT_U32 Segment, Bus, Device, Function, Id; } X86_VTD_ROUTE_REQUEST;
typedef struct {
    BOOT_U64  Base, Bytes;
    BOOT_U32  Unit, Node, Scope, Kind;
    unsigned short  Segment, Requester;
    BOOT_U32  Reserved;
} X86_VTD_ROUTE;
_Static_assert(sizeof(X86_VTD_ROUTE_REQUEST)==20 && sizeof(X86_VTD_ROUTE)==40,"VT-d route ABI");
BOOT_U64 x86_vtd_route_word(const X86_DMAR_INFO *info,const X86_PCI_NODE *nodes,BOOT_U32 count,
    const X86_VTD_ROUTE_REQUEST *request,BOOT_U32 bits);
int x86_vtd_route_select(const X86_DMAR_INFO *info,const X86_PCI_TOPOLOGY *topology,
    const X86_VTD_ROUTE_REQUEST *request,X86_VTD_ROUTE *output);
#endif
