#include "model.h"
X86_QI qi;
unsigned stall_clock,stall_enable,stall_disable,stall_completion,stale,errors,writes,completions,cleans;
BOOT_U64 device[512];
static BOOT_U32 ticks;
unsigned latched_mode;
static BOOT_U64 latched_iqa;
unsigned pin_at;
static unsigned pin_count;
int qi_real_pin(KERNEL_PHYSICAL_ALLOCATOR *owner,const KERNEL_PHYSICAL_LEASE *lease,BOOT_U32 pin);

// ====== kernel_physical_pin ======

int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner,const KERNEL_PHYSICAL_LEASE *lease,BOOT_U32 pin)
{
    if (pin && ++pin_count==pin_at) return 0;
    return qi_real_pin(owner,lease,pin);
}

// ====== qi_fixture ======

void qi_fixture(void)
{
    vtd_fixture(1); memset(&qi,0,sizeof(qi)); memset(device,0,sizeof(device));
    stall_clock=stall_enable=stall_disable=stall_completion=stale=errors=writes=completions=cleans=0; ticks=0xfffff0;
    pin_at=pin_count=0;
    latched_mode=0; latched_iqa=0;
    registers[0][1]|=(1ULL<<54)|(1ULL<<55); registers[0][2]=2|(0xfULL<<8);
    CHECK(x86_vtd_mapping_bind_access(&context,&window,0,&description,&inventory,&mapping,1));
    pin_count=0;
}

// ====== qi_close ======

void qi_close(void)
{
    CHECK(x86_qi_stop(&qi)); CHECK(!mapping.Consumer);
    CHECK(x86_vtd_mapping_unbind(&mapping) && pool.FreePages==128);
}

// ====== qi_enable_ack ======

void qi_enable_ack(void)
{ latched_iqa=device[0x90/8]; device[0x18/8]=(BOOT_U64)X86_QI_ENABLE<<32; }

// ====== modeled CPU operations ======

BOOT_U32 x86_qi_cpu_line(void) { return 64; }
int x86_qi_clean_page(void)
{
    BOOT_U64 address=(BOOT_U64)x86_window_bytes();
    CHECK(address>=(BOOT_U64)arena && address<=(BOOT_U64)arena+sizeof(arena)-4096 && !(address&4095));
    cleans+=64; return 1;
}
BOOT_U64 x86_qi_ticks(const X86_QI *owner)
{
    CHECK(x86_qi_valid(owner)); if (!stall_clock) ticks+=17;
    return ticks&owner->Clock.Mask;
}

// ====== modeled register reads ======

BOOT_U64 x86_qi_read(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width)
{
    CHECK(x86_qi_valid(owner) && offset<4096 && (width==4 || width==8));
    if (offset==0x34) return errors;
    if (offset==0x90 && latched_mode) return latched_iqa;
    if (width==4) return (device[offset/8]>>((offset&7)*8))&0xffffffff;
    return device[offset/8];
}

// ====== modeled queue engine ======

static void process_private(const X86_QI *owner,BOOT_U64 tail)
{
    const BOOT_U64 *queue=(const BOOT_U64 *)latched_iqa;
    unsigned head=(unsigned)device[0x80/8],processed=0;
    CHECK(queue==(const BOOT_U64 *)owner->Pages[0].Base);
    while (head!=tail) {
        CHECK(++processed<256);
        BOOT_U64 low=queue[head/8],high=queue[head/8+1]; unsigned type=(unsigned)(low&15);
        if (type==1) CHECK(low==0x11 && !high);
        else if (type==2) CHECK(low==0xd2 && !high);
        else {
            CHECK(type==5 && (low&0xffffffff)==0x65 && high==owner->Pages[1].Base);
            *(BOOT_U32 *)high=(BOOT_U32)(low>>32)-(stale?1U:0U); ++completions;
        }
        head=(head+16)&4095;
    }
    device[0x80/8]=head;
}

// ====== modeled register writes ======

int x86_qi_write(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U64 value)
{
    CHECK(x86_qi_valid(owner)); ++writes;
    CHECK((offset==0x18 && width==4) || ((offset==0x88 || offset==0x90) && width==8));
    if (offset==0x18) {
        CHECK(value==0 || value==X86_QI_ENABLE);
        if ((value && !stall_enable) || (!value && !stall_disable)) {
            if (value) latched_iqa=device[0x90/8];
            device[0x18/8]=value<<32;
            if (!value) device[0x80/8]=0;
        }
    } else {
        device[offset/8]=value;
        if (offset==0x88 && !stall_completion && ((device[0x18/8]>>32)&X86_QI_ENABLE)) process_private(owner,value);
    }
    return 1;
}
