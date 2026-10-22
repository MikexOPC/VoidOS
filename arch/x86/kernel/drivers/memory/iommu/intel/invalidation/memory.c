#include "qi.h"
#include "arch/x86/mm/window/access.h"
#include "lib/memory/address_ranges.h"
typedef struct { X86_QI *Owner; BOOT_U32 Action, Page, Value; } QI_MEMORY;

// ====== memory_private ======

// Intel VT-d5.0 6.5.2.1/3/9: 128-bit descriptors; SW5/FN6, status data[63:32], coherent DWORD completion.
static int memory_private(void *opaque)
{
    QI_MEMORY *request=opaque; X86_QI *owner=request->Owner;
    volatile BOOT_U64 *words=(volatile BOOT_U64 *)x86_window_bytes();
    if (!words) return 0;
    if (request->Action==2) { request->Value=*(const volatile BOOT_U32 *)words; return 1; }
    if (request->Action==0) for (BOOT_U32 slot=0; slot<512; ++slot) words[slot]=0;
    else if (request->Page==1) *(volatile BOOT_U32 *)words=0;
    else for (BOOT_U32 kind=0; kind<3; ++kind) {
        BOOT_U32 slot=((owner->Tail/16)+kind)&255;
        for (BOOT_U32 high=0; high<2; ++high) {
            BOOT_U64 value=x86_qi_descriptor(kind,high,owner->Token,owner->Pages[1].Base,owner->Cap);
            if (value==~0ULL) return 0;
            words[slot*2+high]=value;
            if (words[slot*2+high]!=value) return 0;
        }
    }
    return x86_qi_clean_page();
}

// ====== x86_qi_memory ======

int x86_qi_memory(X86_QI *owner,BOOT_U32 action,BOOT_U32 *status)
{
    if (!x86_qi_valid(owner) || owner->Allocated!=2 || owner->Pins!=3 || action>2 ||
        (action==2 && (!status || ((BOOT_U64)status&3) ||
            os_address_ranges_overlap((BOOT_U64)status,4,(BOOT_U64)owner,sizeof(*owner)) ||
            os_address_ranges_overlap((BOOT_U64)status,4,(BOOT_U64)owner->Mapping,sizeof(*owner->Mapping)) ||
            os_address_ranges_overlap((BOOT_U64)status,4,(BOOT_U64)owner->Mapping->Context,sizeof(*owner->Mapping->Context)) ||
            os_address_ranges_overlap((BOOT_U64)status,4,(BOOT_U64)owner->Mapping->Window,sizeof(*owner->Mapping->Window)) ||
            os_address_ranges_overlap((BOOT_U64)status,4,(BOOT_U64)owner->Mapping->Inventory,sizeof(*owner->Mapping->Inventory)) ||
            os_address_ranges_overlap((BOOT_U64)status,4,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
            os_address_ranges_overlap((BOOT_U64)status,4,128ULL<<39,1ULL<<39) ||
            os_address_ranges_overlap((BOOT_U64)status,4,225ULL<<39,4ULL<<39) ||
            os_address_ranges_overlap((BOOT_U64)status,4,(BOOT_U64)owner->Mapping->Window->Pool,sizeof(*owner->Mapping->Window->Pool)))) ||
        (action!=2 && owner->Pending)) return 0;
    QI_MEMORY request={owner,action,1,0};
    if (!x86_apic_borrow(owner->Mapping->Window,owner->Pages[1].Base,0,action!=2,memory_private,&request)) return 0;
    if (action==2) *status=request.Value;
    else {
        request.Page=0;
        if (!x86_apic_borrow(owner->Mapping->Window,owner->Pages[0].Base,0,1,memory_private,&request)) return 0;
    }
    return x86_page_window_idle_valid(owner->Mapping->Window);
}

// ====== clean_private ======

static int clean_private(void *opaque) { (void)opaque; return x86_qi_clean_page(); }

// ====== x86_qi_clean_domain ======

int x86_qi_clean_domain(X86_VTD_DOMAIN *domain)
{
    if (!x86_qi_cpu_line() || !x86_vtd_domain_verify(domain)) return 0;
    for (BOOT_U32 table=0; table<domain->TableCount; ++table)
        if (!x86_apic_borrow(domain->Window,domain->Tables[table].Base,0,0,clean_private,domain)) return 0;
    return x86_vtd_domain_verify(domain);
}
