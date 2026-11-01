#ifndef OS_TEST_TRANSITION_FAULTS_H
#define OS_TEST_TRANSITION_FAULTS_H
#include "kernel/mm/physical/allocator.h"
#include <setjmp.h>
extern unsigned int xt_fail_alloc, xt_fail_pin, xt_fail_release, xt_stop_armed;
extern unsigned int xt_release_calls;
extern unsigned int xt_is_bsp;
extern jmp_buf xt_terminal;
extern char xt_reason[80];
void xt_fault_reset(void);
#endif
