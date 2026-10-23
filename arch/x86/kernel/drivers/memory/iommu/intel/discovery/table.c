#include "table.h"
#include "lib/memory/address_ranges.h"

// ====== number_private ======

static uint64_t number_private(const unsigned char *bytes,uint32_t count)
{
    uint64_t value=0;
    for (uint32_t index=0; index<count; ++index) value|=(uint64_t)bytes[index]<<(8*index);
    return value;
}

// ====== scopes_private ======

// Intel VT-d5.0 8.3.1: scope header6, path device/function pairs; flags2 reserved in DRHD.
static int scopes_private(const unsigned char *bytes,uint32_t length,X86_DMAR_INFO *draft,uint32_t unit)
{
    uint32_t at=16;
    while (at<length) {
        if (length-at<6 || draft->Scopes==X86_DMAR_MAX_SCOPES) return 0;
        const unsigned char *scope=bytes+at;
        uint32_t size=scope[1];
        if (size>length-at) return 0;
        uint32_t depth=x86_dmar_scope_fields(scope[0],size,scope[2],scope[3],scope[4]);
        if (!depth || ((draft->Unit[unit].Flags&1) && scope[0]<=2)) return 0;
        X86_DMAR_SCOPE *output=&draft->Scope[draft->Scopes++];
        output->Unit=(uint8_t)unit; output->Type=scope[0]; output->Enumeration=scope[4];
        output->Bus=scope[5]; output->Depth=(uint8_t)depth;
        for (uint32_t pair=0; pair<depth; ++pair) {
            if (scope[6+pair*2]>31 || scope[7+pair*2]>7) return 0;
            output->Path[pair*2]=scope[6+pair*2]; output->Path[pair*2+1]=scope[7+pair*2];
        }
        at+=size;
    }
    return at==length;
}

// ====== unit_private ======

// Intel VT-d5.0 8.3: flags4/INCLUDE_PCI_ALL0, Size5[3:0], segment6, base8; min16.
static int unit_private(const unsigned char *bytes,uint32_t length,uint32_t bits,X86_DMAR_INFO *draft)
{
    if (length<16 || draft->Units==X86_DMAR_MAX_UNITS || (bytes[4]&~1U)) return 0;
    uint64_t base=number_private(bytes+8,8), span=x86_dmar_register_span(base,bytes[5],bits);
    uint16_t segment=(uint16_t)number_private(bytes+6,2);
    if (!span) return 0;
    for (uint32_t index=0; index<draft->Units; ++index) {
        const X86_DMAR_UNIT *other=&draft->Unit[index];
        if ((other->Segment==segment && (other->Flags&1)) || os_address_ranges_overlap(base,span,other->Base,other->Bytes)) return 0;
    }
    X86_DMAR_UNIT *unit=&draft->Unit[draft->Units];
    unit->Base=base; unit->Bytes=span; unit->Segment=segment;
    unit->Flags=bytes[4]; unit->FirstScope=(uint8_t)draft->Scopes;
    if (!scopes_private(bytes,length,draft,draft->Units)) return 0;
    unit->ScopeCount=draft->Scopes-unit->FirstScope;
    if (!unit->ScopeCount && !unit->Flags) return 0;
    ++draft->Units;
    return 1;
}

// ====== x86_dmar_decode ======

// Intel VT-d5.0 8.1/8.2: DMAR header48, revision1/checksum9, HAW36/flags37, records48.
int x86_dmar_decode(const unsigned char *data,size_t length,uint32_t physical_bits,X86_DMAR_INFO *output)
{
    X86_DMAR_INFO draft={0};
    if (!data || !output || ((uintptr_t)output%_Alignof(X86_DMAR_INFO)) ||
        length<48 || length>X86_DMAR_MAX_BYTES || physical_bits<32 || physical_bits>52 ||
        os_address_ranges_overlap((uintptr_t)data,length,(uintptr_t)output,sizeof(*output)) ||
        number_private(data,4)!=0x52414d44 || number_private(data+4,4)!=length) return 0;
    draft.HostBits=x86_dmar_header(data[36],data[37],data[8]); draft.Flags=data[37];
    if (!draft.HostBits) return 0;
    uint32_t sum=0;
    for (size_t byte=0; byte<length; ++byte) sum+=data[byte];
    if (sum&255) return 0;
    for (uint32_t byte=38; byte<48; ++byte) if (data[byte]) return 0;
    uint32_t previous=0;
    for (size_t at=48; at<length;) {
        if (length-at<4) return 0;
        uint32_t type=(uint32_t)number_private(data+at,2), size=(uint32_t)number_private(data+at+2,2);
        if (size<4 || size>length-at || type<previous) return 0;
        if (!type) { if (!unit_private(data+at,size,physical_bits,&draft)) return 0; }
        else {
            ++draft.OtherRecords;
            if (type == 1) ++draft.Rmrr;
        }
        previous=type; at+=size;
    }
    if (!draft.Units) return 0;
    for (uint32_t byte=0; byte<sizeof(draft); ++byte) ((volatile unsigned char *)output)[byte]=((const unsigned char *)&draft)[byte];
    return 1;
}
