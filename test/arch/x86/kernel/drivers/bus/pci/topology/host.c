#include "arch/x86/kernel/drivers/bus/pci/topology/topology.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static X86_PCI_TOPOLOGY scratch,output,before;
static X86_MCFG source;
static X86_PCI_NODE functions[520];
static unsigned checks,count,calls,fail_at,change_at,remove_at;
static KERNEL_BOOT_CONTEXT context;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGE_WINDOW window;
static X86_PAGING_INVENTORY inventory;
static X86_ECAM reader;
#define CHECK(x) do {++checks; if (!(x)) {printf("FAIL topology:%u\n",(unsigned)__LINE__);exit(1);}} while (0)

// ====== function_private ======

static X86_PCI_NODE function_private(unsigned bus,unsigned device,unsigned function,unsigned secondary,unsigned last)
{
    X86_PCI_NODE node={0}; node.Id=0x10501af4; node.Class=secondary?0x06040001:0x03000001; node.Header=secondary?1U<<16:0;
    node.Bus=(unsigned char)bus; node.Device=(unsigned char)device; node.Function=(unsigned char)function;
    if (secondary) node.Buses=bus|(secondary<<8)|(last<<16);
    return node;
}

// ====== fixture_private ======

static void fixture_private(void)
{
    memset(&source,0,sizeof(source)); memset(functions,0,sizeof(functions)); source.Count=2; source.Bits=52;
    source.Ranges[0]=(X86_ECAM_RANGE){0xe0000000,0,0,3,0}; source.Ranges[1]=(X86_ECAM_RANGE){0xf0000000,7,0,3,0};
    functions[0]=function_private(0,1,0,1,3); functions[1]=function_private(1,1,0,2,3);
    functions[2]=function_private(2,0,0,0,0); functions[3]=function_private(3,0,0,0,0);
    functions[4]=function_private(0,0,0,0,0); functions[4].Segment=7;
    count=5; calls=fail_at=change_at=remove_at=0;
    memset(&output,0xa5,sizeof(output)); before=output;
    memset(&reader,0,sizeof(reader)); reader.Info=source; reader.Ready=1; reader.Context=&context;
    reader.Window=&window; reader.Inventory=&inventory; window.Pool=&pool;
}

// ====== read_private ======

static int read_private(void *state,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function,BOOT_U32 offset,BOOT_U32 *value)
{
    CHECK(state==&source && bus<=255 && device<=31 && function<=7 && (offset==0 || offset==8 || offset==12 || offset==24));
    if (++calls==fail_at) return 0;
    if (remove_at && calls==remove_at) functions[2].Id=0xffffffff;
    if (change_at && calls==change_at) {
        if (change_at&1) functions[2].Id^=1U<<16;
        else { functions[5]=function_private(0,2,0,0,0); count=6; }
    }
    *value=0xffffffff;
    for (unsigned index=0; index<count; ++index) {
        const X86_PCI_NODE *node=&functions[index];
        if (node->Segment==segment && node->Bus==bus && node->Device==device && node->Function==function) {
            *value=offset==0?node->Id:offset==8?node->Class:offset==12?node->Header:node->Buses;
            break;
        }
    }
    return 1;
}

// ====== native adapter mocks ======

int x86_ecam_current(const X86_ECAM *owner) { return owner==&reader && owner->Ready==1; }
int x86_ecam_read(X86_ECAM *owner,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function,BOOT_U32 offset,BOOT_U32 *value)
{ CHECK(owner==&reader); return read_private(&source,segment,bus,device,function,offset,value); }

// ====== main ======

int main(void)
{
    fixture_private(); functions[3].Bus=1; functions[3].Device=2;
    CHECK(x86_pci_topology_capture(&source,read_private,&source,&scratch,&output));
    CHECK(output.Count==5 && output.Bridges==2 && x86_pci_topology_valid(&output));
    unsigned reads=(unsigned)output.Reads;
    for (unsigned failure=1; failure<=reads; ++failure) {
        fixture_private(); functions[3].Bus=1; functions[3].Device=2; fail_at=failure;
        CHECK(!x86_pci_topology_capture(&source,read_private,&source,&scratch,&output) && !memcmp(&output,&before,sizeof(output)) && !scratch.Signature);
    }
    fixture_private(); CHECK(!x86_pci_topology_capture(&source,read_private,&source,&scratch,&output) && !memcmp(&output,&before,sizeof(output)));
    for (unsigned cycle=0; cycle<100; ++cycle) {
        fixture_private(); functions[3].Bus=1; functions[3].Device=2;
        CHECK(x86_pci_topology_native(&reader,&scratch,&output) && x86_pci_topology_valid(&output));
        output.Nodes[2].Parent=511; CHECK(!x86_pci_topology_valid(&output));
    }
    fixture_private(); functions[3].Bus=1; functions[3].Device=2; change_at=(reads/2)|1;
    CHECK(!x86_pci_topology_capture(&source,read_private,&source,&scratch,&output) && !memcmp(&output,&before,sizeof(output)));
    fixture_private(); functions[3].Bus=1; functions[3].Device=2; change_at=(reads/2)+2;
    CHECK(!x86_pci_topology_capture(&source,read_private,&source,&scratch,&output) && !memcmp(&output,&before,sizeof(output)));
    fixture_private(); functions[3].Bus=1; functions[3].Device=2; remove_at=reads/2+1;
    CHECK(!x86_pci_topology_capture(&source,read_private,&source,&scratch,&output) && !memcmp(&output,&before,sizeof(output)));
    fixture_private(); functions[3].Bus=1; functions[3].Device=2;
    CHECK(x86_pci_topology_capture(&source,read_private,&source,&scratch,&output));
    output.Nodes[output.Count].Id=1; CHECK(!x86_pci_topology_valid(&output));
    output.Nodes[output.Count].Id=0; output.Nodes[2].Depth^=1; CHECK(!x86_pci_topology_valid(&output));
    fixture_private(); CHECK(!x86_pci_topology_capture(&source,read_private,&source,&output,&output));
    CHECK(!x86_pci_topology_native(&reader,(X86_PCI_TOPOLOGY *)&reader,&output));
    fixture_private(); source.Count=1; source.Ranges[1]=(X86_ECAM_RANGE){0}; source.Ranges[0].Last=31; count=513;
    for (unsigned index=0; index<count; ++index) functions[index]=function_private(index/32,index%32,0,0,0);
    CHECK(!x86_pci_topology_capture(&source,read_private,&source,&scratch,&output) && !memcmp(&output,&before,sizeof(output)));
    printf("PASS %u topology checks; every %u callback failure, 100 native adapter captures, changed/new/removed devices rejected\n",checks,reads);
    return 0;
}
