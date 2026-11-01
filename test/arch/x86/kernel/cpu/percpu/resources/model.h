#ifndef OS_TEST_AP_MODEL_H
#define OS_TEST_AP_MODEL_H
#include "arch/x86/kernel/cpu/percpu/resources/resources.h"
extern unsigned int calls, fail_at, live, pins, cpus_model, alias_model, bad_flush, bad_release;
extern BOOT_U64 root_model[512];
extern KERNEL_BOOT_CONTEXT context_model;
extern KERNEL_PHYSICAL_ALLOCATOR pool_model;
extern X86_PAGE_WINDOW window_model;
extern X86_PAGING_INVENTORY inventory_model;
void model_reset(void);
int capture_model(const KERNEL_BOOT_CONTEXT *, const KERNEL_PHYSICAL_ALLOCATOR *, const X86_LAPIC_MAPPING *, X86_PAGING_INVENTORY *);
#endif
