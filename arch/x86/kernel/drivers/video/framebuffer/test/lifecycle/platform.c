#include "fixture.h"
#include <string.h>
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_LAPIC_MAPPING lapic;
X86_PAGING_INVENTORY inventory;
X86_FB_MAPPING owner;
X86_FB_CPU cpu;
BOOT_U32 borrows, fail_borrow, lose_view, flushes, fail_flush, bad_cpu;
static BOOT_U64 frames[20][512];
static unsigned char map[96];
static BOOT_U64 *borrowed;

// ====== frame ======

BOOT_U64 *frame(BOOT_U64 physical)
{
    physical &= 0x000ffffffffff000ULL;
    if (physical >= 4096 && physical <= 16384) return frames[physical / 4096 - 1];
    if (physical >= 0x100000 && physical < 0x110000) return frames[4 + (physical - 0x100000) / 4096];
    return 0;
}

// ====== x86_paging_view ======

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    BOOT_U32 a = (BOOT_U32)((address >> 30) & 511);
    BOOT_U32 b = (BOOT_U32)((address >> 21) & 511);
    BOOT_U32 c = (BOOT_U32)((address >> 12) & 511);
    BOOT_U64 *page = frames[0];
    if (a == 128 && b == 128 && c == 128) return page;
    if (a == 128 && b == 128) return frame(page[c]);
    if (a == 128) {
        page = frame(page[b]);
        return page ? frame(page[c]) : 0;
    }
    page = frame(page[a]);
    if (!page) return 0;
    page = frame(page[b]);
    return page ? frame(page[c]) : 0;
}

// ====== x86_fb_cpu ======

int x86_fb_cpu(X86_FB_CPU *output)
{
    if (bad_cpu || !output) return 0;
    *output = cpu;
    return 1;
}

// ====== x86_fb_root ======

volatile BOOT_U64 *x86_fb_root(void)
{
    return frames[0];
}

// ====== x86_fb_flush ======

int x86_fb_flush(BOOT_U64 root)
{
    ++flushes;
    return root == cpu.Root && fail_flush != 1 && !(fail_flush == 2 && flushes == 1);
}

// ====== x86_exception_environment ======

int x86_exception_environment(void)
{
    return !bad_cpu;
}

// ====== x86_lapic_authority ======

int x86_lapic_authority(const X86_LAPIC_MAPPING *mapping)
{
    return mapping == &lapic && mapping->Self == (BOOT_U64)mapping && mapping->Ready == 1 &&
        (frames[3][288] & ~0x60ULL) == 0x80000000fee0001bULL;
}

// ====== x86_page_window_idle_valid ======

int x86_page_window_idle_valid(const X86_PAGE_WINDOW *view)
{
    return view == &window && view->Self == (BOOT_U64)view && view->Ready == 1 &&
        view->Pool == &pool && view->Root == 4096 && !view->Active && !borrowed;
}

// ====== x86_window_bytes ======

volatile unsigned char *x86_window_bytes(void)
{
    return (volatile unsigned char *)borrowed;
}

// ====== repair_view ======

void repair_view(void)
{
    borrowed = 0; window.Active = 0; lose_view = 0;
}

// ====== x86_apic_borrow ======

int x86_apic_borrow(X86_PAGE_WINDOW *view, BOOT_U64 physical, BOOT_U64 cache,
    int writable, int (*action)(void *), void *opaque)
{
    int result;
    if (!x86_page_window_idle_valid(view) || cache || writable != 1 || !action) return 0;
    ++borrows;
    if (borrows == fail_borrow) return 0;
    borrowed = frame(physical);
    if (!borrowed) return 0;
    view->Active = 2;
    result = action(opaque);
    if (lose_view) return 0;
    repair_view();
    return result;
}

// ====== number ======

static void number(unsigned char *target, BOOT_U64 value)
{
    BOOT_U32 byte;
    for (byte = 0; byte < 8; ++byte) target[byte] = (unsigned char)(value >> (byte * 8));
}

// ====== fixture ======

int fixture(void)
{
    memset(frames, 0, sizeof(frames)); memset(map, 0, sizeof(map));
    memset(&context, 0, sizeof(context)); memset(&pool, 0, sizeof(pool));
    memset(&window, 0, sizeof(window)); memset(&lapic, 0, sizeof(lapic));
    memset(&inventory, 0, sizeof(inventory)); memset(&owner, 0, sizeof(owner));
    borrows = fail_borrow = lose_view = flushes = fail_flush = bad_cpu = 0;
    repair_view();
    map[0] = 2; number(map + 8, 4096); number(map + 24, 4); map[32] = 8;
    map[48] = 7; number(map + 56, 0x100000); number(map + 72, 128); map[80] = 8;
    context.MemoryMap = map; context.Info.MemoryMapBytes = context.Info.MemoryMapCapacity = 96;
    context.Info.MemoryDescriptorBytes = 48; context.Info.PageTableRoot = 4096;
    context.Info.Framebuffer.Base = 0xc0000000; context.Info.Framebuffer.Capacity = 32768;
    context.Info.Framebuffer.Used = 12288; context.Info.Framebuffer.Width = 1024;
    context.Info.Framebuffer.Stride = 1024; context.Info.Framebuffer.Height = 3;
    frames[0][128] = 0x8000000000001003ULL; frames[0][224] = 0x2003;
    frames[1][0] = 0x3003; frames[2][0] = 0x4003;
    frames[3][288] = 0x80000000fee0001bULL; frames[3][1] = 0x8000000000200003ULL;
    cpu.Root = 4096; cpu.Pat = 0x0007040600070406ULL; cpu.PhysicalBits = 32; cpu.Reserved = 0;
    window.Self = (BOOT_U64)&window; window.Root = 4096; window.Pool = &pool; window.Ready = 1;
    lapic.Self = (BOOT_U64)&lapic; lapic.Root = 4096; lapic.Physical = 0xfee00000;
    lapic.Cache = 0x18; lapic.PhysicalBits = 32; lapic.Ready = 1;
    return kernel_physical_initialize(&pool, map, 96, 48, 32) &&
        x86_paging_inventory_capture_lapic(&context, &pool, &lapic, &inventory);
}
