#include "mock.h"
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_PAGING_INVENTORY inventory;
X86_VTD_DOMAIN domain;
X86_VTD_DOMAIN_REQUEST request;
__declspec(align(4096)) BOOT_U64 arena[4608][512];
unsigned checks,borrow_fail,pin_fail,borrow_count,pin_count,inventory_fail;
static unsigned char memory_map[48];
static volatile unsigned char *borrowed;

// ====== domain_fixture ======

void domain_fixture(unsigned count,unsigned pages)
{
    memset(&context,0,sizeof(context)); memset(&pool,0,sizeof(pool)); memset(&window,0,sizeof(window));
    memset(&inventory,0,sizeof(inventory)); memset(&domain,0,sizeof(domain)); memset(&request,0,sizeof(request));
    memset(memory_map,0,sizeof(memory_map));
    BOOT_U64 physical=(BOOT_U64)arena,capacity=4608,attributes=8;
    memory_map[0]=7; memcpy(memory_map+8,&physical,8); memcpy(memory_map+24,&capacity,8); memcpy(memory_map+32,&attributes,8);
    borrowed=0; borrow_fail=0; pin_fail=0; borrow_count=0; pin_count=0; inventory_fail=0;
    CHECK(kernel_physical_initialize(&pool,memory_map,48,48,52));
    context.MemoryMap=memory_map; context.Info.MemoryMapBytes=48; context.Info.MemoryMapCapacity=48; context.Info.MemoryDescriptorBytes=48;
    context.Info.PageTableRoot=0x400000; window.Root=0x400000; window.Pool=&pool;
    inventory.Ready=1; inventory.Count=1; inventory.Root=0x400000; inventory.Tables[0].Physical=0x400000;
    request.Bus=5; request.Device=31; request.Function=7; request.Count=count; request.PhysicalBits=52;
    for (unsigned index=0; index<count; ++index) {
        CHECK(kernel_physical_allocate(&pool,pages,&request.Buffers[index].Lease));
        CHECK(kernel_physical_pin(&pool,&request.Buffers[index].Lease,1));
        request.Buffers[index].Access=index&1?3:1;
    }
    pin_count=0;
}

// ====== domain_buffers_release ======

void domain_buffers_release(void)
{
    for (unsigned index=0; index<request.Count; ++index) {
        CHECK(kernel_physical_pin(&pool,&request.Buffers[index].Lease,0));
        CHECK(kernel_physical_release(&pool,&request.Buffers[index].Lease));
    }
    CHECK(pool.FreePages==4608);
}

// ====== kernel_physical_pin ======

int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner,const KERNEL_PHYSICAL_LEASE *lease,BOOT_U32 pin)
{
    if (pin && ++pin_count==pin_fail) return 0;
    return domain_real_pin(owner,lease,pin);
}

// ====== mocked CPU window and paging scanner ======

int x86_page_window_idle_valid(const X86_PAGE_WINDOW *loan) { return loan==&window && !borrowed; }
volatile unsigned char *x86_window_bytes(void) { return borrowed; }
int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,const X86_PAGING_INVENTORY *snapshot)
{ return boot==&context && allocator==&pool && snapshot==&inventory && !inventory_fail; }
int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,const X86_PAGING_INVENTORY *snapshot)
{ (void)lapic; return x86_paging_inventory_verify(boot,allocator,snapshot); }
int x86_apic_borrow(X86_PAGE_WINDOW *loan,BOOT_U64 physical,BOOT_U64 cache,int writable,int (*operation)(void *),void *opaque)
{
    CHECK(loan==&window && !cache && (writable==0 || writable==1) && !borrowed);
    CHECK(physical>=(BOOT_U64)arena && physical<=(BOOT_U64)arena+sizeof(arena)-4096 && !(physical&4095));
    if (++borrow_count==borrow_fail) return 0;
    borrowed=(volatile unsigned char *)physical;
    int result=operation(opaque); borrowed=0; return result;
}
