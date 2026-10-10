#ifndef KERNEL_CONSOLE_ACTIVE_WORKER_H
#define KERNEL_CONSOLE_ACTIVE_WORKER_H
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
int kernel_console_active_worker(X86_PS2_SESSION *session);
int kernel_console_active_finish(const X86_PS2_SESSION *session);
#endif
