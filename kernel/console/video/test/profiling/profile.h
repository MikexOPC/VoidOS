#ifndef KERNEL_CONSOLE_TEST_PROFILE_H
#define KERNEL_CONSOLE_TEST_PROFILE_H
#include <stdint.h>
#define KERNEL_CONSOLE_PROFILE_PHASES 6
typedef struct {
    uintptr_t  Self;
    uint64_t   Cells;
    uint64_t   Total;
    uint64_t   Sum[KERNEL_CONSOLE_PROFILE_PHASES];
    uint32_t   Max[KERNEL_CONSOLE_PROFILE_PHASES];
    uint32_t   Stamp[KERNEL_CONSOLE_PROFILE_PHASES + 1];
    uint32_t   Mask;
    uint32_t   Next;
    uint32_t   Failed;
} KERNEL_CONSOLE_PROFILE;
int kernel_console_profile_init(KERNEL_CONSOLE_PROFILE *owner, uint32_t mask);
int kernel_console_profile_mark(KERNEL_CONSOLE_PROFILE *owner, uint32_t phase, uint32_t stamp);
int kernel_console_draw_mark(uint32_t phase);
#endif
