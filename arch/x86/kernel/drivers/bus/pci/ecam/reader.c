#include "ecam.h"
#include "lib/memory/address_ranges.h"
typedef struct { BOOT_U32 Offset, Value; } ECAM_REQUEST;

// ====== x86_ecam_open ======

int x86_ecam_open(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *inventory,const X86_MCFG *info,X86_ECAM *owner)
{
    X86_FB_CPU cpu;
    if (!context || !window || !window->Pool || !inventory || !owner || ((BOOT_U64)owner&7) || !x86_mcfg_valid(info) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)info,sizeof(*info)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)context,sizeof(*context)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)window,sizeof(*window)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)window->Pool,sizeof(*window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)inventory,sizeof(*inventory)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)lapic,lapic?sizeof(*lapic):0) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)context->MemoryMap,context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),128ULL<<39,1ULL<<39) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),225ULL<<39,4ULL<<39) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        !x86_fb_cpu(&cpu) || cpu.PhysicalBits!=info->Bits || !x86_apic_uc_slot(cpu.Pat)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    owner->Self=(BOOT_U64)owner; owner->Context=context; owner->Window=window; owner->Lapic=lapic; owner->Inventory=inventory;
    owner->Info=*info; owner->Root=cpu.Root; owner->Pat=cpu.Pat; owner->Cache=x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat)-1);
    owner->Budget=window->Pool->FreePages; owner->Ready=1;
    int valid=x86_ecam_current(owner);
    for (BOOT_U32 index=0; valid && index<info->Count; ++index) {
        const X86_ECAM_RANGE *range=&info->Ranges[index];
        BOOT_U64 start=range->Base+((BOOT_U64)range->First<<20);
        BOOT_U64 end=x86_ecam_range_end(range->Base,range->First,range->Last,info->Bits);
        valid=x86_ecam_safe(owner,start,end-start);
    }
    if (!valid) { for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((unsigned char *)owner)[byte]=0; }
    return valid;
}

// ====== page_private ======

static BOOT_U64 page_private(const X86_ECAM *owner,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function)
{
    BOOT_U64 page=0;
    for (BOOT_U32 index=0; index<owner->Info.Count; ++index) {
        const X86_ECAM_RANGE *range=&owner->Info.Ranges[index];
        if (range->Segment==segment && bus>=range->First && bus<=range->Last) page=x86_ecam_page(range->Base,bus,device,function);
    }
    return page && x86_ecam_safe(owner,page,4096) ? page : 0;
}

// ====== read_private ======

static int read_private(void *state)
{
    ECAM_REQUEST *request=state;
    return x86_ecam_load(request->Offset,&request->Value);
}

// ====== x86_ecam_read ======

int x86_ecam_read(X86_ECAM *owner,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function,BOOT_U32 offset,BOOT_U32 *value)
{
    if (!value || ((BOOT_U64)value&3) || segment>65535 || bus>255 || device>31 || function>7 ||
        !x86_ecam_offset(offset) || !x86_ecam_current(owner) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner->Window,sizeof(*owner->Window)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner->Context,sizeof(*owner->Context)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner->Window->Pool,sizeof(*owner->Window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner->Inventory,sizeof(*owner->Inventory)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner->Context->MemoryMap,owner->Context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)owner->Lapic,owner->Lapic?sizeof(*owner->Lapic):0) ||
        os_address_ranges_overlap((BOOT_U64)value,4,128ULL<<39,1ULL<<39) ||
        os_address_ranges_overlap((BOOT_U64)value,4,225ULL<<39,4ULL<<39) ||
        os_address_ranges_overlap((BOOT_U64)value,4,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES)) return 0;
    BOOT_U64 page=page_private(owner,segment,bus,device,function);
    if (!page) return 0;
    ECAM_REQUEST request={offset,0};
    owner->Busy=1;
    int valid=x86_apic_borrow(owner->Window,page,owner->Cache,0,read_private,&request);
    owner->Busy=0;
    if (!x86_page_window_idle_valid(owner->Window)) { owner->Ready=2; return 0; }
    if (!valid || !x86_ecam_current(owner) || owner->Budget!=owner->Window->Pool->FreePages) return 0;
    *value=request.Value;
    return 1;
}

// ====== x86_ecam_close ======

int x86_ecam_close(X86_ECAM *owner)
{
    if (!owner || ((BOOT_U64)owner&7) || owner->Self!=(BOOT_U64)owner || (owner->Ready!=1 && owner->Ready!=2) || owner->Busy ||
        !owner->Window || !owner->Window->Pool || !x86_page_window_idle_valid(owner->Window) ||
        owner->Budget!=owner->Window->Pool->FreePages) return 0;
    BOOT_U32 ready=owner->Ready; owner->Ready=1;
    if (!x86_ecam_current(owner)) {owner->Ready=ready;return 0;}
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}

// ====== store_private ======

static int store_private(void *state)
{
    ECAM_REQUEST *request = state;
    return x86_ecam_store_command(request->Value);
}

// ====== x86_ecam_command ======

// Read Command, change only the allowed bits, write the 16-bit register through a writable UC
// borrow and read it back. The caller owns the function (diagnostic DMA tests).
int x86_ecam_command(X86_ECAM *owner, BOOT_U32 segment, BOOT_U32 bus, BOOT_U32 device, BOOT_U32 function,
    BOOT_U32 mask, BOOT_U32 value)
{
    BOOT_U32 old = 0;

    if (!x86_ecam_read(owner, segment, bus, device, function, 4, &old)) return 0;

    ECAM_REQUEST request = {4, x86_ecam_command_value(old, mask, value)};
    BOOT_U64 page = page_private(owner, segment, bus, device, function);

    if (request.Value == ~0U || !page) return 0;

    owner->Busy = 1;
    int valid = x86_apic_borrow(owner->Window, page, owner->Cache, 1, store_private, &request);
    owner->Busy = 0;

    if (!x86_page_window_idle_valid(owner->Window)) {
        owner->Ready = 2;
        return 0;
    }

    if (!valid || !x86_ecam_current(owner) || owner->Budget != owner->Window->Pool->FreePages ||
        !x86_ecam_read(owner, segment, bus, device, function, 4, &old)
    )
    return 0;

    return (old & mask) == value;
}
