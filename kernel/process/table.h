#ifndef OS_KERNEL_PROCESS_TABLE_H
#define OS_KERNEL_PROCESS_TABLE_H

#include <stdint.h>
#include "kernel/mm/heap/heap.h"
#include "kernel/sync/lock.h"

// ====== process_table_contract ======

// Lock order: process table (150) -> heap (200) -> physical (300) -> scheduler (900).
#define KERNEL_PROCESS_LOCK_RANK      150U
// Upper bounds aligned with the 256-slot scheduler core ceiling to prevent heap exhaustion.
#define KERNEL_PROCESS_MAX_CAPACITY   256U
#define KERNEL_PROCESS_MAX_HANDLES    256U
#define KERNEL_IPC_MAX_CHANNELS       256U
#define KERNEL_PROCESS_INIT_CAPACITY  16U
#define KERNEL_PROCESS_INIT_HANDLES   8U
#define KERNEL_IPC_INIT_CHANNELS      16U

#define KERNEL_PID_KERNEL             0ULL
#define KERNEL_TASK_NONE              UINT32_MAX

#define KERNEL_IPC_MESSAGE_BYTES      128U
#define KERNEL_IPC_INVALID            (~4ULL)
#define KERNEL_IPC_WOULD_BLOCK        (~5ULL)
#define KERNEL_IPC_PEER_CLOSED        (~6ULL)
#define KERNEL_IPC_BUFFER_SMALL       (~7ULL)
#define KERNEL_IPC_BAD_LENGTH         (~2ULL)

#define KERNEL_PROC_FREE              0U
#define KERNEL_PROC_CREATED           1U
#define KERNEL_PROC_RUNNABLE          2U
#define KERNEL_PROC_BLOCKED           3U
#define KERNEL_PROC_ZOMBIE            4U

#define KERNEL_PROC_EXIT_NONE         0U
#define KERNEL_PROC_EXIT_NORMAL       1U
#define KERNEL_PROC_EXIT_FAULT        2U
#define KERNEL_PROC_EXIT_BAD_FRAME    3U
#define KERNEL_PROC_EXIT_BUDGET       4U
#define KERNEL_PROC_EXIT_KILLED       5U

#define KERNEL_PROC_FLAG_RESIDENT     1U

typedef struct {
    uint64_t  Storage[142];
} KERNEL_IPC_CHANNEL;

_Static_assert(sizeof(KERNEL_IPC_CHANNEL) == 142U * 8U, "Rust Channel ABI size");

typedef struct {
    uint64_t  Handle;
    uint32_t  ChannelIndex;
    uint32_t  EndpointBit;
    uint32_t  Active;
    uint32_t  Reserved;
} KERNEL_IPC_HANDLE;

typedef struct {
    KERNEL_IPC_CHANNEL  Channel;
    uint64_t            EndpointBase;
    uint64_t            PublicHandle[2];
    uint64_t            OwnerPid[2];
    uint32_t            WaiterTask[2];
    uint32_t            InUse;
    uint32_t            AttachedMask;
    uint32_t            Closed;
    uint32_t            Reserved;
} KERNEL_IPC_SLOT;

typedef struct {
    uint64_t            Pid;
    uint32_t            State;
    uint32_t            ExitKind;
    uint32_t            TaskIndex;
    uint32_t            Flags;
    uint64_t            ExitCode;
    uint64_t            FaultVector;
    uint64_t            FaultError;
    uint64_t            FaultRip;
    uint64_t            FaultCr2;
    uint64_t            WrittenBytes;
    uint64_t            Writes;
    uint64_t            Irqs;
    uint64_t            ArchContext;
    KERNEL_IPC_HANDLE  *Handles;
    uint32_t            HandleCount;
    uint32_t            HandleCapacity;
} KERNEL_PROCESS;

