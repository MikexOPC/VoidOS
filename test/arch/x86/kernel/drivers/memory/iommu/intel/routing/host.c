#include "arch/x86/kernel/drivers/memory/iommu/intel/routing/routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static X86_DMAR_INFO info,saved;
static X86_PCI_TOPOLOGY topology;
static X86_VTD_ROUTE_REQUEST request;
static X86_VTD_ROUTE output,before;
static unsigned checks,refused;
#define CHECK(x) do {++checks; if (!(x)) {printf("FAIL routing:%u\n",(unsigned)__LINE__);exit(1);}} while (0)

// ====== fixture_private ======

static void fixture_private(void)
{
    memset(&info,0,sizeof(info)); memset(&topology,0,sizeof(topology));
    info.HostBits=48; info.Units=2; info.Scopes=1;
    info.Unit[0]=(X86_DMAR_UNIT){0xfed90000,4096,0,0,0,1};
    info.Unit[1]=(X86_DMAR_UNIT){0xfed91000,4096,0,1,1,0};
    info.Scope[0].Type=1; info.Scope[0].Depth=2; info.Scope[0].Path[0]=1;
    topology.Signature=X86_PCI_TOPOLOGY_SIGNATURE; topology.Count=3; topology.Bridges=1; topology.Reads=1;
    topology.Source.Count=1; topology.Source.Bits=48; topology.Source.Ranges[0]=(X86_ECAM_RANGE){0xe0000000,0,0,1,0};
    topology.Nodes[0]=(X86_PCI_NODE){0x00011b36,0x06040001,1U<<16,0x010100,0,0,1,0,{0},0,0};
    topology.Nodes[1]=(X86_PCI_NODE){0x10501af4,0x03000001,0,0,0,1,0,0,{0},0,0};
    topology.Nodes[2]=(X86_PCI_NODE){0x000129c0,0x06000001,0,0,0,0,0,0,{0},0,0};
    CHECK(x86_pci_graph_build(topology.Nodes,3)==2 && x86_pci_topology_valid(&topology));
    request=(X86_VTD_ROUTE_REQUEST){0,1,0,0,0x10501af4};
    memset(&output,0xa5,sizeof(output)); before=output;
}

// ====== unchanged_private ======

static void unchanged_private(int expected)
{
    CHECK(x86_vtd_route_select(&info,&topology,&request,&output)==expected && !memcmp(&output,&before,sizeof(output)));
}

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<1000; ++cycle) {
        fixture_private(); CHECK(x86_vtd_route_select(&info,&topology,&request,&output)==2);
        CHECK(output.Unit==0 && output.Node==1 && output.Scope==0 && output.Kind==1 && output.Requester==0x100 &&
            output.Base==0xfed90000 && output.Bytes==4096 && !output.Reserved && !output.Segment);
    }
    fixture_private(); info.Scope[0].Depth=1; memset(info.Scope[0].Path,0,16);
    CHECK(x86_vtd_route_select(&info,&topology,&request,&output)==2 && output.Unit==1 && output.Kind==3 && output.Scope==0xffffffff);
    info.Unit[1].Segment=7; output=before; unchanged_private(1);
    fixture_private(); info.Scope[0].Type=2; info.Scope[0].Depth=1;
    CHECK(x86_vtd_route_select(&info,&topology,&request,&output)==2 && output.Unit==0 && output.Kind==2);
    fixture_private(); saved=info;
    for (unsigned byte=0; byte<sizeof(info); ++byte) {
        info=saved; ((unsigned char *)&info)[byte]^=255; output=before;
        int result=x86_vtd_route_select(&info,&topology,&request,&output);
        CHECK(result>=0 && result<=2);
        if (result!=2) {++refused; CHECK(!memcmp(&output,&before,sizeof(output)));}
        else CHECK(output.Unit<info.Units && output.Node==1 && output.Requester==0x100);
    }
    fixture_private();
    for (unsigned bdf=0; bdf<65536; ++bdf) {
        request.Bus=bdf>>8; request.Device=(bdf>>3)&31; request.Function=bdf&7; output=before;
        int result=x86_vtd_route_select(&info,&topology,&request,&output);
        if (bdf==0x100) CHECK(result==2 && output.Requester==bdf);
        else CHECK(result==0 && !memcmp(&output,&before,sizeof(output)));
    }
    fixture_private();
    CHECK(!x86_vtd_route_select(&info,&topology,&request,(X86_VTD_ROUTE *)&info));
    CHECK(!x86_vtd_route_select(&info,&topology,&request,(X86_VTD_ROUTE *)&topology));
    CHECK(!x86_vtd_route_select(&info,&topology,&request,(X86_VTD_ROUTE *)&request));
    CHECK(!x86_vtd_route_select(&info,&topology,&request,(X86_VTD_ROUTE *)X86_KERNEL_PAGE_WINDOW));
    CHECK(!x86_vtd_route_select(0,&topology,&request,&output));
    CHECK(!x86_vtd_route_word(0,topology.Nodes,3,&request,48));
    CHECK(!x86_vtd_route_word(&info,topology.Nodes,513,&request,48));
    CHECK(!x86_vtd_route_word(&info,(const X86_PCI_NODE *)((const unsigned char *)topology.Nodes+1),3,&request,48));
    CHECK(!x86_vtd_route_word(&info,topology.Nodes,3,0,48));
    printf("PASS %u routing checks; 1000 selections, 65536 BDFs, 992 DMAR mutations (%u rejected), immutable failures and alias guards\n",checks,refused);
    return 0;
}
