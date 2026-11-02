#ifndef OS_TEST_PANIC_BARRIER_H
#define OS_TEST_PANIC_BARRIER_H
#include <stdint.h>
extern uint64_t xp_stress_arrived, xp_stress_failed;
void xp_stress_before_elect(uint32_t index);
__declspec(noreturn) void xp_stress_start(void);
#endif
