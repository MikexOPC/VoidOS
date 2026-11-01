#include "retire.h"
#include "arch/x86/lib/processor/processor_access.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== x86_retire_cpu ======

int x86_retire_cpu(X86_RETIRE_CPU *cpu) {
    X86_PAGING_REGISTERS before, after;
    if (!cpu ||
        !x86_read_paging_registers(&before)
    )
        return 0;
    x86_retire_capture(cpu);
    if (!x86_read_paging_registers(&after) ||
        before.Cr0 != after.Cr0 ||
        before.Cr3 != after.Cr3 ||
        before.Cr4 != after.Cr4 ||
        before.Efer != after.Efer
    )
        return 0;
    cpu->Cr0 = before.Cr0; cpu->Root = before.Cr3; cpu->Cr4 = before.Cr4; cpu->Efer = before.Efer;
    return 1;
}

// ====== x86_retire_loaded ======

int x86_retire_loaded(void) {
    return x86_kernel_segments_retire_ready() && x86_exception_retire_ready();
}

// ====== x86_retire_table ======

volatile BOOT_U64 *x86_retire_table(BOOT_U64 view) {
    return (volatile BOOT_U64 *)x86_paging_view(view);
}
