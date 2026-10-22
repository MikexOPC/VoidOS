#ifndef VTD_QI_MODEL_H
#define VTD_QI_MODEL_H
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/registers/mock.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/invalidation/qi.h"
#include "arch/x86/mm/window/access.h"
extern X86_QI qi;
extern unsigned stall_clock,stall_enable,stall_disable,stall_completion,stale,errors,writes,completions,cleans;
extern unsigned pin_at;
extern unsigned latched_mode;
extern BOOT_U64 device[512];
void qi_fixture(void);
void qi_close(void);
void qi_enable_ack(void);
#endif
