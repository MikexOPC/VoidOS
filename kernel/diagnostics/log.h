#ifndef OS_KERNEL_DIAGNOSTICS_H
#define OS_KERNEL_DIAGNOSTICS_H

#include "kernel/boot/contract.h"

int kernel_text(const char *text);
int kernel_section(const char *name);
int kernel_log(unsigned int level, const char *message);
int kernel_number(BOOT_U64 value);
__declspec(noreturn) void kernel_panic(const char *message);

#endif
