#ifndef X86_SURFACE_TEST_MOCK_H
#define X86_SURFACE_TEST_MOCK_H
#include "arch/x86/kernel/drivers/video/virtio/resources/mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_VGPU_MAPPING gpu;
extern X86_PAGING_INVENTORY inventory;
extern X86_SURFACE_MAPPING mapping;
extern BOOT_U64 roots[512];
extern unsigned checks, failure;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL surface:%u\n",(unsigned)__LINE__);exit(1);} } while (0)
void surface_fixture(void);
#endif
