#ifndef X86_SCHEDULER_REMOTE_H
#define X86_SCHEDULER_REMOTE_H
#include "scope.h"
#include "../integration/control.h"
#define X86_REMOTE_IDLE 0U
#define X86_REMOTE_REQUESTED 1U
#define X86_REMOTE_STARTED 3U
#define X86_REMOTE_DONE 4U
#define X86_REMOTE_FAILED 5U

typedef struct {
    uint64_t       Self, Root, Generation;
    uint64_t       Argument, Result[4];
    uint64_t       Started, Done, EventSource, EventBinding, State, Quiesced;
    uint32_t       Cpu, Index, Failure, Event, Jobs, Reserved;
    X86_TASK_ENTRY Entry;
} X86_REMOTE;

// ====== remote_task_service ======

int x86_remote_open(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, uint32_t event,
    uint64_t source, uint64_t binding);
int x86_remote_request(uint32_t cpu, X86_TASK_ENTRY entry, uint64_t argument);
void x86_remote_service(void);
uint32_t x86_remote_state(void);
int x86_remote_take(uint64_t result[4]);
X86_REMOTE *x86_remote_job(void);
int x86_remote_close(void);
uint32_t x86_remote_jobs(void);
KERNEL_PHYSICAL_ALLOCATOR *x86_scheduler_batch_resources(uint64_t *root);
int x86_remote_dispatch_collect(const KERNEL_SCHEDULER_REPLY *task);
int x86_remote_dispatch_release(void);
#endif
