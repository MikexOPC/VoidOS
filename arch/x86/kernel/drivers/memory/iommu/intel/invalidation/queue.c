#include "qi.h"

// ====== x86_qi_start ======

// Intel VT-d5.0 6.5.2,11.4.4/9: IQT=0, IQA aligned/QS=0/DW=0, QIE26; TES31 stays zero.
int x86_qi_start(X86_QI *owner)
{
    if (!x86_qi_valid(owner) || owner->State!=1 || owner->Allocated!=2 || owner->Pins!=3 ||
        !x86_vtd_inventory_verify(owner->Mapping) || x86_qi_read(owner,0x1c,4) ||
        x86_qi_read(owner,0x80,8) || x86_qi_read(owner,0x88,8) || x86_qi_read(owner,0x34,4)) return 0;
    owner->Configured=1;
    owner->Failure=1; if (!x86_qi_write(owner,0x88,8,0)) return 0;
    owner->Failure=2; if (!x86_qi_write(owner,0x90,8,owner->Pages[0].Base)) return 0;
    owner->EnableIssued=1; owner->State=2;
    if (!x86_qi_write(owner,0x18,4,X86_QI_ENABLE) || !x86_qi_poll(owner,0)) return 0;
    owner->State=3;
    // QEMU11.1 vtd_mem_read returns the base latched by QIE; verify after the enable acknowledgement.
    owner->Failure=3; if (x86_qi_read(owner,0x90,8)!=owner->Pages[0].Base) return 0;
    owner->Failure=0;
    return x86_vtd_inventory_verify(owner->Mapping);
}

// ====== x86_qi_submit ======

int x86_qi_submit(X86_QI *owner)
{
    if (!x86_qi_valid(owner) || owner->State!=3 || owner->Pending || !owner->EnableIssued || owner->Token==0xffffffff ||
        !x86_vtd_inventory_verify(owner->Mapping) || (x86_qi_read(owner,0x1c,4)&~(BOOT_U64)owner->Status)!=X86_QI_ENABLE ||
        x86_qi_read(owner,0x90,8)!=owner->Pages[0].Base ||
        x86_qi_read(owner,0x80,8)!=owner->Tail || x86_qi_read(owner,0x88,8)!=owner->Tail) return 0;
    ++owner->Token;
    if (!x86_qi_memory(owner,1,0)) return 0;
    owner->Tail=(owner->Tail+48)&4095; owner->Pending=1; owner->Completed=0;
    if (!x86_qi_write(owner,0x88,8,owner->Tail)) return 0;
    return 1;
}

// ====== x86_qi_finish ======

int x86_qi_finish(X86_QI *owner)
{
    if (!x86_qi_valid(owner) || !owner->Pending || owner->State!=3 ||
        !x86_qi_poll(owner,1) || !x86_vtd_inventory_verify(owner->Mapping)) return 0;
    owner->Pending=0; owner->Completed=1;
    return 1;
}
