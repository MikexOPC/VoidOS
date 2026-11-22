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
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *window);
// Park: unmap an acquired span but keep its pinned lease, now owned by the caller, so the window
// can serve borrows; resume maps the same lease again at the same address without zeroing it.
int x86_page_window_park(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_LEASE *parked);
int x86_page_window_resume(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_LEASE *parked);
int x86_page_window_allocate_lease(X86_PAGE_WINDOW *window, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *lease_out);
int x86_page_window_write_lease(X86_PAGE_WINDOW *window, const KERNEL_PHYSICAL_LEASE *lease,
                                BOOT_U64 byte_offset, const void *src, BOOT_U64 bytes);
int x86_page_window_free_lease(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_LEASE *lease);
int x86_page_window_read_acpi(X86_PAGE_WINDOW *window, const KERNEL_BOOT_CONTEXT *context,
                              BOOT_U64 physical, BOOT_U64 bytes, unsigned char *output);
#endif
