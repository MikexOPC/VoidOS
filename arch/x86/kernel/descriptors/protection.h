#ifndef OS_X86_KERNEL_PROTECTION_H
#define OS_X86_KERNEL_PROTECTION_H
#include "kernel/boot/contract.h"

int x86_user_protection_boot(void);
int x86_user_protection_ap(void);
int x86_user_mode_allowed(void);
#endif
