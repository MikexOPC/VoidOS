#ifndef X86_CONSOLE_RUNTIME_H
#define X86_CONSOLE_RUNTIME_H
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
int x86_console_open(X86_PS2_SESSION *session);
int x86_console_edit(X86_PS2_SESSION *session);
int x86_console_mouse(X86_PS2_SESSION *session, int32_t dx, int32_t dy, uint32_t buttons);
int x86_console_submit(X86_PS2_SESSION *session);
int x86_console_flush(X86_PS2_SESSION *session);
int x86_console_pending(X86_PS2_SESSION *session);
// G5: idle audit of the paging inventory (at most once per second); 0 when it failed.
int x86_console_audit(X86_PS2_SESSION *session);
int x86_console_finish(X86_PS2_SESSION *session);
#endif
