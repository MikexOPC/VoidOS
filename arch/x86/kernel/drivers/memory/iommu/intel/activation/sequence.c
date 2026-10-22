#include "activation.h"
#include "lib/memory/address_ranges.h"

// ====== x86_vtd_activation_valid ======

int x86_vtd_activation_valid(const X86_VTD_ACTIVATION *owner)
{
    if (!owner || owner->Self!=(BOOT_U64)owner || owner->State<X86_VTD_ACT_READY || owner->State>X86_VTD_ACT_RETAINED ||
        !owner->Qi || !owner->Domain || !x86_qi_valid(owner->Qi) || owner->Domain->Self!=(BOOT_U64)owner->Domain ||
        owner->Domain->State!=3 || owner->Domain->TableCount<5 || owner->Domain->TableCount>X86_VTD_DOMAIN_TABLES ||
        owner->Root!=owner->Domain->Tables[0].Base || owner->Source>65535 || owner->FaultCount<1 || owner->FaultCount>256 ||
        owner->FaultOffset<0x100 || (owner->FaultOffset&15) || owner->FaultOffset+owner->FaultCount*16>4096 ||
        owner->Logged>X86_VTD_FAULT_LOG) return 0;
    return 1;
}

// ====== retain_private ======

// Hardware may still walk the root/context tables or own the queue: nothing is released.
static int retain_private(X86_VTD_ACTIVATION *owner,BOOT_U32 failure)
{
    owner->Failure=failure; owner->State=X86_VTD_ACT_RETAINED;
    return 0;
}

// ====== wait_private ======

// Same bounded PM-clock deadline as the QI module: one second, 4096 reads, 256 stalled samples.
static int wait_private(X86_VTD_ACTIVATION *owner,BOOT_U32 expected)
{
    const BOOT_U32 mask=X86_VTD_GSTS_TES|X86_VTD_GSTS_RTPS|X86_QI_ENABLE;
    BOOT_U64 initial=x86_qi_ticks(owner->Qi),previous=initial;
    BOOT_U32 stagnant=0;
    if (initial==~0ULL) return 0;
    for (BOOT_U32 count=0; count<4096; ++count) {
        BOOT_U64 now=x86_qi_ticks(owner->Qi),status=x86_vtd_activation_read(owner,0x1c,4);
        if (now==~0ULL || status==~0ULL || ((now-initial)&owner->Qi->Clock.Mask)>=X86_PM_CLOCK_HZ ||
            (now==previous && ++stagnant==256)) return 0;
        if (now!=previous) stagnant=0;
        previous=now;
        if ((status&mask)==expected) return 1;
    }
    return 0;
}

// ====== command_private ======

// Intel VT-d5.0 11.4.4.1: read GSTS, drop the one-shot bits, change one field, write GCMD, wait GSTS.
static int command_private(X86_VTD_ACTIVATION *owner,BOOT_U32 bit,BOOT_U32 set,BOOT_U32 expected)
{
    BOOT_U64 status=x86_vtd_activation_read(owner,0x1c,4);
    if (status==~0ULL) return 0;
    BOOT_U64 value=x86_vtd_activation_command((BOOT_U32)status,bit,set);
    return value!=~0ULL && x86_vtd_activation_write(owner,0x18,4,value) && wait_private(owner,expected);
}

// ====== x86_vtd_activation_begin ======

int x86_vtd_activation_begin(X86_QI *qi,X86_VTD_DOMAIN *domain,const X86_VTD_ROUTE *route,X86_VTD_ACTIVATION *owner)
{
    if (!qi || !domain || !route || !owner || ((BOOT_U64)owner&7) || ((BOOT_U64)route&7) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)qi,sizeof(*qi)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)domain,sizeof(*domain)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)route,sizeof(*route)) ||
        !x86_qi_valid(qi) || os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)qi->Mapping,sizeof(*qi->Mapping)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)qi->Mapping->Window,sizeof(*qi->Mapping->Window)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),225ULL<<39,4ULL<<39) ||
        qi->State!=3 || qi->Pending || !qi->EnableIssued || qi->Status || qi->Mapping->Authority.Count!=1 ||
        // CAP.RWBF bit4 (11.4.2, 6.8): write-buffer flushing is not implemented, so it must not be required.
        (qi->Cap&(1ULL<<4)) || !x86_vtd_domain_verify(domain) || domain->State!=3 ||
        domain->Window!=qi->Mapping->Window || domain->TableCount<5 || route->Unit || route->Reserved ||
        route->Base!=qi->Mapping->Authority.Base[0] || route->Kind<1 || route->Kind>3 ||
        route->Segment!=domain->Request.Segment || domain->Request.Bus>255 || domain->Request.Device>31 ||
        domain->Request.Function>7 ||
        route->Requester!=((domain->Request.Bus<<8)|(domain->Request.Device<<3)|domain->Request.Function)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    owner->Self=(BOOT_U64)owner; owner->Qi=qi; owner->Domain=domain; owner->Root=domain->Tables[0].Base;
    owner->Source=route->Requester; owner->State=X86_VTD_ACT_READY;
    owner->FaultOffset=(BOOT_U32)((qi->Cap>>24)&1023)*16; owner->FaultCount=(BOOT_U32)((qi->Cap>>40)&255)+1;
    if (x86_vtd_activation_valid(owner)) return 1;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 0;
}

