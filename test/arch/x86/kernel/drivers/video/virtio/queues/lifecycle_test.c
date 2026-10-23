#include "probe.h"
#include "kernel/diagnostics/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, mode, resets, releases, calls;
static X86_VGPU_MAPPING owner;
static X86_VGPU_TRANSPORT transport;
static X86_PAGE_WINDOW window;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_DMA_ARENA *bound;
__declspec(align(4096)) static unsigned char bytes[16384];
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL cleanup:%u\n",(unsigned)__LINE__);exit(1);} } while (0)

// ====== mocks ======

int kernel_text(const char *text) { CHECK(text!=0); return 1; }
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(BOOT_U32 level,const char *text) { (void)level; CHECK(text!=0); return 1; }
// V2a: the four queue pages come from the persistent DMA arena, modeled here.
int x86_dma_arena_bind(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *target,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,X86_DMA_CAPTURE capture,BOOT_U32 pages,X86_DMA_ARENA *arena)
{
    (void)context; (void)lapic; (void)inventory;
    CHECK(target==&window && pages==4 && !bound && capture==owner.Capture && arena && !arena->Self);
    if (mode==7) return 0;
    pool.FreePages-=4; bound=arena; arena->Self=(BOOT_U64)arena;
    arena->Authority.Data=(KERNEL_PHYSICAL_LEASE){0x100000,4,1,(BOOT_U64)&pool};
    return 1;
}
volatile unsigned char *x86_dma_arena_bytes(X86_DMA_ARENA *arena) { return arena==bound ? bytes : 0; }
int x86_dma_arena_unbind(X86_DMA_ARENA *arena)
{
    CHECK(arena==bound && resets==1);
    ++releases; pool.FreePages+=4; bound=0; memset(arena,0,sizeof(*arena));
    return 1;
}
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *target) { return target==&window; }
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *allocator,const KERNEL_PHYSICAL_LEASE *lease)
{
    return allocator==&pool && bound && lease==&bound->Authority.Data && lease->Pages==4;
}
int x86_vgpu_queue_open(X86_VGPU_TRANSPORT *target,X86_VGPU_QUEUE_TRANSPORT *native,VGPU_QUEUE_IO_CONTEXT *io)
{
    CHECK(target==&transport); (void)native; memset(io,0,sizeof(*io)); return mode!=6;
}
int kernel_vgpu_queue_start(VGPU_QUEUE *queue,const VGPU_QUEUE_IO_CONTEXT *io,const VGPU_BUFFERS *buffers)
{
    (void)io;
    CHECK(bound && buffers->Physical[0]==0x100000 && buffers->Physical[3]==0x103000 && buffers->Device[3]==0x103000);
    CHECK(buffers->Views[0]==bytes && buffers->Views[3]==bytes+12288);
    memset(queue,0,sizeof(*queue)); queue->State=1;
    return mode!=1;
}
int kernel_vgpu_queue_exchange(VGPU_QUEUE *queue)
{
    CHECK(bound && !resets && !releases);
    ++calls;
    if (mode==2 && calls==3) return 0;
    ++queue->Sequence; queue->Width=1024; queue->Height=768;
    return 1;
}
int x86_vgpu_queue_register(void *opaque,uint32_t region,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value)
{
    (void)opaque; CHECK(region==2 && !offset && width==1 && !write); *value=0; return 1;
}
int kernel_vgpu_reset(const KERNEL_VGPU_IO *io,KERNEL_VGPU_NEGOTIATION *result)
{
    (void)io; CHECK((bound || mode==7) && !releases); ++resets;
    if (mode==3) return 0;
    result->Reset=1; return 1;
}
int x86_vgpu_transport_end(const X86_VGPU_TRANSPORT *target) { CHECK(target==&transport && (releases==1 || mode==7) && !bound); return mode!=4; }
static int capture_private(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,X86_PAGING_INVENTORY *snapshot)
{
    (void)boot; (void)lapic; (void)snapshot; CHECK(allocator==&pool && bound); return mode!=5;
}

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<100; ++cycle) for (mode=0; mode<8; ++mode) {
        KERNEL_VGPU_IO io={0}; resets=releases=calls=0;
        // Host-only: an arena retained by a failed reset is forced idle before the next session.
        if (bound) memset(bound,0,sizeof(*bound));
        bound=0;
        memset(&window,0,sizeof(window)); memset(&pool,0,sizeof(pool)); memset(&owner,0,sizeof(owner));
        pool.FreePages=100; window.Pool=&pool; owner.Window=&window;
        owner.Capture=capture_private; owner.Authority.PhysicalBits=32;
        CHECK(x86_vgpu_queue_probe(&owner,&transport,&io)==(mode==0));
        if (mode==6) CHECK(!resets && !releases && pool.FreePages==100);
        else if (mode==7) CHECK(resets==1 && !releases && pool.FreePages==100 && !bound);
        else if (mode==3) CHECK(resets==1 && !releases && pool.FreePages==96 && bound);
        else CHECK(resets==1 && releases==1 && pool.FreePages==100 && !bound);
        if (!mode) CHECK(calls==256);
    }
    printf("PASS %u lifecycle checks, 800 mock sessions, reset before release and retention on reset failure\n",checks);
    return 0;
}
