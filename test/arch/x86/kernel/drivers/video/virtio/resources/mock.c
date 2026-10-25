#include "mock.h"
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_VGPU_MAPPING gpu;
X86_PAGING_INVENTORY inventory;
X86_SURFACE_MAPPING mapping;
BOOT_U64 roots[512];
unsigned checks, failure;
__declspec(align(4096)) static BOOT_U64 arena[1200][512];
static unsigned char memory_map[48];
static BOOT_U64 *borrowed;

// ====== capture_private ======

static int capture_private(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,X86_PAGING_INVENTORY *out)
{
    (void)lapic;
    if (failure==3 && x86_surface_owner()) return 0;
    memset(out,0,sizeof(*out)); out->Root=boot->Info.PageTableRoot; out->Count=1; out->Ready=1;
    out->Tables[0]=(X86_PAGING_RESOURCE){0x400000,0x400000,0,0};
    if (!x86_surface_inputs(allocator,out->Root,(BOOT_U64)out,sizeof(*out),&out->Surface)) return 0;
    const X86_SURFACE_AUTH *auth=x86_surface_owner();
    if (!auth) return !roots[X86_SURFACE_SLOT];
    for (BOOT_U32 table=0; table<auth->Count; ++table) {
        const BOOT_U64 *page=(const BOOT_U64 *)auth->Tables[table].Base;
        BOOT_U32 level=table<2?table+1:3;
        BOOT_U64 view=(BOOT_U64)X86_SURFACE_SLOT<<(12+(level-1)*9);
        out->Tables[out->Count++]=(X86_PAGING_RESOURCE){auth->Tables[table].Base,view,level,20+table};
        CHECK(x86_surface_record(&out->Surface,auth->Tables[table].Base,view,level,20+table));
        for (BOOT_U32 slot=0; slot<512; ++slot) {
            BOOT_U64 child=0;
            if (!table && !slot) child=auth->Tables[1].Base;
            if (table==1 && slot<auth->Count-2) child=auth->Tables[slot+2].Base;
            if (page[slot]!=kernel_vgpu_surface_entry(table,slot,auth->Data.Base,auth->Data.Pages,child)) return 0;
        }
    }
    return x86_surface_complete(&out->Surface,(1U<<auth->Count)-1,auth->Data.Pages);
}

// ====== surface_fixture ======

void surface_fixture(void)
{
    CHECK(!x86_surface_owner()); failure=0; borrowed=0;
    memset(&context,0,sizeof(context)); memset(&pool,0,sizeof(pool)); memset(&window,0,sizeof(window));
    memset(&gpu,0,sizeof(gpu)); memset(&inventory,0,sizeof(inventory)); memset(&mapping,0,sizeof(mapping));
    memset(roots,0,sizeof(roots)); memset(memory_map,0,sizeof(memory_map)); memory_map[0]=7;
    for (BOOT_U32 byte=0; byte<8; ++byte) {
        memory_map[8+byte]=(unsigned char)((BOOT_U64)arena>>(byte*8));
        memory_map[24+byte]=(unsigned char)(1200ULL>>(byte*8));
    }
    memory_map[32]=8;
    CHECK(kernel_physical_initialize(&pool,memory_map,48,48,52));
    context.Info.PageTableRoot=0x400000; window.Pool=&pool; window.Root=0x400000;
    gpu.Self=(BOOT_U64)&gpu; gpu.Window=&window; gpu.Context=&context; gpu.Inventory=&inventory;
    gpu.Authority.Root=0x400000; gpu.Authority.Pat=6; gpu.Capture=capture_private;
}

// ====== mocked CPU and mappings ======

int x86_fb_cpu(X86_FB_CPU *cpu) { *cpu=(X86_FB_CPU){0x400000,6,52,0}; return failure!=4; }
volatile BOOT_U64 *x86_fb_root(void) { return roots; }
int x86_fb_flush(BOOT_U64 root) { CHECK(root==0x400000); return failure!=1; }
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *loan) { return loan==&window && !borrowed; }
int x86_vgpu_authority(const X86_PAGING_GPU_AUTH *auth) { return auth==&gpu.Authority; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address) { CHECK(address==0x400000); return roots; }
volatile unsigned char *x86_window_bytes(void) { return (volatile unsigned char *)borrowed; }
int x86_apic_borrow(X86_PAGE_WINDOW *loan,BOOT_U64 physical,BOOT_U64 cache,int writable,int (*operation)(void *),void *opaque)
{
    CHECK(loan==&window && !cache && writable==1 && !borrowed);
    if (failure==2) return 0;
    borrowed=(BOOT_U64 *)physical; int result=operation(opaque); borrowed=0; return result;
}
