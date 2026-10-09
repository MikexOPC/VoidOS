#include "kernel/scheduler/payload.h"
#include "../integration/control.h"

// ====== kernel_scheduler_payload_context_valid ======

uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{
    return consumer < 8 && generation && consumer == x86_scheduler_index() &&
        generation == x86_scheduler_generation() && x86_scheduler_task_valid();
}
