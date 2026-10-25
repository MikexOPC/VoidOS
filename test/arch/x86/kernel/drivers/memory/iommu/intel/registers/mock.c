#include "mock.h"
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_PAGING_INVENTORY inventory;
X86_VTD_MAPPING mapping;
X86_DMAR_INFO description;
BOOT_U64 roots[512],registers[8][512];
__declspec(align(4096)) BOOT_U64 arena[128][512];
unsigned checks,failure;
static unsigned char memory_map[48];
static BOOT_U64 *borrowed;

// ====== x86_paging_inventory_capture ======

int x86_paging_inventory_capture(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,X86_PAGING_INVENTORY *out)
{
    const X86_IOMMU_AUTH *auth=x86_iommu_owner();
    if (failure==3 && auth) return 0;
    memset(out,0,sizeof(*out)); out->Root=boot->Info.PageTableRoot; out->Count=1; out->Ready=1;
    out->Tables[0]=(X86_PAGING_RESOURCE){0x400000,0x400000,0,0};
    if (!x86_iommu_inputs(boot,allocator,(BOOT_U64)out,sizeof(*out),&out->Iommu)) return 0;
    if (!auth) return !roots[X86_IOMMU_SLOT];
    for (BOOT_U32 table=0; table<3; ++table) {
        const BOOT_U64 *page=(const BOOT_U64 *)auth->Tables[table].Base;
        BOOT_U64 view=(BOOT_U64)X86_IOMMU_SLOT<<(12+table*9);
        out->Tables[out->Count++]=(X86_PAGING_RESOURCE){auth->Tables[table].Base,view,table+1,32+table};
        CHECK(x86_iommu_record(&out->Iommu,auth->Tables[table].Base,view,table+1,32+table));
        for (BOOT_U32 slot=0; slot<512; ++slot) {
            BOOT_U64 child=table<2?auth->Tables[table+1].Base:0,base=0;
            if (table==2 && slot && slot<=15 && (slot&1) && slot/2<auth->Count) base=auth->Base[slot/2];
            if (page[slot]!=x86_vtd_entry_access(table,slot,base,auth->Cache,child,(BOOT_U32)auth->Writable)) return 0;
        }
    }
    out->MmioPages=auth->Count; out->LeafPages=auth->Count;
    return x86_iommu_complete(&out->Iommu,7,auth->Count);
}

// ====== x86_paging_inventory_verify ======

int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,const X86_PAGING_INVENTORY *snapshot)
{
    X86_PAGING_INVENTORY draft;
    if (failure==6 || !x86_paging_inventory_capture(boot,allocator,&draft)) return 0;
    return !memcmp(&draft,snapshot,sizeof(draft));
}

// ====== vtd_fixture ======

int x86_paging_inventory_capture_lapic(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,X86_PAGING_INVENTORY *snapshot)
{
    (void)lapic;
    return x86_paging_inventory_capture(boot,allocator,snapshot);
}

// ====== x86_paging_inventory_verify_lapic ======

int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,const X86_PAGING_INVENTORY *snapshot)
{
    (void)lapic;
    return x86_paging_inventory_verify(boot,allocator,snapshot);
}

// ====== vtd_fixture ======

void vtd_fixture(unsigned units)
{
    CHECK(!x86_iommu_owner()); failure=0; borrowed=0;
    memset(&context,0,sizeof(context)); memset(&pool,0,sizeof(pool)); memset(&window,0,sizeof(window));
    memset(&inventory,0,sizeof(inventory)); memset(&mapping,0,sizeof(mapping)); memset(&description,0,sizeof(description));
    memset(roots,0,sizeof(roots)); memset(memory_map,0,sizeof(memory_map)); memset(registers,0,sizeof(registers));
    memory_map[0]=7; memory_map[32]=8;
    for (BOOT_U32 byte=0; byte<8; ++byte) {
        memory_map[8+byte]=(unsigned char)((BOOT_U64)arena>>(byte*8)); memory_map[24+byte]=(unsigned char)(128ULL>>(byte*8));
    }
    CHECK(kernel_physical_initialize(&pool,memory_map,48,48,52));
    context.MemoryMap=memory_map; context.Info.MemoryMapBytes=48; context.Info.MemoryDescriptorBytes=48; context.Info.MemoryMapCapacity=48;
    context.Info.PageTableRoot=0x400000; window.Pool=&pool; window.Root=0x400000; description.Units=units;
    for (unsigned index=0; index<units; ++index) {
        description.Unit[index].Base=0xfed90000+index*65536; description.Unit[index].Bytes=4096;
        registers[index][0]=0x10; registers[index][1]=6|(1ULL<<10)|(47ULL<<16)|(0x22ULL<<24);
        registers[index][2]=1|(0xfULL<<8);
    }
}

// ====== mocked CPU/MMU/MMIO ======

int x86_fb_cpu(X86_FB_CPU *cpu) { *cpu=(X86_FB_CPU){failure==5?0x401000:0x400000,6,52,0}; return failure!=4; }
volatile BOOT_U64 *x86_fb_root(void) { return roots; }
int x86_fb_flush(BOOT_U64 root) { CHECK(root==0x400000); return failure!=1; }
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *loan) { return loan==&window && !borrowed; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address) { CHECK(address==0x400000); return roots; }
volatile unsigned char *x86_window_bytes(void) { return (volatile unsigned char *)borrowed; }
BOOT_U32 x86_apic_uc_slot(BOOT_U64 pat) { CHECK(pat==6); return 2; }
BOOT_U64 x86_apic_cache_bits(BOOT_U32 slot) { CHECK(slot==1); return 8; }
int x86_apic_borrow(X86_PAGE_WINDOW *loan,BOOT_U64 physical,BOOT_U64 cache,int writable,int (*operation)(void *),void *opaque)
{
    CHECK(loan==&window && !cache && (writable==0 || writable==1) && !borrowed && operation && opaque);
    if (failure==2) return 0;
    borrowed=(BOOT_U64 *)physical; int result=operation(opaque); borrowed=0; return result;
}
volatile unsigned char *x86_vtd_register_view(BOOT_U64 address,BOOT_U32 width)
{
    BOOT_U64 relative=address-X86_IOMMU_VIRTUAL,unit=relative/8192,offset=relative%8192;
    CHECK(unit<description.Units && (width==4 || width==8) && offset<32);
    return (volatile unsigned char *)registers[unit]+offset;
}
