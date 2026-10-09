#ifndef X86_SCHEDULER_PERSISTENT_H
#define X86_SCHEDULER_PERSISTENT_H
#include <stdint.h>

// ====== persistent_profile_contract ======

int x86_scheduler_run_persistent(void);
void x86_scheduler_persistent_task(uint32_t index);
int x86_scheduler_persistent_complete(void);
uint32_t x86_scheduler_persistent_cycles(void);
#endif
