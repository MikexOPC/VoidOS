#include "ecam.h"
#include "lib/memory/address_ranges.h"

// ====== number_private ======

static BOOT_U64 number_private(const unsigned char *bytes,BOOT_U32 count)
{
    BOOT_U64 value=0;
    for (BOOT_U32 index=0; index<count; ++index) value|=(BOOT_U64)bytes[index]<<(index*8);
    return value;
}

// ====== x86_mcfg_valid ======

int x86_mcfg_valid(const X86_MCFG *info)
{
    if (!info || ((BOOT_U64)info&7) || !info->Count || info->Count>32 || info->Bits<32 || info->Bits>52 ||
        (info->Physical && info->Physical>(1ULL<<info->Bits)-X86_MCFG_BYTES)) return 0;
    BOOT_U32 segments=0;
    for (BOOT_U32 index=0; index<32; ++index) {
        const X86_ECAM_RANGE *range=&info->Ranges[index];
        if (index>=info->Count) {
            if (range->Base || range->Segment || range->First || range->Last || range->Reserved) return 0;
            continue;
        }
        BOOT_U64 end=x86_ecam_range_end(range->Base,range->First,range->Last,info->Bits);
        if (!end || range->Reserved) return 0;
        BOOT_U64 start=range->Base+((BOOT_U64)range->First<<20);
        BOOT_U32 seen=0;
        for (BOOT_U32 other=0; other<index; ++other) {
            const X86_ECAM_RANGE *prior=&info->Ranges[other];
            BOOT_U64 prior_start=prior->Base+((BOOT_U64)prior->First<<20);
            BOOT_U64 prior_end=x86_ecam_range_end(prior->Base,prior->First,prior->Last,info->Bits);
            if (os_address_ranges_overlap(start,end-start,prior_start,prior_end-prior_start)) return 0;
            if (prior->Segment==range->Segment) {
                if (range->First<=prior->Last && prior->First<=range->Last) return 0;
                seen=1;
            }
        }
        if (!seen && ++segments>8) return 0;
    }
    return 1;
}

// ====== x86_mcfg_decode ======

// ACPICA/Intel actbl2.h MCFG v1: header36+reserved8; allocations16: base0,segment8,start10,end11,reserved12.
int x86_mcfg_decode(const unsigned char *bytes,BOOT_U32 length,BOOT_U32 bits,X86_MCFG *output)
{
    X86_MCFG draft={0};
    if (!bytes || !output || ((BOOT_U64)output&7) || length<60 || length>X86_MCFG_BYTES || (length-44)%16 ||
        os_address_ranges_overlap((BOOT_U64)bytes,length,(BOOT_U64)output,sizeof(*output)) ||
        number_private(bytes,4)!=0x4746434d || number_private(bytes+4,4)!=length || bytes[8]!=1) return 0;
    BOOT_U32 sum=0;
    for (BOOT_U32 index=0; index<length; ++index) sum+=bytes[index];
    if (sum&255) return 0;
    for (BOOT_U32 index=36; index<44; ++index) if (bytes[index]) return 0;
    draft.Bits=bits; draft.Count=(length-44)/16;
    for (BOOT_U32 index=0; index<draft.Count; ++index) {
        const unsigned char *entry=bytes+44+index*16;
        draft.Ranges[index]=(X86_ECAM_RANGE){number_private(entry,8),(unsigned short)number_private(entry+8,2),entry[10],entry[11],(BOOT_U32)number_private(entry+12,4)};
    }
    if (!x86_mcfg_valid(&draft)) return 0;
    *output=draft;
    return 1;
}
