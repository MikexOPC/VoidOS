#include <assert.h>
#include "kernel/console/video/pointer/state.h"

// ====== main ======

int main(void)
{
    int32_t fraction = 0;
    assert(kernel_pointer_scale_delta(1, &fraction) == 1);
    assert(kernel_pointer_scale_delta(1, &fraction) == 2);
    assert(fraction == 0);
    assert(kernel_pointer_scale_delta(-1, &fraction) == -1);
    assert(kernel_pointer_scale_delta(-1, &fraction) == -2);
    assert(fraction == 0);
    assert(kernel_pointer_scale_delta(1, &fraction) == 1);
    assert(kernel_pointer_scale_delta(0, &fraction) == 0);
    assert(kernel_pointer_scale_delta(-1, &fraction) == -1);
    assert(fraction == 0);
    for (int32_t delta = -256; delta <= 255; ++delta) {
        int32_t divided = 0;
        fraction = 0;
        for (uint32_t sample = 0; sample < 20; ++sample)
            divided += kernel_pointer_scale_delta(delta, &fraction);
        assert(divided == delta * 30 && fraction == 0);
    }
    return 0;
}
