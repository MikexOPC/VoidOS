#include "probe.h"
#include "kernel/diagnostics/log.h"
typedef struct { X86_DMAR_INFO Info; X86_VTD_ROUTE Result; BOOT_U32 Status, Reserved; } ROUTING_CASE;
_Static_assert(sizeof(ROUTING_CASE)==1040,"Synthetic routing witness ABI");
static ROUTING_CASE cases[4];

// ====== endpoint_private ======

static int endpoint_private(X86_DMAR_INFO *info,const X86_PCI_TOPOLOGY *topology,BOOT_U32 target)
{
    if (target>=topology->Count || topology->Nodes[target].Depth>7) return 0;
    X86_DMAR_SCOPE *scope=&info->Scope[0]; scope->Unit=3; scope->Type=1;
    scope->Depth=(unsigned char)(topology->Nodes[target].Depth+1);
    BOOT_U32 current=target;
    for (BOOT_U32 step=scope->Depth; step>0; --step) {
        if (current>=topology->Count) return 0;
        const X86_PCI_NODE *node=&topology->Nodes[current];
        scope->Path[(step-1)*2]=node->Device; scope->Path[(step-1)*2+1]=node->Function;
        if (step==1) scope->Bus=node->Bus;
        current=node->Parent;
    }
    if (current!=X86_PCI_NO_PARENT) return 0;
    info->Scopes=1; info->Unit[3].Flags=0; info->Unit[3].ScopeCount=1; info->Unit[3].Segment=topology->Nodes[target].Segment;
    for (BOOT_U32 index=4; index<8; ++index) info->Unit[index].FirstScope=1;
    return 1;
}

// ====== x86_vtd_routing_matrix ======

int x86_vtd_routing_matrix(const X86_PCI_TOPOLOGY *topology,const X86_VTD_ROUTE_REQUEST *request)
{
    X86_DMAR_INFO *info=&cases[0].Info; info->HostBits=48; info->Units=8;
    for (BOOT_U32 index=0; index<8; ++index) {
        info->Unit[index]=(X86_DMAR_UNIT){0x10000000ULL+index*4096,4096,(unsigned short)(index+1),1,0,0};
    }
    info->Unit[7].Segment=(unsigned short)request->Segment;
    cases[0].Status=(BOOT_U32)x86_vtd_route_select(info,topology,request,&cases[0].Result);
    if (cases[0].Status!=2 || cases[0].Result.Unit!=7 || cases[0].Result.Kind!=3) return 0;
    cases[1].Info=*info;
    if (!endpoint_private(&cases[1].Info,topology,cases[0].Result.Node)) return 0;
    cases[1].Status=(BOOT_U32)x86_vtd_route_select(&cases[1].Info,topology,request,&cases[1].Result);
    if (cases[1].Status!=2 || cases[1].Result.Unit!=3 || cases[1].Result.Kind!=1) return 0;
    cases[2].Info=cases[1].Info;
    X86_DMAR_SCOPE *scope=&cases[2].Info.Scope[0];
    if (scope->Depth>1) { --scope->Depth; scope->Type=2; scope->Path[scope->Depth*2]=0; scope->Path[scope->Depth*2+1]=0; }
    else { scope->Type=3; scope->Enumeration=0; }
    cases[2].Status=(BOOT_U32)x86_vtd_route_select(&cases[2].Info,topology,request,&cases[2].Result);
    if (cases[2].Status!=2 || cases[2].Result.Kind!=(scope->Type==2?2U:3U)) return 0;
    cases[3].Info=cases[1].Info; info=&cases[3].Info; info->Scopes=2;
    info->Scope[1]=info->Scope[0]; info->Scope[1].Unit=4;
    info->Unit[4].Segment=(unsigned short)request->Segment; info->Unit[4].Flags=0; info->Unit[4].ScopeCount=1;
    for (BOOT_U32 index=5; index<8; ++index) info->Unit[index].FirstScope=2;
    for (BOOT_U32 byte=0; byte<sizeof(cases[3].Result); ++byte) ((unsigned char *)&cases[3].Result)[byte]=0xa5;
    cases[3].Status=(BOOT_U32)x86_vtd_route_select(info,topology,request,&cases[3].Result);
    if (cases[3].Status) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(cases[3].Result); ++byte) if (((const unsigned char *)&cases[3].Result)[byte]!=0xa5) return 0;
    return kernel_text("KERNEL_ROUTING_SYNTHETIC_CASES: ") && kernel_number((BOOT_U64)cases) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_ROUTING_SYNTHETIC_4_OK");
}