// ====== x86_vtd_activation_enable ======

int x86_vtd_activation_enable(X86_VTD_ACTIVATION *owner)
{
    if (!x86_vtd_activation_valid(owner) || owner->State!=X86_VTD_ACT_READY) return 0;
    X86_QI *qi=owner->Qi;
    // Observed before any write: only QIES set, RTADDR clear, no recorded fault; tables flushed (ECAP.C may be 0).
    if (qi->Status || x86_vtd_activation_read(owner,0x1c,4)!=X86_QI_ENABLE || x86_vtd_activation_read(owner,0x20,8) ||
        x86_vtd_activation_read(owner,0x34,4) || !x86_qi_clean_domain(owner->Domain)) return 0;
    owner->State=X86_VTD_ACT_ROOT_WRITTEN;
    // 11.4.5 RTADDR: RTA63:12, TTM11:10=00b legacy tables as built by the domain module.
    if (!x86_vtd_activation_write(owner,0x20,8,owner->Root) || x86_vtd_activation_read(owner,0x20,8)!=owner->Root)
        return retain_private(owner,1);
    if (!x86_qi_status_allow(qi,X86_VTD_GSTS_RTPS) ||
        !command_private(owner,X86_VTD_GCMD_SRTP,1,X86_VTD_GSTS_RTPS|X86_QI_ENABLE)) return retain_private(owner,2);
    owner->State=X86_VTD_ACT_ROOT_PUBLISHED;
    // 6.6: with CAP.ESRTPS63 clear, global context-cache then IOTLB invalidation; issued unconditionally.
    if (!x86_qi_submit(qi) || !x86_qi_finish(qi)) return retain_private(owner,3);
    owner->State=X86_VTD_ACT_INVALIDATED;
    if (!x86_qi_status_allow(qi,X86_VTD_GSTS_TES|X86_VTD_GSTS_RTPS) ||
        !command_private(owner,X86_VTD_GCMD_TE,1,X86_VTD_GSTS_TES|X86_VTD_GSTS_RTPS|X86_QI_ENABLE)) return retain_private(owner,4);
    owner->State=X86_VTD_ACT_TRANSLATING;
    return x86_vtd_domain_verify(owner->Domain) ? 1 : retain_private(owner,5);
}

// ====== x86_vtd_activation_disable ======

// TE=0 first, then a global invalidation, then RTADDR cleared: only after that may the tables be released.
int x86_vtd_activation_disable(X86_VTD_ACTIVATION *owner)
{
    if (!x86_vtd_activation_valid(owner) || owner->State!=X86_VTD_ACT_TRANSLATING) return 0;
    if (!command_private(owner,X86_VTD_GCMD_TE,0,X86_VTD_GSTS_RTPS|X86_QI_ENABLE)) return retain_private(owner,6);
    owner->State=X86_VTD_ACT_DRAINED;
    if (!x86_qi_status_allow(owner->Qi,X86_VTD_GSTS_RTPS) || !x86_qi_submit(owner->Qi) || !x86_qi_finish(owner->Qi))
        return retain_private(owner,7);
    owner->State=X86_VTD_ACT_FLUSHED;
    if (!x86_vtd_activation_write(owner,0x20,8,0) || x86_vtd_activation_read(owner,0x20,8)) return retain_private(owner,8);
    owner->State=X86_VTD_ACT_UNPUBLISHED;
    return 1;
}

// ====== x86_vtd_activation_end ======

// Closes a never-enabled or fully unpublished activation; QI keeps tolerating the sticky RTPS bit.
int x86_vtd_activation_end(X86_VTD_ACTIVATION *owner)
{
    if (!x86_vtd_activation_valid(owner) || owner->Owned ||
        (owner->State!=X86_VTD_ACT_READY && owner->State!=X86_VTD_ACT_UNPUBLISHED)) return 0;
    BOOT_U64 status=x86_vtd_activation_read(owner,0x1c,4),faults=x86_vtd_activation_read(owner,0x34,4);
    if (status==~0ULL || (status&X86_VTD_GSTS_TES) || faults==~0ULL || (faults&3) ||
        x86_vtd_activation_read(owner,0x20,8) || !x86_vtd_domain_verify(owner->Domain)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}
