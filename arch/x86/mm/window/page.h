#ifndef OS_X86_PAGE_WINDOW_H
#define OS_X86_PAGE_WINDOW_H
#include "kernel/mm/physical/allocator.h"
typedef struct {
    BOOT_U64 Self, Root;
    KERNEL_PHYSICAL_ALLOCATOR *Pool;
    KERNEL_PHYSICAL_LEASE Lease;
    BOOT_U32 Ready, Active;
} X86_PAGE_WINDOW;
int x86_page_window_initialize(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root);
int x86_page_window_acquire(X86_PAGE_WINDOW *window, BOOT_U64 pages);
int x86_page_window_release(X86_PAGE_WINDOW *window);
#endif
