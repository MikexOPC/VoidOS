#ifndef VTD_DOMAIN_TEST_MOCK_H
#define VTD_DOMAIN_TEST_MOCK_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/domains/domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_PAGING_INVENTORY inventory;
extern X86_VTD_DOMAIN domain;
extern X86_VTD_DOMAIN_REQUEST request;
extern BOOT_U64 arena[4608][512];
extern unsigned checks,borrow_fail,pin_fail,borrow_count,pin_count,inventory_fail;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL DOMAIN:%u\n",(unsigned)__LINE__);exit(1);} } while (0)
void domain_fixture(unsigned count,unsigned pages);
void domain_buffers_release(void);
void domain_walk_check(void);
int domain_real_pin(KERNEL_PHYSICAL_ALLOCATOR *owner,const KERNEL_PHYSICAL_LEASE *lease,BOOT_U32 pin);
#endif
