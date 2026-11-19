#ifndef OS_KERNEL_DIAGNOSTICS_H
#define OS_KERNEL_DIAGNOSTICS_H

#include "kernel/boot/contract.h"

int kernel_text(const char *text);
int kernel_section(const char *name);
int kernel_log(unsigned int level, const char *message);
int kernel_number(BOOT_U64 value);
int kernel_log_line_open(void);
int kernel_log_character(char value);
void kernel_log_panic_prepare(const char *message);
__declspec(noreturn) void kernel_log_stop(BOOT_U64 marker);
__declspec(noreturn) void kernel_panic(const char *message);

#endif
