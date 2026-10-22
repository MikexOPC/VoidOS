#include "mock.h"
#include "test/arch/x86/boot/uefi/pci/fixture.h"
#include "arch/x86/mm/window/access.h"
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_LAPIC_MAPPING lapic;
X86_PAGING_INVENTORY inventory;
X86_VGPU_MAPPING mapping;
BOOT_U64 root_entries[512];
BOOT_U32 failure, checks;
__declspec(align(4096)) static BOOT_U64 arena[128][512];
static unsigned char map[48];
static BOOT_U64 *borrowed;

// ====== fixture ======

void fixture(void)
{
    CHECK(!x86_paging_gpu_owner());
    failure=0; borrowed=0;
    memset(&context,0,sizeof(context)); memset(&pool,0,sizeof(pool));
    memset(&window,0,sizeof(window)); memset(&lapic,0,sizeof(lapic));
    memset(&inventory,0,sizeof(inventory)); memset(&mapping,0,sizeof(mapping));
    memset(root_entries,0,sizeof(root_entries)); memset(map,0,sizeof(map));
    map[0]=7;
    for (BOOT_U32 byte=0; byte<8; ++byte) {
        map[8+byte]=(unsigned char)((BOOT_U64)arena>>(8*byte));
        map[24+byte]=(unsigned char)(128ULL>>(8*byte));
    }
    map[32]=8;
    context.MemoryMap=map; context.Info.MemoryMapBytes=48; context.Info.MemoryDescriptorBytes=48;
    context.Info.MemoryMapCapacity=48; context.Info.PageTableRoot=0x400000;
    test_vgpu_fixture(&context.Info.Gpu);
    CHECK(kernel_physical_initialize(&pool,map,48,48,52));
    window.Pool=&pool; window.Root=0x400000;
    CHECK(capture(&context,&pool,&lapic,&inventory));
}

// ====== capture ======

int capture(const KERNEL_BOOT_CONTEXT *ctx,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *device,X86_PAGING_INVENTORY *out)
{
    const X86_PAGING_GPU_AUTH *gpu=x86_paging_gpu_owner();
    (void)device;
    if ((failure==5 && gpu) || failure==6 || !kernel_physical_validate(allocator) ||
        !x86_paging_gpu_inputs(ctx,allocator,out)) return 0;
    memset(out,0,sizeof(*out));
    out->Root=ctx->Info.PageTableRoot; out->Ready=1; out->Count=1;
    out->Tables[0]=(X86_PAGING_RESOURCE){out->Root,0x400000,0,0};
    if (!gpu) return root_entries[X86_GPU_SLOT]==0;
    out->Gpu=*gpu;
    for (BOOT_U32 level=0; level<3; ++level) {
        const BOOT_U64 *page=(const BOOT_U64 *)gpu->Tables[level].Base;
        out->Tables[out->Count++]=(X86_PAGING_RESOURCE){gpu->Tables[level].Base,
            (BOOT_U64)X86_GPU_SLOT<<(12+9*level),level+1,16+level};
        for (BOOT_U32 slot=0; slot<512; ++slot) {
            BOOT_U64 expected=0;
            if (level<2 && !slot) expected=gpu->Tables[level+1].Base|3;
            if (level==2 && slot && slot<=gpu->Plan.Count) {
                expected=gpu->Plan.Physical[slot-1]|gpu->Cache|0x8000000000000003ULL;
                CHECK(x86_paging_gpu_leaf(page[slot],X86_GPU_VIRTUAL+(slot-1)*4096)==4);
                ++out->MmioPages;
            }
            CHECK(page[slot]==expected);
        }
    }
    return x86_paging_gpu_complete(out);
}

// ====== CPU and temporary mapping mocks ======

int x86_fb_cpu(X86_FB_CPU *out)
{
    *out=(X86_FB_CPU){0x400000,0x0007040600070406ULL,52,0};
    return failure!=1;
}
volatile BOOT_U64 *x86_fb_root(void) { return root_entries; }
int x86_fb_flush(BOOT_U64 root) { return root==0x400000 && failure!=2; }
int x86_vgpu_pci_match(const KERNEL_VGPU_BOOT *boot, BOOT_U32 bits)
{
    return failure!=7 && kernel_vgpu_boot_valid(boot,bits);
}
BOOT_U32 x86_apic_uc_slot(BOOT_U64 pat) { return pat==0x0007040600070406ULL?4:0; }
BOOT_U64 x86_apic_cache_bits(BOOT_U32 slot) { return slot==3?0x18:~0ULL; }
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *value) { return value==&window && !borrowed; }
volatile unsigned char *x86_window_bytes(void) { return (volatile unsigned char *)borrowed; }
void recover_borrow(void) { borrowed=0; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    if (address==0x400000) return root_entries;
    for (BOOT_U32 level=0; level<3; ++level)
        if (address==((BOOT_U64)X86_GPU_SLOT<<(12+9*level))) return (const BOOT_U64 *)mapping.Authority.Tables[level].Base;
    return 0;
}
int x86_apic_borrow(X86_PAGE_WINDOW *value,BOOT_U64 physical,BOOT_U64 cache,
    int writable,int (*operation)(void *),void *opaque)
{
    int result;
    CHECK(value==&window && !borrowed && !cache && writable==1);
    if (failure==3) return 0;
    borrowed=(BOOT_U64 *)physical;
    if (failure==4) return 0;
    result=operation(opaque); borrowed=0;
    return result;
}
