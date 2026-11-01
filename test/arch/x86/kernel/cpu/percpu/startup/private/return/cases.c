#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static XS_RETURN_PLAN plan;

// ====== main ======

int main(int argc, char **argv) {
    _Static_assert(offsetof(X86_CPU_LOCAL, EarlyContext) == 8 && offsetof(X86_CPU_LOCAL, Index) == 16 &&
        offsetof(X86_CPU_LOCAL, State) == 20 && X86_PERCPU_ACTIVE == 2, "Resume per-CPU offsets");
    assert(argc == 2);
    XS_ENTRY_INPUT input = {1, 0, 48, 0, 520, 0x8000, 0x9000, 0x10000, 0x20000,
        0xffffffff80010000ULL, 0xffffffff80100000ULL, 0x0007040600070406ULL};
    XS_ENTRY_RECORD record;
    assert(xs_entry_build(&record, input));
    for (uint64_t offset = 0; offset < 73728; offset += 4096) {
        memset(&plan, 0xa5, sizeof(plan));
        XS_RETURN_PLAN untouched = plan;
        assert(!xs_return_build(&plan, record, record.StackTop + offset, 0xffffffff80104000ULL, 48));
        assert(!memcmp(&plan, &untouched, sizeof(plan)));
        assert(!xs_return_build(&plan, record, 0xffffffff80102000ULL, record.StackTop + offset, 48));
        assert(!memcmp(&plan, &untouched, sizeof(plan)));
    }
    memset(&plan, 0xa5, sizeof(plan));
    XS_RETURN_PLAN saved = plan;
    assert(!xs_return_build(&plan, record, input.Entry, 0xffffffff80104000ULL, 48));
    assert(!memcmp(&plan, &saved, sizeof(plan)));
    assert(!xs_return_build(&plan, record, 0xffffffff80102000ULL, record.StackTop - 4096, 48));
    assert(!memcmp(&plan, &saved, sizeof(plan)));
    assert(!xs_return_build(NULL, record, 0xffffffff80102000ULL, 0xffffffff80104000ULL, 48));
    assert(!xs_return_build((XS_RETURN_PLAN *)(UINTPTR_MAX - 7), record, 0xffffffff80102000ULL, 0xffffffff80104000ULL, 48));
    assert(xs_return_build(&plan, record, 0xffffffff80102000ULL, 0xffffffff80104000ULL, 48));
    assert(xs_return_valid(plan, record, 48));
    for (unsigned int byte = 0; byte < sizeof(plan); ++byte) {
        XS_RETURN_PLAN changed = plan;
        ((unsigned char *)&changed)[byte] ^= 0x80;
        int valid = xs_return_valid(changed, record, 48) != 0;
        if (byte < 24 || byte >= 40) assert(!valid);
    }
    FILE *file = NULL;
    int opened = fopen_s(&file, argv[1], "wb");
    assert(!opened && file);
    size_t bytes = fwrite(&plan, 1, sizeof(plan), file);
    int closed = fclose(file);
    assert(bytes == sizeof(plan) && !closed);
    puts("PASS: private return C/Rust ABI, invalid-output preservation and token/root/frame bindings");
    return 0;
}
