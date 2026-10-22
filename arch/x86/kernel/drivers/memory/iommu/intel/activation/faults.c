#include "activation.h"

// ====== record_private ======

static void record_private(X86_VTD_ACTIVATION *owner,BOOT_U64 decoded,BOOT_U64 low,BOOT_U32 slot)
{
    BOOT_U32 source=(BOOT_U32)(decoded&0xffff);
    // Deny by default: the owned requester must never fault; any other source ID is blocked DMA.
    if (source==owner->Source) { if (owner->Owned!=~0U) ++owner->Owned; }
    else if (owner->Blocked!=~0U) ++owner->Blocked;
    if (owner->Logged>=X86_VTD_FAULT_LOG) return;
    X86_VTD_FAULT *entry=&owner->Log[owner->Logged++];
    entry->Address=low&~4095ULL; entry->Source=source; entry->Reason=(BOOT_U32)((decoded>>16)&255);
    entry->Read=(BOOT_U32)((decoded>>24)&1); entry->Index=slot;
}

// ====== x86_vtd_activation_faults ======

// Intel VT-d5.0 7.2.1 and 11.4.7.1/6: primary fault logging, polled (FECTL.IM stays 1). Records are
// consumed as a circular FIFO from FSTS.FRI until F=0; F and then PFO are cleared by writing 1.
// Returns 1 only when nothing is pending and no fault of the owned requester was ever seen.
int x86_vtd_activation_faults(X86_VTD_ACTIVATION *owner)
{
    if (!x86_vtd_activation_valid(owner) || owner->State==X86_VTD_ACT_RETAINED || owner->Scans==~0U) return 0;
    ++owner->Scans;
    for (BOOT_U32 pass=0; pass<4; ++pass) {
        BOOT_U64 status=x86_vtd_activation_read(owner,0x34,4);
        if (status==~0ULL) return 0;
        if (!(status&3)) return !owner->Owned;
        BOOT_U32 index=(BOOT_U32)((status>>8)&255);
        if (index>=owner->FaultCount) return 0;
        for (BOOT_U32 step=0; step<owner->FaultCount; ++step) {
            BOOT_U32 slot=(index+step)%owner->FaultCount,offset=owner->FaultOffset+slot*16;
            BOOT_U64 decoded=x86_vtd_fault_decode(x86_vtd_activation_read(owner,offset+8,8));
            if (!decoded) break;
            BOOT_U64 low=x86_vtd_activation_read(owner,offset,8);
            if (decoded==~0ULL || low==~0ULL) return 0;
            record_private(owner,decoded,low,slot);
            if (!x86_vtd_activation_write(owner,offset+8,8,1ULL<<63)) return 0;
        }
        if (status&1) {
            if (owner->Overflows!=~0U) ++owner->Overflows;
            if (!x86_vtd_activation_write(owner,0x34,4,1)) return 0;
        }
    }
    return 0; // Still pending after four passes: a requester keeps faulting.
}
