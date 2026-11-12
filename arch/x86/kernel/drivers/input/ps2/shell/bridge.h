#ifndef X86_PS2_SHELL_BRIDGE_H
#define X86_PS2_SHELL_BRIDGE_H
#include "arch/x86/kernel/process/resident.h"

int x86_shell_bridge_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer);
void x86_shell_bridge_dispatch(void);
int x86_shell_bridge_close(void);
int kernel_shell_bridge_status(void);
#endif
