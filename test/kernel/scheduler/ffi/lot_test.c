#include "kernel/scheduler/lot.h"
#include "arch/x86/kernel/scheduler/dispatcher/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static SRWLOCK core_lock = SRWLOCK_INIT;
static __declspec(thread) uint32_t cpu;
static __declspec(thread) uint32_t authority = 1;
static HANDLE entered[2], completed[2], quiet[2], start;
static uint64_t token;
static KERNEL_SCHEDULER_REPLY tasks[4];
static volatile LONG checks, acquisitions, releases;
static uint32_t runs[4];
static __declspec(thread) uint32_t first;

// ====== require ======

static void require(int value)
{
    LONG count = InterlockedIncrement(&checks);
    if (!value) { fprintf(stderr,"LOT_HOST_FAIL: check %ld cpu %u\n",count,cpu); exit(1); }
}

// ====== backend_mocks ======

uint32_t kernel_scheduler_context_valid(void) { return cpu == 0 && authority == 1; }
uint32_t kernel_scheduler_context_valid_on(uint32_t at) { return at == cpu ? authority : 0; }
uint32_t kernel_scheduler_binding_context_valid(void) { return 0; }
uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return 0; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(2); }
uint32_t kernel_scheduler_lock_acquire(void)
{
    AcquireSRWLockExclusive(&core_lock);
    InterlockedIncrement(&acquisitions);
    return 1;
}
void kernel_scheduler_lock_release(void)
{
    InterlockedIncrement(&releases);
    ReleaseSRWLockExclusive(&core_lock);
}

// ====== lot ======

static KERNEL_LOT_REPLY lot(uint32_t operation)
{
    return kernel_scheduler_lot_on(cpu,operation,0x1000,token,operation == KERNEL_LOT_OPEN ? 6 : 0);
}

// ====== wait_event ======

static void wait_event(HANDLE event)
{
    require(WaitForSingleObject(event,10000) == WAIT_OBJECT_0);
}

// ====== run_private ======

static int run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    require(task->status == 0 && task->index < 4 && task->index / 2 + 1 == cpu &&
        task->generation == tasks[task->index].generation);
    if (!first) {
        first = 1;
        require(SetEvent(entered[cpu - 1]) != 0);
        wait_event(start);
        if (cpu == 2) wait_event(completed[0]);
    }
    command->reason = ++runs[task->index] <= 8 ? X86_TASK_PREEMPT : X86_TASK_EXIT;
    return 1;
}

// ====== retained_callbacks ======

