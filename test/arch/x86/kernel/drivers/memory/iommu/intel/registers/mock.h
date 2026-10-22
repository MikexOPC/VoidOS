#ifndef VTD_TEST_MOCK_H
#define VTD_TEST_MOCK_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/registers/mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_PAGING_INVENTORY inventory;
extern X86_VTD_MAPPING mapping;
extern X86_DMAR_INFO description;
extern BOOT_U64 roots[512],arena[128][512],registers[8][512];
extern unsigned checks,failure;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL VT-d:%u\n",(unsigned)__LINE__);exit(1);} } while (0)
void vtd_fixture(unsigned units);
#endif
