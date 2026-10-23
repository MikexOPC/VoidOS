#ifndef ECAM_TEST_MODEL_H
#define ECAM_TEST_MODEL_H
#include "arch/x86/kernel/drivers/bus/pci/ecam/ecam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_PAGING_INVENTORY inventory;
extern X86_ECAM owner;
extern unsigned checks,fail_mode,loads;
extern BOOT_U64 leaves[512],last_page;
extern unsigned writes;
extern BOOT_U32 command_word;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL ECAM:%u\n",(unsigned)__LINE__);exit(1);} } while (0)
void fixture(void);
X86_MCFG sample(void);
void restore_borrow(void);
#endif
