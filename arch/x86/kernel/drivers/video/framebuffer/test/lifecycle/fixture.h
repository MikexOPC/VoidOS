#ifndef OS_FB_TEST_FIXTURE_H
#define OS_FB_TEST_FIXTURE_H
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_LAPIC_MAPPING lapic;
extern X86_PAGING_INVENTORY inventory;
extern X86_FB_MAPPING owner;
extern X86_FB_CPU cpu;
extern BOOT_U32 borrows, fail_borrow, lose_view, flushes, fail_flush, bad_cpu;
BOOT_U64 *frame(BOOT_U64 physical);
int fixture(void);
void repair_view(void);
#endif
