#ifndef OS_X86_FRAMEBUFFER_MAPPING_H
#define OS_X86_FRAMEBUFFER_MAPPING_H
#include "arch/x86/mm/devices/lapic.h"
#include "kernel/drivers/video/framebuffer/mapping.h"

#define X86_FB_VIRTUAL ((225ULL << 39) + 4096)
#define X86_FB_SLOT 225U
#define X86_FB_RENDERING 4U
typedef struct {
    BOOT_U64  Root, Pat;
    BOOT_U32  PhysicalBits, Reserved;
} X86_FB_CPU;
typedef struct {
    BOOT_U64  Self, Budget;
    X86_PAGING_VIDEO_AUTH  Authority;
    const KERNEL_BOOT_CONTEXT  *Context;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGING_INVENTORY  *Inventory;
    X86_PAGE_WINDOW  *Window;
    BOOT_U32  Allocated, Pinned, Published, Ready;
} X86_FB_MAPPING;

int x86_fb_cpu(X86_FB_CPU *output);
int x86_fb_flush(BOOT_U64 root);
volatile BOOT_U64 *x86_fb_root(void);
int x86_fb_authority(const X86_PAGING_VIDEO_AUTH *owner);
int x86_fb_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory, X86_FB_MAPPING *owner);
int x86_fb_unbind(X86_FB_MAPPING *owner);
BOOT_U64 x86_fb_table_entry(BOOT_U32 table, BOOT_U32 slot, BOOT_U64 physical,
    BOOT_U64 pages, BOOT_U64 cache, BOOT_U32 bits, BOOT_U64 child);
#endif
