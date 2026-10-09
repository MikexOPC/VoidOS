#ifndef OS_KERNEL_TEST_BOOTSTRAP_PROBES_H
#define OS_KERNEL_TEST_BOOTSTRAP_PROBES_H
#include "kernel/bootstrap/memory.h"

// ====== explicit_diagnostic_profile ======

// Compiled and linked only with -TestBootstrapMemory (including memory faults).
void kernel_test_bootstrap_memory(KERNEL_BOOTSTRAP_MEMORY *memory);
#endif
