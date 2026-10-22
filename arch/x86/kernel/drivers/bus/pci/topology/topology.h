#ifndef X86_PCI_TOPOLOGY_H
#define X86_PCI_TOPOLOGY_H
#include "../ecam/ecam.h"
#define X86_PCI_TOPOLOGY_SIGNATURE 0x3159504f54494350ULL
#define X86_PCI_TOPOLOGY_NODES 512U
#define X86_PCI_READ_LIMIT 150000U
#define X86_PCI_NO_PARENT 0xffffffffU
typedef struct {
    BOOT_U32  Id, Class, Header, Buses;
    unsigned short  Segment;
    unsigned char  Bus, Device, Function, Reserved[3];
    BOOT_U32  Parent, Depth;
} X86_PCI_NODE;
typedef struct {
    BOOT_U64  Signature;
    BOOT_U32  Count, Bridges;
    BOOT_U64  Reads;
    X86_MCFG  Source;
    X86_PCI_NODE  Nodes[X86_PCI_TOPOLOGY_NODES];
} X86_PCI_TOPOLOGY;
_Static_assert(sizeof(X86_PCI_NODE)==32 && sizeof(X86_PCI_TOPOLOGY)==16936,"PCI topology ABI");
typedef int (*X86_PCI_CONFIG_READ)(void *,BOOT_U32,BOOT_U32,BOOT_U32,BOOT_U32,BOOT_U32,BOOT_U32 *);
BOOT_U32 x86_pci_graph_build(X86_PCI_NODE *nodes,BOOT_U32 count);
BOOT_U32 x86_pci_graph_check(const X86_PCI_NODE *nodes,BOOT_U32 count);
int x86_pci_topology_valid(const X86_PCI_TOPOLOGY *snapshot);
int x86_pci_topology_capture(const X86_MCFG *source,X86_PCI_CONFIG_READ read,void *state,
    X86_PCI_TOPOLOGY *scratch,X86_PCI_TOPOLOGY *output);
int x86_pci_topology_native(X86_ECAM *reader,X86_PCI_TOPOLOGY *scratch,X86_PCI_TOPOLOGY *output);
#endif