typedef struct {
    uint64_t  Pid;
    uint32_t  State;
    uint32_t  ExitKind;
    uint32_t  TaskIndex;
    uint32_t  Flags;
    uint64_t  ExitCode;
    uint64_t  FaultVector;
    uint64_t  FaultError;
    uint64_t  FaultRip;
    uint64_t  FaultCr2;
    uint64_t  WrittenBytes;
    uint64_t  Writes;
    uint64_t  Irqs;
    uint64_t  ArchContext;
} KERNEL_PROCESS_INFO;

typedef struct {
    uint32_t  ProcessCount;
    uint32_t  ProcessCapacity;
    uint32_t  ActiveProcesses;
    uint32_t  ZombieProcesses;
    uint32_t  ChannelCount;
    uint32_t  ChannelCapacity;
} KERNEL_PROCESS_STATS;

typedef struct {
    uintptr_t        Self;
    KERNEL_HEAP     *Heap;
    KERNEL_PROCESS  *Processes;
    uint32_t         ProcessCapacity;
    uint32_t         ProcessCount;
    KERNEL_IPC_SLOT *Channels;
    uint32_t         ChannelCapacity;
    uint32_t         ChannelCount;
    uint64_t         NextPid;
    uint64_t         NextHandle;
    uint32_t         Ready;
    uint32_t         Reserved;
    KERNEL_LOCK      Gate;
} KERNEL_PROCESS_TABLE;

int kernel_process_table_init(KERNEL_PROCESS_TABLE *table, KERNEL_HEAP *heap,
    uint32_t initial_processes, uint32_t initial_channels);
int kernel_process_table_destroy(KERNEL_PROCESS_TABLE *table);
int kernel_process_table_stats(KERNEL_PROCESS_TABLE *table, KERNEL_PROCESS_STATS *out);

int kernel_process_create(KERNEL_PROCESS_TABLE *table, uint32_t flags,
    uint64_t arch_context, uint64_t *pid_out);
int kernel_process_bind_task(KERNEL_PROCESS_TABLE *table, uint64_t pid, uint32_t task_index);
int kernel_process_set_state(KERNEL_PROCESS_TABLE *table, uint64_t pid, uint32_t expected, uint32_t next);
int kernel_process_info(KERNEL_PROCESS_TABLE *table, uint64_t pid, KERNEL_PROCESS_INFO *out);
int kernel_process_record_write(KERNEL_PROCESS_TABLE *table, uint64_t pid, uint64_t bytes);
int kernel_process_record_irq(KERNEL_PROCESS_TABLE *table, uint64_t pid);

int kernel_process_terminate(KERNEL_PROCESS_TABLE *table, uint64_t pid,
    uint32_t exit_kind, uint64_t exit_code,
    uint64_t fault_vector, uint64_t fault_error, uint64_t fault_rip, uint64_t fault_cr2,
    uint32_t *wake_tasks, uint32_t wake_capacity, uint32_t *wake_count_out);
int kernel_process_reap(KERNEL_PROCESS_TABLE *table, uint64_t pid, KERNEL_PROCESS_INFO *out);

int kernel_ipc_channel_create(KERNEL_PROCESS_TABLE *table, uint64_t pid0, uint64_t pid1,
    uint64_t *handle0_out, uint64_t *handle1_out);
int kernel_ipc_close_handle(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    uint32_t *wake_task_out);
uint64_t kernel_ipc_send_check(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid,
    uint64_t handle, uint64_t len);
uint64_t kernel_ipc_send(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    const unsigned char *bytes, uint64_t len, uint32_t *wake_task_out);
uint64_t kernel_ipc_peek(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    unsigned char *bytes, uint64_t capacity);
uint64_t kernel_ipc_consume(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle);
uint64_t kernel_ipc_receive_ready(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle);
uint64_t kernel_ipc_wait_register(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid,
    uint64_t handle, uint32_t task_index);
int kernel_ipc_wait_cancel(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle);

#endif
