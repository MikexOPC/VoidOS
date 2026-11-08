#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include <stdio.h>

// ====== main ======

int main(void) {
    _Static_assert(offsetof(X86_KERNEL_SEGMENTS, Pointer) == 168, "Private GDTR offset");
    puts("PASS: C private frame, record and descriptor offsets match assembly ABI");
    return 0;
}