static int retain_private(const KERNEL_SCHEDULER_REPLY *task)
{ return task->index < 4 && task->state == KERNEL_TASK_FINISHED && runs[task->index] == 9; }
static int reply_private(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{ (void)task; (void)reply; return 0; }
static int release_private(void) { return 1; }

// ====== worker ======

static DWORD WINAPI worker(LPVOID argument)
{
    cpu = (uint32_t)(uintptr_t)argument;
    X86_DISPATCH_CONFIG config = {2,18,0,0,run_private,retain_private,reply_private,release_private,NULL};
    X86_DISPATCH_STATS stats = {0};
    require(x86_scheduler_dispatch_lot_on(cpu,0x1000,token,&config,&stats));
    require(stats.completed == 2 && stats.steps == 18 && stats.failure == 0);
    require(lot(KERNEL_LOT_COMPLETE).status == KERNEL_SCHEDULER_BAD_STATE);
    require(SetEvent(completed[cpu - 1]) != 0);
    wait_event(quiet[cpu - 1]);
    require(lot(KERNEL_LOT_QUIESCE).status == 0);
    return 0;
}

// ====== rejected_metadata ======

static void rejected_metadata(KERNEL_LOT_REPLY reply, uint32_t status)
{
    require(reply.status == status && !reply.failed && !reply.group && !reply.token &&
        !reply.participants && !reply.entered && !reply.completed && !reply.quiesced);
}

// ====== round_private ======

static void round_private(void)
{
    HANDLE workers[2];
    require(ResetEvent(start) != 0);
    for (uint32_t at = 0; at < 2; ++at) {
        require(ResetEvent(entered[at]) && ResetEvent(completed[at]) && ResetEvent(quiet[at]));
    }
    for (uint32_t i = 0; i < 4; ++i) {
        runs[i] = 0;
        tasks[i] = kernel_scheduler_apply(KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
        require(tasks[i].status == 0 && tasks[i].index == i);
        require(kernel_scheduler_affinity_on(0,i,tasks[i].generation,1ULL << (i / 2 + 1)) == 0);
        require(kernel_scheduler_apply(KERNEL_SCHEDULER_PUBLISH,i,tasks[i].generation).status == 0);
    }
    require(lot(KERNEL_LOT_OPEN).status == 0);
    for (uint32_t at = 0; at < 2; ++at) {
        workers[at] = CreateThread(NULL,0,worker,(LPVOID)(uintptr_t)(at + 1),0,NULL);
        require(workers[at] != NULL);
    }
    wait_event(entered[0]); wait_event(entered[1]);
    require(lot(KERNEL_LOT_QUERY).entered == 6);
    rejected_metadata(kernel_scheduler_lot_on(1,KERNEL_LOT_QUERY,0x1000,token,0),KERNEL_SCHEDULER_DENIED);
    rejected_metadata(kernel_scheduler_lot_on(0,KERNEL_LOT_QUERY,0x1000,token + 1,0),KERNEL_SCHEDULER_BAD_TOKEN);
    require(SetEvent(start) != 0);
    wait_event(completed[0]); wait_event(completed[1]);
    require(lot(KERNEL_LOT_QUERY).completed == 6 && lot(KERNEL_LOT_QUERY).quiesced == 0);
    require(kernel_scheduler_apply(KERNEL_SCHEDULER_COLLECT,0,tasks[0].generation).status == KERNEL_SCHEDULER_BAD_STATE);
    require(SetEvent(quiet[0]) != 0);
    wait_event(workers[0]);
    require(lot(KERNEL_LOT_QUERY).quiesced == 2);
    require(kernel_scheduler_apply(KERNEL_SCHEDULER_COLLECT,0,tasks[0].generation).status == KERNEL_SCHEDULER_BAD_STATE);
    require(lot(KERNEL_LOT_CLOSE).status == KERNEL_SCHEDULER_BAD_STATE);
    require(SetEvent(quiet[1]) != 0);
    wait_event(workers[1]);
    require(lot(KERNEL_LOT_QUERY).quiesced == 6);
    for (uint32_t i = 0; i < 4; ++i)
        require(kernel_scheduler_apply(KERNEL_SCHEDULER_COLLECT,i,tasks[i].generation).status == 0);
    require(lot(KERNEL_LOT_CLOSE).status == 0);
    require(lot(KERNEL_LOT_OPEN).status == KERNEL_SCHEDULER_BAD_STATE);
    for (uint32_t at = 0; at < 2; ++at) require(CloseHandle(workers[at]) != 0);
}

// ====== fail_run_private ======

static int fail_run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{ (void)task; (void)command; return 0; }

// ====== failure_private ======

static void failure_private(void)
{
    for (uint32_t i = 0; i < 4; ++i) {
        tasks[i] = kernel_scheduler_apply(KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
        require(tasks[i].status == 0 && tasks[i].index == i);
        require(kernel_scheduler_affinity_on(0,i,tasks[i].generation,1ULL << (i / 2 + 1)) == 0);
        require(kernel_scheduler_apply(KERNEL_SCHEDULER_PUBLISH,i,tasks[i].generation).status == 0);
    }
    require(lot(KERNEL_LOT_OPEN).status == 0);
    cpu = 1;
    X86_DISPATCH_CONFIG config = {2,18,0,0,fail_run_private,retain_private,reply_private,release_private,NULL};
    X86_DISPATCH_STATS stats = {0};
    require(!x86_scheduler_dispatch_lot_on(cpu,0x1000,token,&config,&stats));
    require(stats.failure == 4 && stats.completed == 0);
    require(lot(KERNEL_LOT_QUERY).failed == 1);
    cpu = 0;
    require(lot(KERNEL_LOT_QUERY).failed == 1 && lot(KERNEL_LOT_QUERY).quiesced == 0);
    require(lot(KERNEL_LOT_CLOSE).status == KERNEL_SCHEDULER_BAD_STATE);
    require(kernel_scheduler_apply(KERNEL_SCHEDULER_COLLECT,0,tasks[0].generation).status == KERNEL_SCHEDULER_BAD_STATE);
    require(kernel_scheduler_apply(KERNEL_SCHEDULER_QUERY,0,tasks[0].generation).state == KERNEL_TASK_RUNNING);
}

// ====== main ======

int main(void)
{
    token = 1;
    rejected_metadata(lot(KERNEL_LOT_QUERY),KERNEL_SCHEDULER_NOT_INITIALIZED);
    require(kernel_scheduler_apply(KERNEL_SCHEDULER_INIT,KERNEL_SCHEDULER_NONE,0).status == 0);
    require(kernel_scheduler_configure(4,1) == 0);
    start = CreateEventW(NULL,TRUE,FALSE,NULL);
    require(start != NULL);
    for (uint32_t at = 0; at < 2; ++at) {
        entered[at] = CreateEventW(NULL,TRUE,FALSE,NULL);
        completed[at] = CreateEventW(NULL,TRUE,FALSE,NULL);
        quiet[at] = CreateEventW(NULL,TRUE,FALSE,NULL);
        require(entered[at] && completed[at] && quiet[at]);
    }
    authority = 2;
    rejected_metadata(lot(KERNEL_LOT_QUERY),KERNEL_SCHEDULER_DENIED);
    authority = 1;
    rejected_metadata(kernel_scheduler_lot_on(0,7,1,1,0),KERNEL_SCHEDULER_BAD_ARGUMENT);
    rejected_metadata(kernel_scheduler_lot_on(0,KERNEL_LOT_QUERY,1,1,6),KERNEL_SCHEDULER_BAD_ARGUMENT);
    for (; token <= 100; ++token) round_private();
    failure_private();
    require(acquisitions == releases);
    for (uint32_t at = 0; at < 2; ++at) {
        require(CloseHandle(entered[at]) && CloseHandle(completed[at]) && CloseHandle(quiet[at]));
    }
    require(CloseHandle(start) != 0);
    printf("SCHEDULER_LOT_HOST_PASS: 100 lots, 400 tasks, 3200 preemptions, %ld checks\n",checks);
    return 0;
}
