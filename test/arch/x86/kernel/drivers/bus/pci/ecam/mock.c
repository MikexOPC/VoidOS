#include "mock.h"
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_PAGING_INVENTORY inventory;
X86_ECAM owner;
unsigned checks,fail_mode,loads;
BOOT_U64 leaves[512],last_page;
static unsigned char memory_map[96];
static __declspec(align(4096)) unsigned char arena[128*4096];
static __declspec(align(4096)) BOOT_U32 config[1024];
static unsigned borrowed;

// ====== sample ======

X86_MCFG sample(void)
{
    X86_MCFG value={0}; value.Count=1; value.Bits=52;
    value.Ranges[0]=(X86_ECAM_RANGE){0xe0000000,0,0,255,0};
    return value;
}

// ====== fixture ======

void fixture(void)
{
    memset(&context,0,sizeof(context)); memset(&window,0,sizeof(window)); memset(&owner,0,sizeof(owner));
    memset(memory_map,0,sizeof(memory_map)); memset(&pool,0,sizeof(pool)); memset(&inventory,0,sizeof(inventory));
    memset(leaves,0,sizeof(leaves)); borrowed=fail_mode=loads=0; last_page=0;
    BOOT_U64 base=(BOOT_U64)arena,pages=128;
    memory_map[0]=7; memcpy(memory_map+8,&base,8); memcpy(memory_map+24,&pages,8); memory_map[32]=8;
    base=0xe0000000; pages=65536; memcpy(memory_map+56,&base,8); memcpy(memory_map+72,&pages,8);
    CHECK(kernel_physical_initialize(&pool,memory_map,sizeof(memory_map),48,52));
    context.MemoryMap=memory_map; context.Info.MemoryMapBytes=sizeof(memory_map); context.Info.MemoryMapCapacity=sizeof(memory_map);
    context.Info.MemoryDescriptorBytes=48; context.Info.PageTableRoot=0x400000; window.Root=0x400000; window.Pool=&pool;
    inventory.Root=0x400000; inventory.Count=1; inventory.Ready=1; inventory.Tables[0]=(X86_PAGING_RESOURCE){0x400000,0x400000,3,0};
}

// ====== mocked CPU and inventory ======

int x86_fb_cpu(X86_FB_CPU *out) { *out=(X86_FB_CPU){0x400000,6,52,0}; return fail_mode!=1; }
BOOT_U32 x86_apic_uc_slot(BOOT_U64 pat) { CHECK(pat==6); return 2; }
BOOT_U64 x86_apic_cache_bits(BOOT_U32 slot) { CHECK(slot==1); return 8; }
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *loan) { return loan==&window && !borrowed; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 view) { CHECK(view==0x400000); return leaves; }
int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,const X86_PAGING_INVENTORY *snap)
{ return boot==&context && allocator==&pool && snap==&inventory && fail_mode!=2; }
int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,const X86_PAGING_INVENTORY *snap)
{ (void)lapic; return x86_paging_inventory_verify(boot,allocator,snap); }

// ====== modeled borrow, real scalar MMIO backend ======

volatile unsigned char *x86_window_bytes(void) { ++loads; return borrowed && fail_mode!=6?(volatile unsigned char *)config:0; }

// ====== restore_borrow ======

void restore_borrow(void) { borrowed=0; fail_mode=0; }
int x86_apic_borrow(X86_PAGE_WINDOW *loan,BOOT_U64 physical,BOOT_U64 cache,int writable,int (*callback)(void *),void *state)
{
    CHECK(loan==&window && !borrowed && !writable && cache==8 && !(physical&4095) && callback && state);
    if (fail_mode==3) return 0;
    last_page=physical; config[0]=0x10501af4; config[2]=0x03000001; config[3]=0; config[6]=0x00020201;
    borrowed=1; int result=callback(state);
    if (fail_mode==4) return 0;
    borrowed=0;
    if (fail_mode==5) { ++pool.FreePages; return 0; }
    return result;
}
