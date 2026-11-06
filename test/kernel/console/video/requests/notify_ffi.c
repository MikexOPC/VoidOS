#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"

#define CHECK(expression) do { if (!(expression)) { printf("FAIL: line %d\n",__LINE__); return 1; } } while (0)

// ====== kernel_console_requests_fail_stop ======

__declspec(noreturn) void kernel_console_requests_fail_stop(void) { abort(); }

// ====== main ======

int main(int argc, char **argv) {
    uint32_t mode = argc == 2 ? (uint32_t)(argv[1][0] - '0') : 0;
    CHECK(mode <= 2);
    CHECK(!kernel_console_ap_notify(0,1,123,0,0));
    CHECK(!kernel_console_ap_notify(0,0,0,0,0));
    CHECK(kernel_console_ap_notify(0,0,123,0,0) == 1);
    CHECK(!kernel_console_ap_notify(0,0,123,0,0));
    CHECK(!kernel_console_ap_notify(1,0,123,0,0));
    CHECK(!kernel_console_ap_notify(1,1,124,0,0));
    CHECK(!kernel_console_ap_notify(1,1,123,2,0));
    CHECK(kernel_console_ap_notify(7,0,123,1,0) == 0);
    uint64_t first = kernel_console_ap_notify(1,1,123,0,0);
    uint64_t second = kernel_console_ap_notify(1,1,123,1,0);
    CHECK(first == 3 && second == 4);
    CHECK(!kernel_console_ap_notify(2,1,123,0,second));
    CHECK(kernel_console_ap_notify(3,0,123,0,0) == 2);
    CHECK(!kernel_console_ap_notify(4,0,123,0,3));
    if (mode != 1) CHECK(kernel_console_ap_notify(4,0,123,0,2) == 1);
    CHECK(kernel_console_ap_notify(2,1,123,0,first) == 1);
    CHECK(!kernel_console_ap_notify(2,1,123,0,first));
    if (mode != 2) CHECK(kernel_console_ap_notify(2,1,123,1,second) == 1);
    CHECK(kernel_console_ap_notify(5,0,123,0,0) == 1);
    CHECK(!kernel_console_ap_notify(1,1,123,0,0));
    CHECK(!kernel_console_ap_notify(6,0,123,0,0));
    CHECK(!kernel_console_ap_notify(6,0,123,0,1));
    CHECK(!kernel_console_ap_notify(6,0,123,0,2));
    CHECK(!kernel_console_ap_notify(6,0,123,0,4));
    CHECK(kernel_console_ap_notify(6,0,123,0,3) == (uint64_t)(mode == 0));
    CHECK(kernel_console_ap_notify(7,0,123,0,0) == (mode == 0 ? 3ULL : mode == 1 ? 2ULL : 10ULL));
    CHECK(kernel_console_ap_notify(7,0,123,5,0) == (mode == 2 ? second : 0));
    CHECK(kernel_console_ap_notify(3,1,123,0,0) == UINT64_MAX);
    CHECK(!kernel_console_ap_notify(99,0,123,0,0));
    printf("PASS: actual Rust notification FFI mode %u; lifecycle and rejection checks\n",mode);
    return 0;
}
