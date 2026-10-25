#include "topology.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"
typedef struct {
    X86_PCI_CONFIG_READ  Read;
    void  *State;
    X86_PCI_TOPOLOGY  *Scratch;
    BOOT_U32  Verify, Seen;
} PCI_CAPTURE;

// ====== read_private ======

static int read_private(PCI_CAPTURE *capture,const X86_PCI_NODE *node,BOOT_U32 offset,BOOT_U32 *out)
{
    if (capture->Scratch->Reads>=X86_PCI_READ_LIMIT) return 0;
    ++capture->Scratch->Reads;
    return capture->Read(capture->State,node->Segment,node->Bus,node->Device,node->Function,offset,out);
}

// ====== function_private ======

// Intel316966-002 tables5-1/6-1: ID0,class8,header0x0c. Header type byte0x0e, multifunction bit7.
// 6.1.9/10/11: primary0x18,secondary0x19,subordinate0x1a; DWORD read only for type1 header.
static int function_private(PCI_CAPTURE *capture,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function,BOOT_U32 *multi)
{
    X86_PCI_NODE node={0};
    node.Segment=(unsigned short)segment; node.Bus=(unsigned char)bus; node.Device=(unsigned char)device; node.Function=(unsigned char)function;
    if (!read_private(capture,&node,0,&node.Id)) return 0;
    *multi=0;
    if ((node.Id&65535)==65535) return 1;
    if ((node.Id&65535)<=1 || !read_private(capture,&node,8,&node.Class) || !read_private(capture,&node,12,&node.Header)) return 0;
    *multi=(node.Header>>23)&1;
    if (((node.Header>>16)&127)==1 && !read_private(capture,&node,24,&node.Buses)) return 0;
    if (capture->Seen==X86_PCI_TOPOLOGY_NODES) return 0;
    if (!capture->Verify) capture->Scratch->Nodes[capture->Seen]=node;
    else {
        if (capture->Seen>=capture->Scratch->Count) return 0;
        const X86_PCI_NODE *old=&capture->Scratch->Nodes[capture->Seen];
        if (old->Id!=node.Id || old->Class!=node.Class || old->Header!=node.Header || old->Buses!=node.Buses ||
            old->Segment!=node.Segment || old->Bus!=node.Bus || old->Device!=node.Device || old->Function!=node.Function) return 0;
    }
    ++capture->Seen;
    return 1;
}

// ====== pass_private ======

static int pass_private(PCI_CAPTURE *capture)
{
    capture->Seen=0;
    for (BOOT_U32 range=0; range<capture->Scratch->Source.Count; ++range) {
        const X86_ECAM_RANGE *window=&capture->Scratch->Source.Ranges[range];
        for (BOOT_U32 bus=window->First; bus<=window->Last; ++bus) for (BOOT_U32 device=0; device<32; ++device) {
            BOOT_U32 multi=0;
            if (!function_private(capture,window->Segment,bus,device,0,&multi)) return 0;
            if (multi) for (BOOT_U32 function=1; function<8; ++function) {
                BOOT_U32 unused;
                if (!function_private(capture,window->Segment,bus,device,function,&unused)) return 0;
            }
        }
    }
    return 1;
}

// ====== storage_private ======

static int storage_private(const X86_MCFG *source,X86_PCI_TOPOLOGY *scratch,X86_PCI_TOPOLOGY *output)
{
    if (!scratch || !output || ((BOOT_U64)scratch&7) || ((BOOT_U64)output&7) ||
        os_address_ranges_overlap((BOOT_U64)scratch,sizeof(*scratch),(BOOT_U64)output,sizeof(*output)) ||
        os_address_ranges_overlap((BOOT_U64)scratch,sizeof(*scratch),(BOOT_U64)source,sizeof(*source)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)source,sizeof(*source))) return 0;
    const BOOT_U64 buffers[]={(BOOT_U64)scratch,(BOOT_U64)output};
    for (BOOT_U32 index=0; index<2; ++index)
        if (os_address_ranges_overlap(buffers[index],sizeof(*output),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
            os_address_ranges_overlap(buffers[index],sizeof(*output),X86_LAYOUT_DEVICES_VIRTUAL,X86_LAYOUT_DEVICES_BYTES) ||
            os_address_ranges_overlap(buffers[index],sizeof(*output),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES)) return 0;
    return 1;
}

// ====== x86_pci_topology_capture ======

int x86_pci_topology_capture(const X86_MCFG *source,X86_PCI_CONFIG_READ read,void *state,
    X86_PCI_TOPOLOGY *scratch,X86_PCI_TOPOLOGY *output)
{
    if (!read || !x86_mcfg_valid(source) || !storage_private(source,scratch,output)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*scratch); ++byte) ((unsigned char *)scratch)[byte]=0;
    scratch->Source=*source;
    PCI_CAPTURE capture={read,state,scratch,0,0};
    if (!pass_private(&capture)) return 0;
    scratch->Count=capture.Seen;
    BOOT_U32 graph=x86_pci_graph_build(scratch->Nodes,scratch->Count);
    if (!graph) return 0;
    scratch->Bridges=graph-1; capture.Verify=1;
    if (!pass_private(&capture) || capture.Seen!=scratch->Count) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*source); ++byte)
        if (((const unsigned char *)source)[byte]!=((const unsigned char *)&scratch->Source)[byte]) return 0;
    scratch->Signature=X86_PCI_TOPOLOGY_SIGNATURE;
    if (!x86_pci_topology_valid(scratch)) {scratch->Signature=0;return 0;}
    for (BOOT_U32 byte=0; byte<sizeof(*output); ++byte) ((volatile unsigned char *)output)[byte]=((const unsigned char *)scratch)[byte];
    return 1;
}
