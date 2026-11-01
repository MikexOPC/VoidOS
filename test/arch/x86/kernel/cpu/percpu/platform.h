#ifndef OS_TEST_PERCPU_PLATFORM_H
#define OS_TEST_PERCPU_PLATFORM_H
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include <setjmp.h>
extern uint32_t model_bsp, model_supported, model_image, model_descriptors;
extern uint32_t model_id;
extern uint32_t model_write_fault, model_self_fault, model_gs_reads, model_self_reads, model_writes;
extern uint64_t model_if, model_gs, model_target;
extern jmp_buf model_terminal;
extern int model_expect_fatal;
extern const char *model_reason;
#endif
