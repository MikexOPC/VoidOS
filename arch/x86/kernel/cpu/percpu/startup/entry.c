#include "entry.h"
#include "../resources/geometry.h"

// ====== xs_entry_build ======

int xs_entry_build(XS_ENTRY_RECORD *output, XS_ENTRY_INPUT input) {
    if (!output || ((uintptr_t)output & 7) ||
        (uintptr_t)output > UINTPTR_MAX - (sizeof(*output) - 1) ||
        !input.Index || input.Index >= 64 || input.Reserved) return 0;
    uint64_t base = X86_AP_BASE + input.Index * X86_AP_STRIDE;
    XS_ENTRY_RECORD record = {
        XS_ENTRY_VERSION, sizeof(XS_ENTRY_RECORD), input.Index, input.ApicId,
        input.Token, input.Code, input.Data, input.TransitionRoot, input.KernelRoot,
        base + 17 * 4096, base + 48 * 4096, input.Local, input.Entry, input.Pat, 0
    };
    if (!xs_entry_valid(record, input.PhysicalBits)) return 0;
    *output = record;
    return 1;
}
