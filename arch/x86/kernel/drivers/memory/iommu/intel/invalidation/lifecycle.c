#include "qi.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== x86_qi_prepare ======

int x86_qi_prepare(X86_VTD_MAPPING *mapping,const X86_PM_CLOCK *clock,BOOT_U32 bits,X86_QI *owner)
{
    X86_VTD_CAPABILITIES caps;
    if (!mapping || !clock || !owner || ((BOOT_U64)owner&7) || mapping->Consumer || !mapping->Context || !mapping->Inventory || !mapping->Window || !mapping->Window->Pool ||
        mapping->Authority.Writable!=1 || mapping->Authority.Count!=1 || bits<32 || bits>52 || bits>mapping->Authority.PhysicalBits ||
        clock->Ready!=1 || !clock->Port || clock->Port>65535 || (clock->Mask!=0xffffff && clock->Mask!=0xffffffff) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)mapping,sizeof(*mapping)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)clock,sizeof(*clock)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)mapping->Window,sizeof(*mapping->Window)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)mapping->Window->Pool,sizeof(*mapping->Window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)mapping->Inventory,sizeof(*mapping->Inventory)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)mapping->Context,sizeof(*mapping->Context)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)mapping->Context->MemoryMap,mapping->Context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_LAYOUT_DEVICES_VIRTUAL,X86_LAYOUT_DEVICES_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        !x86_qi_cpu_line() || !x86_vtd_capabilities_read(mapping,0,&caps) || caps.Status || !(caps.Ecap&2) ||
        (caps.Version>>4)!=1 || caps.IotlbOffset>4080) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    owner->Self=(BOOT_U64)owner; owner->Mapping=mapping; owner->Clock=*clock; owner->PhysicalBits=bits;
    owner->Cap=caps.Cap; owner->IotlbOffset=caps.IotlbOffset; owner->State=1;
    owner->Budget=mapping->Window->Pool->FreePages; mapping->Consumer=(BOOT_U64)owner;
    if (x86_qi_read(owner,0x20,8) || x86_qi_read(owner,0x28,8) || x86_qi_read(owner,owner->IotlbOffset+8,8) ||
        x86_qi_read(owner,0x34,4) || x86_qi_read(owner,0x80,8) || x86_qi_read(owner,0x88,8)) goto failure;
    BOOT_U64 old_iqa=x86_qi_read(owner,0x90,8);
    if ((old_iqa&4095) || old_iqa>=(1ULL<<bits)) goto failure;
    BOOT_U64 before=x86_qi_ticks(owner); BOOT_U32 advances=0;
    if (before==~0ULL) goto failure;
    for (BOOT_U32 read=0; read<1024; ++read) { BOOT_U64 tick=x86_qi_ticks(owner); if (tick==~0ULL) goto failure; if (tick!=before) {advances=1;break;} }
    if (!advances) goto failure;
    for (BOOT_U32 index=0; index<2; ++index) {
        if (!kernel_physical_allocate(mapping->Window->Pool,1,&owner->Pages[index])) goto failure;
        ++owner->Allocated;
        if (!kernel_physical_pin(mapping->Window->Pool,&owner->Pages[index],1)) goto failure;
        owner->Pins|=1U<<index;
    }
    if (!x86_qi_memory(owner,0,0) || !x86_vtd_inventory_verify(mapping)) goto failure;
    return 1;
failure:
    (void)x86_qi_stop(owner);
    return 0;
}

// ====== x86_qi_stop ======

int x86_qi_stop(X86_QI *owner)
{
    if (!x86_qi_valid(owner) || !x86_vtd_inventory_verify(owner->Mapping)) return 0;
    // GCMD writes below rebuild the command from zero: translation must already be off (TES31).
    // While the activation owner still declares TES possible, the queue is needed for its teardown.
    if ((owner->Status&(1U<<31)) || (owner->EnableIssued && (x86_qi_read(owner,0x1c,4)&(1U<<31)))) return 0;
    if (owner->EnableIssued) {
        if (owner->State==2) { if (!x86_qi_poll(owner,0)) return 0; owner->State=3; }
        if (owner->State!=5) {
            if (owner->Pending && !x86_qi_finish(owner)) return 0;
            if (!owner->Completed && (!x86_qi_submit(owner) || !x86_qi_finish(owner))) return 0;
            if (x86_qi_read(owner,0x80,8)!=owner->Tail || x86_qi_read(owner,0x88,8)!=owner->Tail) return 0;
            owner->State=5;
            if (!x86_qi_write(owner,0x18,4,0)) return 0;
        }
        if (!x86_qi_poll(owner,2)) return 0;
        owner->EnableIssued=0;
    }
    if (owner->Configured) {
        BOOT_U64 address=x86_qi_read(owner,0x90,8);
        if ((x86_qi_read(owner,0x1c,4)&~(BOOT_U64)owner->Status) || (address && address!=owner->Pages[0].Base) ||
            !x86_qi_write(owner,0x88,8,0) || !x86_qi_write(owner,0x90,8,0) ||
            x86_qi_read(owner,0x80,8) || x86_qi_read(owner,0x88,8)) return 0;
        address=x86_qi_read(owner,0x90,8);
        // QEMU11.1 retains the latched read value after disable; QIES=0 and quiescence govern lifetime.
        if (address && address!=owner->Pages[0].Base) return 0;
        owner->Configured=0;
    }
    owner->State=6;
    for (BOOT_U32 count=owner->Allocated; count; --count) {
        if ((owner->Pins&(1U<<(count-1))) && !kernel_physical_pin(owner->Mapping->Window->Pool,&owner->Pages[count-1],0)) return 0;
        owner->Pins&=~(1U<<(count-1));
        if (!kernel_physical_release(owner->Mapping->Window->Pool,&owner->Pages[count-1])) return 0;
        --owner->Allocated;
    }
    if (owner->Mapping->Window->Pool->FreePages!=owner->Budget || !x86_vtd_inventory_verify(owner->Mapping)) return 0;
    owner->Mapping->Consumer=0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}
