#include "arch/x86/kernel/cpu/percpu/startup/entry.h"
#include "arch/x86/kernel/cpu/percpu/startup/startup.h"
#include "arch/x86/kernel/cpu/percpu/resources/geometry.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

// ====== main ======

int main(void) {
    XS_ENTRY_RECORD record, saved;
    XS_ENTRY_INPUT input = {1, 7, 48, 0, 520, 4096, 8192, 0x100000, 0x200000,
        0xffffffff80010000ULL, 0xffffffff80100000ULL, 0x0007040600070406ULL};
    memset(&record, 0xa5, sizeof(record));
    saved = record;
    assert(!xs_entry_build(NULL, input));
    input.Reserved = 1;
    assert(!xs_entry_build(&record, input) && memcmp(&record, &saved, sizeof(record)) == 0);
    input.Reserved = 0;
    assert(xs_entry_build(&record, input));
    assert(record.StackTop == X86_AP_BASE + X86_AP_STRIDE + 17 * 4096);
    assert(record.Descriptors == X86_AP_BASE + X86_AP_STRIDE + 48 * 4096);
    assert(xs_entry_valid(record, 48) == 1);
    saved = record;
    input.Token = 528;
    assert(!xs_entry_build(&record, input) && memcmp(&record, &saved, sizeof(record)) == 0);
    for (uint32_t cpu = 1; cpu < 64; ++cpu) {
        input.Index = cpu; input.Token = 512 + cpu * 8;
        assert(xs_entry_build(&record, input));
        assert(record.Index == cpu && record.Token == input.Token);
        XS_ENTRY_RECORD good = record;
        XS_ENTRY_INPUT changed = input;
        uint64_t base = X86_AP_BASE + cpu * X86_AP_STRIDE;
        for (uint32_t page = 17; page < 35; ++page) {
            uint64_t target = base + page * 4096;
            assert(target >= good.StackTop && target < good.StackTop + 73728);
            changed.Entry = target;
            assert(!xs_entry_build(&record, changed) && !memcmp(&record, &good, sizeof(record)));
            changed = input; changed.Local = target;
            assert(!xs_entry_build(&record, changed) && !memcmp(&record, &good, sizeof(record)));
            changed = input;
        }
    }
    saved = record;
    input.Index = 64;
    assert(!xs_entry_build(&record, input) && memcmp(&record, &saved, sizeof(record)) == 0);
    assert(xs_configure(4) == 1);
    input.Index = 1; input.Token = xs_reserve(1);
    assert(xs_entry_build(&record, input));
    assert(!xs_entry_claim(record, 48, 7));
    assert(xs_advance(input.Token, XS_PREPARED) == 1);
    assert(xs_advance(input.Token, XS_COMMITTED) == 1);
    uint64_t state = xs_snapshot();
    assert(!xs_entry_claim(record, 48, 8) && xs_snapshot() == state);
    record.Reserved = 1;
    assert(!xs_entry_claim(record, 48, 7) && xs_snapshot() == state);
    record.Reserved = 0;
    assert(xs_entry_claim(record, 48, 7) == 1);
    assert(!xs_entry_claim(record, 48, 7));
    assert(xs_quarantine(record.Token) == 1);
    assert(!xs_entry_claim(record, 48, 7) && xs_reserve(2) == 0);
    puts("PASS: AP entry C/Rust record ABI, AP layout and unchanged output on rejection");
    return 0;
}
