#include "topology.h"

// ====== x86_pci_topology_valid ======

int x86_pci_topology_valid(const X86_PCI_TOPOLOGY *snapshot)
{
    if (!snapshot || ((BOOT_U64)snapshot&7) || snapshot->Signature!=X86_PCI_TOPOLOGY_SIGNATURE ||
        snapshot->Count>X86_PCI_TOPOLOGY_NODES || !snapshot->Reads || snapshot->Reads>X86_PCI_READ_LIMIT ||
        !x86_mcfg_valid(&snapshot->Source) || x86_pci_graph_check(snapshot->Nodes,snapshot->Count)!=snapshot->Bridges+1 || snapshot->Bridges>64) return 0;
    for (BOOT_U32 index=0; index<snapshot->Count; ++index) {
        const X86_PCI_NODE *node=&snapshot->Nodes[index]; BOOT_U32 matches=0;
        for (BOOT_U32 range=0; range<snapshot->Source.Count; ++range) {
            const X86_ECAM_RANGE *window=&snapshot->Source.Ranges[range];
            if (node->Segment==window->Segment && node->Bus>=window->First && node->Bus<=window->Last) ++matches;
        }
        if (matches!=1) return 0;
    }
    const unsigned char *unused=(const unsigned char *)&snapshot->Nodes[snapshot->Count];
    for (BOOT_U32 byte=0; byte<(X86_PCI_TOPOLOGY_NODES-snapshot->Count)*sizeof(X86_PCI_NODE); ++byte) if (unused[byte]) return 0;
    return 1;
}
