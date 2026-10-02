#ifndef OS_X86_PAGE_WINDOW_ACCESS_H
#define OS_X86_PAGE_WINDOW_ACCESS_H
#include "arch/x86/kernel/platform.h"
volatile BOOT_U64 *x86_window_entries(BOOT_U32 level);
volatile unsigned char *x86_window_bytes(void);
void x86_invalidate_page(BOOT_U64 address);
#endif
