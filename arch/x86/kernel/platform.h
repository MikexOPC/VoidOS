#ifndef OS_X86_KERNEL_PLATFORM_H
#define OS_X86_KERNEL_PLATFORM_H
#include "kernel/boot/contract.h"
#include "arch/x86/configs/boot_profile.h"
unsigned char x86_read_port(unsigned short port);
void x86_write_port(unsigned short port, unsigned char value);
BOOT_U64 x86_kernel_root(void);
BOOT_U32 x86_physical_bits(void);
__declspec(noreturn) void x86_kernel_stop(BOOT_U64 marker);
__declspec(noreturn) void x86_kernel_idle(void);
int x86_serial_initialize(void);
int x86_serial_character(char value);
int x86_serial_present(void);
void x86_serial_flush_gop_early(void);
#endif
