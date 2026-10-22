#ifndef TEST_VGPU_MOCK_H
#define TEST_VGPU_MOCK_H
#include "arch/x86/kernel/drivers/video/virtio/mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_LAPIC_MAPPING lapic;
extern X86_PAGING_INVENTORY inventory;
extern X86_VGPU_MAPPING mapping;
extern BOOT_U64 root_entries[512];
extern BOOT_U32 failure, checks;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#value); exit(1); } } while (0)
void fixture(void);
void recover_borrow(void);
int capture(const KERNEL_BOOT_CONTEXT *,const KERNEL_PHYSICAL_ALLOCATOR *,const X86_LAPIC_MAPPING *,X86_PAGING_INVENTORY *);
#endif
