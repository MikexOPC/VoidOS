#include "model.h"
BOOT_U64 rtaddr,records[ACT_NFR][2];
BOOT_U32 fsts,stall_srtp,stall_te_on,stall_te_off,bad_readback,refill,act_writes,refused;
ACT_WRITE act_log[64];

// ====== GSTS shared with the QI model (upper half of device[0x18/8]) ======

BOOT_U32 act_status(void) { return (BOOT_U32)(device[0x18/8]>>32); }
void act_set_status(BOOT_U32 value) { device[0x18/8]=(BOOT_U64)value<<32; }

// ====== pending_private ======

// Intel VT-d5.0 7.2.1: PPF is the OR of every F bit; FRI points at the first recorded slot.
static void pending_private(void)
{
    BOOT_U32 pending=0;
    for (BOOT_U32 slot=0; slot<ACT_NFR; ++slot) pending|=(BOOT_U32)(records[slot][1]>>63);
    fsts=(fsts&~2U)|(pending<<1);
}

// ====== act_fixture ======

void act_fixture(void)
{
    qi_fixture();
    registers[0][1]|=3ULL<<40; // NFR=3: four fault recording registers at FRO 0x22*16.
    rtaddr=0; fsts=0; memset(records,0,sizeof(records)); memset(act_log,0,sizeof(act_log));
    stall_srtp=stall_te_on=stall_te_off=bad_readback=refill=act_writes=refused=0;
}

// ====== act_inject ======

void act_inject(BOOT_U32 slot,BOOT_U32 source,BOOT_U32 reason,BOOT_U32 read,BOOT_U64 address)
{
    if (records[slot][1]>>63) { fsts|=1; return; }
    if (!(fsts&2)) fsts=(fsts&~0xff00U)|(slot<<8);
    records[slot][0]=address&~4095ULL;
    records[slot][1]=(1ULL<<63)|((BOOT_U64)read<<62)|((BOOT_U64)reason<<32)|source;
    pending_private();
}

// ====== x86_vtd_activation_read ======

BOOT_U64 x86_vtd_activation_read(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width)
{
    CHECK(x86_vtd_activation_valid(owner));
    if (!x86_vtd_activation_allowed(offset,width,0,0,owner->Root,owner->FaultOffset,owner->FaultCount)) { ++refused; return ~0ULL; }
    if (offset==0x1c) return act_status();
    if (offset==0x20) return bad_readback && rtaddr?rtaddr^4096:rtaddr;
    if (offset==0x34) return fsts;
    if (offset==0x38) return 1U<<31;
    return records[(offset-ACT_FRO)/16][(offset&8)?1:0];
}

// ====== x86_vtd_activation_write ======

int x86_vtd_activation_write(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U64 value)
{
    CHECK(x86_vtd_activation_valid(owner));
    if (!x86_vtd_activation_allowed(offset,width,1,value,owner->Root,owner->FaultOffset,owner->FaultCount)) { ++refused; return 0; }
    CHECK(act_writes<64); act_log[act_writes].Offset=offset; act_log[act_writes].Value=value; ++act_writes;
    if (offset==0x18) {
        BOOT_U32 status=act_status();
        if ((value&X86_VTD_GCMD_SRTP) && !stall_srtp) status|=X86_VTD_GSTS_RTPS;
        if ((value&X86_VTD_GCMD_TE) && !stall_te_on) status|=X86_VTD_GSTS_TES;
        if (!(value&X86_VTD_GCMD_TE) && !stall_te_off) status&=~X86_VTD_GSTS_TES;
        act_set_status(status);
    } else if (offset==0x20) rtaddr=value;
    else if (offset==0x34) fsts&=~1U;
    else {
        BOOT_U32 slot=(offset-ACT_FRO)/16;
        records[slot][1]&=~(1ULL<<63); pending_private();
        if (refill) act_inject(slot,0x00fa,2,0,0x7000);
    }
    return 1;
}
