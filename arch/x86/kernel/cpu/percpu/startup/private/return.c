#include "return.h"
#include "../../resources/geometry.h"

// ====== xs_return_build ======

int xs_return_build(XS_RETURN_PLAN *output, XS_ENTRY_RECORD record, uint64_t entry, uint64_t idtr, uint32_t bits) {
    if (!output || ((uintptr_t)output & 7) || (uintptr_t)output > UINTPTR_MAX - (sizeof(*output) - 1)) return 0;
    XS_RETURN_PLAN plan = {1, sizeof(XS_RETURN_PLAN), record.Token, record.StackTop - X86_AP_FRAME_BYTES,
        entry, idtr, record.Local, record.KernelRoot, 0};
    if (!xs_return_valid(plan, record, bits)) return 0;
    *output = plan;
    return 1;
}
