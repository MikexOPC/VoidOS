#ifndef X86_SCHEDULER_SWITCH_BRIDGE_H
#define X86_SCHEDULER_SWITCH_BRIDGE_H
#include "../control.h"

// ====== switch_private ======

#ifdef KERNEL_TEST_SCHEDULER_PRESERVATION
// Diagnostic profile: every switch goes through the register/XMM/x87 pattern probe.
extern int x86_scheduler_probe(X86_TASK_CONTEXT *, const X86_TASK_CONTEXT *, uint64_t);
#define switch_private(save, load, tag) x86_scheduler_probe((save), (load), (tag))
#else
// Production: the switch itself saves and restores RBX/RBP/RDI/RSI/R12-R15 and the whole FXSAVE
// image (context.asm); callers re-check task/generation/event after the return.
static int switch_private(X86_TASK_CONTEXT *save, const X86_TASK_CONTEXT *load, uint64_t tag)
{
    (void)tag;
    x86_scheduler_context_switch(save, load);
    return 1;
}
#endif

#endif
