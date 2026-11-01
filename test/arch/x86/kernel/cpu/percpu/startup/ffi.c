#include "arch/x86/kernel/cpu/percpu/startup/startup.h"
#include <assert.h>
#include <stdio.h>

// ====== main ======

int main(void) {
    assert(xs_configure(4) == 1);
    assert(xs_configure(4) == 0);
    for (uint32_t cpu = 1; cpu < 3; ++cpu) {
        uint64_t token = xs_reserve(cpu);
        assert(token != 0 && (token & 7) == 0);
        assert(xs_snapshot() == (token | XS_PREPARED));
        for (uint64_t phase = XS_PREPARED; phase < XS_PARKED; ++phase) {
            assert(xs_advance(token, phase) == 1);
        }
        assert(xs_snapshot() == (token | XS_PARKED));
    }
    uint64_t token = xs_reserve(3);
    assert(xs_cancel(token) == 1);
    uint64_t retry = xs_reserve(3);
    assert(retry != token && xs_advance(token, XS_PREPARED) == 0);
    assert(xs_advance(retry, XS_PREPARED) == 1);
    assert(xs_quarantine(retry) == 1);
    assert(xs_advance(retry, XS_COMMITTED) == 0);
    assert(xs_cancel(retry) == 0 && xs_reserve(3) == 0);
    assert(xs_snapshot() == (retry | XS_QUARANTINED));
    assert(xs_advance(retry, UINT64_MAX) == 0);
    puts("PASS: AP startup C/Rust ABI, sequence, retry and quarantine");
    return 0;
}
