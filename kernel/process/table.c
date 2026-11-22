#include "table.h"
#include "kernel/sync/internal.h"

extern uint64_t x86_ipc_open(KERNEL_IPC_CHANNEL *channel);
extern void x86_ipc_close(KERNEL_IPC_CHANNEL *channel);
extern void x86_ipc_finish(KERNEL_IPC_CHANNEL *channel);
extern uint64_t x86_ipc_send_check(const KERNEL_IPC_CHANNEL *channel, uint64_t owner, uint64_t handle, uint64_t len);
extern uint64_t x86_ipc_send(KERNEL_IPC_CHANNEL *channel, uint64_t owner, uint64_t handle,
    const unsigned char *bytes, uint64_t len);
extern uint64_t x86_ipc_peek(const KERNEL_IPC_CHANNEL *channel, uint64_t owner, uint64_t handle,
    unsigned char *bytes, uint64_t capacity);
extern uint64_t x86_ipc_consume(KERNEL_IPC_CHANNEL *channel, uint64_t owner, uint64_t handle);
extern uint64_t x86_ipc_receive_ready(const KERNEL_IPC_CHANNEL *channel, uint64_t owner, uint64_t handle);

typedef struct {
    KERNEL_LOCK_CONTEXT *Context;
    KERNEL_LOCK_GUARD    Guard;
    uint64_t             SavedIf;
} PROCESS_ACCESS;

// ====== zero_bytes ======

static void zero_bytes(void *pointer, size_t bytes)
{
    unsigned char *target = (unsigned char *)pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = 0;
}

// ====== copy_bytes ======

static void copy_bytes(void *target_pointer, const void *source_pointer, size_t bytes)
{
    unsigned char *target = (unsigned char *)target_pointer;
    const unsigned char *source = (const unsigned char *)source_pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = source[index];
}

// ====== enter_private ======

static int enter_private(KERNEL_PROCESS_TABLE *table, PROCESS_ACCESS *access)
{
    if (!table || !access || table->Self != (uintptr_t)table || !table->Ready || !table->Heap)
        return 0;
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!context) {
        ks_irq_restore(saved_if);
        return 0;
    }
    zero_bytes(access, sizeof(*access));
    if (!kernel_lock_acquire(&table->Gate, context, &access->Guard)) {
        ks_irq_restore(saved_if);
        return 0;
    }
    if (table->Self != (uintptr_t)table || !table->Ready) {
        (void)kernel_lock_release(context, &access->Guard);
        ks_irq_restore(saved_if);
        return 0;
    }
    access->Context = context;
    access->SavedIf = saved_if;
    return 1;
}

// ====== leave_private ======

static void leave_private(PROCESS_ACCESS *access)
{
    if (!access || !access->Context)
        return;
    (void)kernel_lock_release(access->Context, &access->Guard);
    ks_irq_restore(access->SavedIf);
    access->Context = NULL;
}

// ====== find_process_private ======

static KERNEL_PROCESS *find_process_private(KERNEL_PROCESS_TABLE *table, uint64_t pid)
{
    if (!pid || !table->Processes)
        return NULL;
    for (uint32_t index = 0; index < table->ProcessCapacity; ++index) {
        if (table->Processes[index].State != KERNEL_PROC_FREE && table->Processes[index].Pid == pid)
            return &table->Processes[index];
    }
    return NULL;
}

// ====== grow_processes_private ======

static int grow_processes_private(KERNEL_PROCESS_TABLE *table)
{
    if (table->ProcessCapacity >= KERNEL_PROCESS_MAX_CAPACITY)
        return 0;
    uint32_t next_capacity = table->ProcessCapacity ? table->ProcessCapacity * 2U : KERNEL_PROCESS_INIT_CAPACITY;
    if (next_capacity > KERNEL_PROCESS_MAX_CAPACITY)
        next_capacity = KERNEL_PROCESS_MAX_CAPACITY;
    KERNEL_PROCESS *grown = kernel_heap_alloc(table->Heap, (uint64_t)next_capacity * sizeof(*grown), 16);
    if (!grown)
        return 0;
    if (table->Processes) {
        copy_bytes(grown, table->Processes, (size_t)table->ProcessCapacity * sizeof(*grown));
        (void)kernel_heap_free(table->Heap, table->Processes);
    }
    table->Processes = grown;
    table->ProcessCapacity = next_capacity;
    return 1;
}

// ====== grow_channels_private ======

static int grow_channels_private(KERNEL_PROCESS_TABLE *table)
{
    if (table->ChannelCapacity >= KERNEL_IPC_MAX_CHANNELS)
        return 0;
    uint32_t next_capacity = table->ChannelCapacity ? table->ChannelCapacity * 2U : KERNEL_IPC_INIT_CHANNELS;
    if (next_capacity > KERNEL_IPC_MAX_CHANNELS)
        next_capacity = KERNEL_IPC_MAX_CHANNELS;
    KERNEL_IPC_SLOT *grown = kernel_heap_alloc(table->Heap, (uint64_t)next_capacity * sizeof(*grown), 16);
    if (!grown)
        return 0;
    if (table->Channels) {
        copy_bytes(grown, table->Channels, (size_t)table->ChannelCapacity * sizeof(*grown));
        (void)kernel_heap_free(table->Heap, table->Channels);
    }
    for (uint32_t index = table->ChannelCapacity; index < next_capacity; ++index) {
        grown[index].WaiterTask[0] = KERNEL_TASK_NONE;
        grown[index].WaiterTask[1] = KERNEL_TASK_NONE;
    }
    table->Channels = grown;
    table->ChannelCapacity = next_capacity;
    return 1;
}

// ====== grow_handles_private ======

static int grow_handles_private(KERNEL_PROCESS_TABLE *table, KERNEL_PROCESS *process)
{
    if (process->HandleCapacity >= KERNEL_PROCESS_MAX_HANDLES)
        return 0;
    uint32_t next_capacity = process->HandleCapacity ? process->HandleCapacity * 2U : KERNEL_PROCESS_INIT_HANDLES;
    if (next_capacity > KERNEL_PROCESS_MAX_HANDLES)
        next_capacity = KERNEL_PROCESS_MAX_HANDLES;
    KERNEL_IPC_HANDLE *grown = kernel_heap_alloc(table->Heap, (uint64_t)next_capacity * sizeof(*grown), 8);
    if (!grown)
        return 0;
    if (process->Handles) {
        copy_bytes(grown, process->Handles, (size_t)process->HandleCapacity * sizeof(*grown));
        (void)kernel_heap_free(table->Heap, process->Handles);
    }
    process->Handles = grown;
    process->HandleCapacity = next_capacity;
    return 1;
}

// ====== reserve_handle_slot_private ======

static int reserve_handle_slot_private(KERNEL_PROCESS_TABLE *table, KERNEL_PROCESS *process, uint32_t *index_out)
{
    for (uint32_t index = 0; index < process->HandleCapacity; ++index) {
        if (!process->Handles[index].Active) {
            *index_out = index;
            return 1;
        }
    }
    if (!grow_handles_private(table, process))
        return 0;
    for (uint32_t index = 0; index < process->HandleCapacity; ++index) {
        if (!process->Handles[index].Active) {
            *index_out = index;
            return 1;
        }
    }
    return 0;
}

// ====== resolve_handle_private ======

static int resolve_handle_private(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    KERNEL_IPC_SLOT **slot_out, uint32_t *side_out, KERNEL_IPC_HANDLE **entry_out)
{
    if (!handle)
        return 0;
    if (caller_pid == KERNEL_PID_KERNEL) {
        for (uint32_t index = 0; index < table->ChannelCapacity; ++index) {
            KERNEL_IPC_SLOT *slot = &table->Channels[index];
            if (!slot->InUse)
                continue;
            uint32_t side = (uint32_t)(handle & 1ULL);
            if (slot->PublicHandle[side] == handle && (slot->AttachedMask & (1U << side)) &&
                slot->OwnerPid[side] == KERNEL_PID_KERNEL) {
                *slot_out = slot;
                *side_out = side;
                if (entry_out)
                    *entry_out = NULL;
                return 1;
            }
        }
        return 0;
    }
    KERNEL_PROCESS *process = find_process_private(table, caller_pid);
    if (!process || process->State == KERNEL_PROC_ZOMBIE)
        return 0;
    for (uint32_t index = 0; index < process->HandleCapacity; ++index) {
        KERNEL_IPC_HANDLE *entry = &process->Handles[index];
        if (!entry->Active || entry->Handle != handle)
            continue;
        if (entry->ChannelIndex >= table->ChannelCapacity)
            return 0;
        KERNEL_IPC_SLOT *slot = &table->Channels[entry->ChannelIndex];
        uint32_t side = entry->EndpointBit & 1U;
        if (!slot->InUse || !(slot->AttachedMask & (1U << side)) ||
            slot->OwnerPid[side] != caller_pid || slot->PublicHandle[side] != handle)
            return 0;
        *slot_out = slot;
        *side_out = side;
        if (entry_out)
            *entry_out = entry;
        return 1;
    }
    return 0;
}

// ====== detach_endpoint_private ======

static uint32_t detach_endpoint_private(KERNEL_PROCESS_TABLE *table, KERNEL_IPC_SLOT *slot, uint32_t side)
{
    uint32_t peer = side ^ 1U;
    uint32_t wake_task = KERNEL_TASK_NONE;
    slot->AttachedMask &= ~(1U << side);
    slot->WaiterTask[side] = KERNEL_TASK_NONE;
    if (!slot->Closed) {
        x86_ipc_close(&slot->Channel);
        slot->Closed = 1;
        if (slot->WaiterTask[peer] != KERNEL_TASK_NONE) {
            wake_task = slot->WaiterTask[peer];
            slot->WaiterTask[peer] = KERNEL_TASK_NONE;
        }
    }
    if (!slot->AttachedMask) {
        x86_ipc_finish(&slot->Channel);
        slot->EndpointBase = 0;
        slot->PublicHandle[0] = 0;
        slot->PublicHandle[1] = 0;
        slot->OwnerPid[0] = 0;
        slot->OwnerPid[1] = 0;
        slot->WaiterTask[0] = KERNEL_TASK_NONE;
        slot->WaiterTask[1] = KERNEL_TASK_NONE;
        slot->InUse = 0;
        slot->Closed = 0;
        if (table->ChannelCount)
            --table->ChannelCount;
    }
    return wake_task;
}

// ====== fill_info_private ======

static void fill_info_private(const KERNEL_PROCESS *process, KERNEL_PROCESS_INFO *out)
{
    out->Pid = process->Pid;
    out->State = process->State;
    out->ExitKind = process->ExitKind;
    out->TaskIndex = process->TaskIndex;
    out->Flags = process->Flags;
    out->ExitCode = process->ExitCode;
    out->FaultVector = process->FaultVector;
    out->FaultError = process->FaultError;
    out->FaultRip = process->FaultRip;
    out->FaultCr2 = process->FaultCr2;
    out->WrittenBytes = process->WrittenBytes;
    out->Writes = process->Writes;
    out->Irqs = process->Irqs;
    out->ArchContext = process->ArchContext;
}

// ====== kernel_process_table_init ======

int kernel_process_table_init(KERNEL_PROCESS_TABLE *table, KERNEL_HEAP *heap,
    uint32_t initial_processes, uint32_t initial_channels)
{
    if (!table || !heap || table->Ready)
        return 0;
    if (!initial_processes)
        initial_processes = KERNEL_PROCESS_INIT_CAPACITY;
    if (!initial_channels)
        initial_channels = KERNEL_IPC_INIT_CHANNELS;
    if (initial_processes > KERNEL_PROCESS_MAX_CAPACITY || initial_channels > KERNEL_IPC_MAX_CHANNELS)
        return 0;
    zero_bytes(table, sizeof(*table));
    if (!kernel_lock_initialize(&table->Gate, KERNEL_PROCESS_LOCK_RANK))
        return 0;
    KERNEL_PROCESS *processes = kernel_heap_alloc(heap, (uint64_t)initial_processes * sizeof(*processes), 16);
    KERNEL_IPC_SLOT *channels = processes
        ? kernel_heap_alloc(heap, (uint64_t)initial_channels * sizeof(*channels), 16)
        : NULL;
    if (!channels) {
        if (processes)
            (void)kernel_heap_free(heap, processes);
        zero_bytes(table, sizeof(*table));
        return 0;
    }
    for (uint32_t index = 0; index < initial_channels; ++index) {
        channels[index].WaiterTask[0] = KERNEL_TASK_NONE;
        channels[index].WaiterTask[1] = KERNEL_TASK_NONE;
    }
    table->Self = (uintptr_t)table;
    table->Heap = heap;
    table->Processes = processes;
    table->ProcessCapacity = initial_processes;
    table->Channels = channels;
    table->ChannelCapacity = initial_channels;
    table->NextPid = 1;
    table->NextHandle = 2;
    table->Ready = 1;
    return 1;
}

// ====== kernel_process_table_destroy ======

int kernel_process_table_destroy(KERNEL_PROCESS_TABLE *table)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return 0;
    KERNEL_HEAP *heap = table->Heap;
    KERNEL_PROCESS *processes = table->Processes;
    uint32_t proc_capacity = table->ProcessCapacity;
    KERNEL_IPC_SLOT *channels = table->Channels;
    uint32_t chan_capacity = table->ChannelCapacity;
    for (uint32_t index = 0; index < proc_capacity; ++index) {
        if (processes[index].Handles)
            (void)kernel_heap_free(heap, processes[index].Handles);
    }
    for (uint32_t index = 0; index < chan_capacity; ++index) {
        if (channels[index].InUse)
            x86_ipc_finish(&channels[index].Channel);
    }
    if (channels)
        (void)kernel_heap_free(heap, channels);
    if (processes)
        (void)kernel_heap_free(heap, processes);
    table->Ready = 0;
    table->Processes = NULL;
    table->Channels = NULL;
    table->ProcessCapacity = 0;
    table->ProcessCount = 0;
    table->ChannelCapacity = 0;
    table->ChannelCount = 0;
    leave_private(&access);
    zero_bytes(table, sizeof(*table));
    return 1;
}

// ====== kernel_process_table_stats ======

int kernel_process_table_stats(KERNEL_PROCESS_TABLE *table, KERNEL_PROCESS_STATS *out)
{
    PROCESS_ACCESS access;
    if (!out || !enter_private(table, &access))
        return 0;
    zero_bytes(out, sizeof(*out));
    out->ProcessCount = table->ProcessCount;
    out->ProcessCapacity = table->ProcessCapacity;
    out->ChannelCount = table->ChannelCount;
    out->ChannelCapacity = table->ChannelCapacity;
    for (uint32_t index = 0; index < table->ProcessCapacity; ++index) {
        uint32_t state = table->Processes[index].State;
        if (state == KERNEL_PROC_ZOMBIE)
            ++out->ZombieProcesses;
        else if (state != KERNEL_PROC_FREE)
            ++out->ActiveProcesses;
    }
    leave_private(&access);
    return 1;
}

// ====== kernel_process_create ======

int kernel_process_create(KERNEL_PROCESS_TABLE *table, uint32_t flags,
    uint64_t arch_context, uint64_t *pid_out)
{
    PROCESS_ACCESS access;
    if (!pid_out || !enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *slot = NULL;
    for (uint32_t index = 0; index < table->ProcessCapacity; ++index) {
        if (table->Processes[index].State == KERNEL_PROC_FREE) {
            slot = &table->Processes[index];
            break;
        }
    }
    if (!slot && grow_processes_private(table)) {
        for (uint32_t index = 0; index < table->ProcessCapacity; ++index) {
            if (table->Processes[index].State == KERNEL_PROC_FREE) {
                slot = &table->Processes[index];
                break;
            }
        }
    }
    if (slot && table->NextPid && table->NextPid < UINT64_MAX) {
        zero_bytes(slot, sizeof(*slot));
        slot->Pid = table->NextPid++;
        slot->State = KERNEL_PROC_CREATED;
        slot->TaskIndex = KERNEL_TASK_NONE;
        slot->Flags = flags;
        slot->ArchContext = arch_context;
        ++table->ProcessCount;
        *pid_out = slot->Pid;
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_process_bind_task ======

int kernel_process_bind_task(KERNEL_PROCESS_TABLE *table, uint64_t pid, uint32_t task_index)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process && (process->State == KERNEL_PROC_CREATED || process->State == KERNEL_PROC_RUNNABLE)) {
        process->TaskIndex = task_index;
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_process_set_state ======

int kernel_process_set_state(KERNEL_PROCESS_TABLE *table, uint64_t pid, uint32_t expected, uint32_t next)
{
    PROCESS_ACCESS access;
    if (next == KERNEL_PROC_FREE || next == KERNEL_PROC_ZOMBIE || !enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process && process->State == expected) {
        int allowed = (expected == KERNEL_PROC_CREATED && next == KERNEL_PROC_RUNNABLE) ||
            (expected == KERNEL_PROC_RUNNABLE && next == KERNEL_PROC_BLOCKED) ||
            (expected == KERNEL_PROC_BLOCKED && next == KERNEL_PROC_RUNNABLE);
        if (allowed) {
            process->State = next;
            result = 1;
        }
    }
    leave_private(&access);
    return result;
}

// ====== kernel_process_info ======

int kernel_process_info(KERNEL_PROCESS_TABLE *table, uint64_t pid, KERNEL_PROCESS_INFO *out)
{
    PROCESS_ACCESS access;
    if (!out || !enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process) {
        fill_info_private(process, out);
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_process_record_write ======

int kernel_process_record_write(KERNEL_PROCESS_TABLE *table, uint64_t pid, uint64_t bytes)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process && process->State != KERNEL_PROC_ZOMBIE && bytes <= UINT64_MAX - process->WrittenBytes) {
        process->WrittenBytes += bytes;
        ++process->Writes;
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_process_record_irq ======

int kernel_process_record_irq(KERNEL_PROCESS_TABLE *table, uint64_t pid)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process && process->State != KERNEL_PROC_ZOMBIE) {
        ++process->Irqs;
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_process_terminate ======

int kernel_process_terminate(KERNEL_PROCESS_TABLE *table, uint64_t pid,
    uint32_t exit_kind, uint64_t exit_code,
    uint64_t fault_vector, uint64_t fault_error, uint64_t fault_rip, uint64_t fault_cr2,
    uint32_t *wake_tasks, uint32_t wake_capacity, uint32_t *wake_count_out)
{
    PROCESS_ACCESS access;
    if (exit_kind < KERNEL_PROC_EXIT_NORMAL || exit_kind > KERNEL_PROC_EXIT_KILLED ||
        (wake_capacity && !wake_tasks) || !enter_private(table, &access))
        return 0;
    int result = 0;
    uint32_t wakes = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process && process->State != KERNEL_PROC_ZOMBIE) {
        process->State = KERNEL_PROC_ZOMBIE;
        process->ExitKind = exit_kind;
        process->ExitCode = exit_code;
        process->FaultVector = fault_vector;
        process->FaultError = fault_error;
        process->FaultRip = fault_rip;
        process->FaultCr2 = fault_cr2;
        for (uint32_t index = 0; index < process->HandleCapacity; ++index) {
            KERNEL_IPC_HANDLE *entry = &process->Handles[index];
            if (!entry->Active)
                continue;
            if (entry->ChannelIndex < table->ChannelCapacity) {
                KERNEL_IPC_SLOT *slot = &table->Channels[entry->ChannelIndex];
                uint32_t side = entry->EndpointBit & 1U;
                if (slot->InUse && (slot->AttachedMask & (1U << side)) && slot->OwnerPid[side] == pid) {
                    uint32_t wake_task = detach_endpoint_private(table, slot, side);
                    if (wake_task != KERNEL_TASK_NONE && wakes < wake_capacity)
                        wake_tasks[wakes++] = wake_task;
                }
            }
            entry->Active = 0;
        }
        if (process->Handles) {
            (void)kernel_heap_free(table->Heap, process->Handles);
            process->Handles = NULL;
            process->HandleCapacity = 0;
            process->HandleCount = 0;
        }
        result = 1;
    }
    if (wake_count_out)
        *wake_count_out = wakes;
    leave_private(&access);
    return result;
}

// ====== kernel_process_reap ======

int kernel_process_reap(KERNEL_PROCESS_TABLE *table, uint64_t pid, KERNEL_PROCESS_INFO *out)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *process = find_process_private(table, pid);
    if (process && (process->State == KERNEL_PROC_ZOMBIE || process->State == KERNEL_PROC_CREATED)) {
        if (out)
            fill_info_private(process, out);
        if (process->Handles)
            (void)kernel_heap_free(table->Heap, process->Handles);
        zero_bytes(process, sizeof(*process));
        if (table->ProcessCount)
            --table->ProcessCount;
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_ipc_channel_create ======

int kernel_ipc_channel_create(KERNEL_PROCESS_TABLE *table, uint64_t pid0, uint64_t pid1,
    uint64_t *handle0_out, uint64_t *handle1_out)
{
    PROCESS_ACCESS access;
    if (!handle0_out || !handle1_out || !enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_PROCESS *proc0 = pid0 ? find_process_private(table, pid0) : NULL;
    KERNEL_PROCESS *proc1 = pid1 ? find_process_private(table, pid1) : NULL;
    if ((pid0 && (!proc0 || proc0->State == KERNEL_PROC_ZOMBIE)) ||
        (pid1 && (!proc1 || proc1->State == KERNEL_PROC_ZOMBIE))) {
        leave_private(&access);
        return 0;
    }
    uint32_t channel_index = UINT32_MAX;
    for (uint32_t index = 0; index < table->ChannelCapacity; ++index) {
        if (!table->Channels[index].InUse) {
            channel_index = index;
            break;
        }
    }
    if (channel_index == UINT32_MAX && grow_channels_private(table)) {
        for (uint32_t index = 0; index < table->ChannelCapacity; ++index) {
            if (!table->Channels[index].InUse) {
                channel_index = index;
                break;
            }
        }
    }
    if (channel_index != UINT32_MAX) {
        uint32_t idx0 = UINT32_MAX;
        uint32_t idx1 = UINT32_MAX;
        int reserved = 1;
        if (proc0) {
            if (!reserve_handle_slot_private(table, proc0, &idx0))
                reserved = 0;
            else
                proc0->Handles[idx0].Active = 1;
        }
        if (reserved && proc1) {
            if (!reserve_handle_slot_private(table, proc1, &idx1))
                reserved = 0;
            else
                proc1->Handles[idx1].Active = 1;
        }
        if (reserved && table->NextHandle <= UINT64_MAX - 2ULL) {
            KERNEL_IPC_SLOT *slot = &table->Channels[channel_index];
            uint64_t base = x86_ipc_open(&slot->Channel);
            if (base) {
                uint64_t public_base = table->NextHandle;
                table->NextHandle += 2ULL;
                slot->EndpointBase = base;
                slot->PublicHandle[0] = public_base;
                slot->PublicHandle[1] = public_base | 1ULL;
                slot->OwnerPid[0] = pid0;
                slot->OwnerPid[1] = pid1;
                slot->WaiterTask[0] = KERNEL_TASK_NONE;
                slot->WaiterTask[1] = KERNEL_TASK_NONE;
                slot->InUse = 1;
                slot->AttachedMask = 3U;
                slot->Closed = 0;
                ++table->ChannelCount;
                if (proc0) {
                    proc0->Handles[idx0].Handle = public_base;
                    proc0->Handles[idx0].ChannelIndex = channel_index;
                    proc0->Handles[idx0].EndpointBit = 0;
                    ++proc0->HandleCount;
                }
                if (proc1) {
                    proc1->Handles[idx1].Handle = public_base | 1ULL;
                    proc1->Handles[idx1].ChannelIndex = channel_index;
                    proc1->Handles[idx1].EndpointBit = 1;
                    ++proc1->HandleCount;
                }
                *handle0_out = public_base;
                *handle1_out = public_base | 1ULL;
                result = 1;
            } else {
                if (proc0)
                    proc0->Handles[idx0].Active = 0;
                if (proc1)
                    proc1->Handles[idx1].Active = 0;
            }
        } else {
            if (proc0 && idx0 != UINT32_MAX)
                proc0->Handles[idx0].Active = 0;
            if (proc1 && idx1 != UINT32_MAX)
                proc1->Handles[idx1].Active = 0;
        }
    }
    leave_private(&access);
    return result;
}

// ====== kernel_ipc_close_handle ======

int kernel_ipc_close_handle(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    uint32_t *wake_task_out)
{
    PROCESS_ACCESS access;
    if (wake_task_out)
        *wake_task_out = KERNEL_TASK_NONE;
    if (!enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_IPC_SLOT *slot = NULL;
    KERNEL_IPC_HANDLE *entry = NULL;
    uint32_t side = 0;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, &entry)) {
        if (entry) {
            KERNEL_PROCESS *process = find_process_private(table, caller_pid);
            entry->Active = 0;
            entry->Handle = 0;
            if (process && process->HandleCount)
                --process->HandleCount;
        }
        uint32_t wake_task = detach_endpoint_private(table, slot, side);
        if (wake_task_out)
            *wake_task_out = wake_task;
        result = 1;
    }
    leave_private(&access);
    return result;
}

// ====== kernel_ipc_send_check ======

uint64_t kernel_ipc_send_check(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid,
    uint64_t handle, uint64_t len)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return KERNEL_IPC_INVALID;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    uint64_t status = KERNEL_IPC_INVALID;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        uint64_t owner = slot->EndpointBase | side;
        status = x86_ipc_send_check(&slot->Channel, owner, owner, len);
    }
    leave_private(&access);
    return status;
}

// ====== kernel_ipc_send ======

uint64_t kernel_ipc_send(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    const unsigned char *bytes, uint64_t len, uint32_t *wake_task_out)
{
    PROCESS_ACCESS access;
    if (wake_task_out)
        *wake_task_out = KERNEL_TASK_NONE;
    if (!bytes || !enter_private(table, &access))
        return KERNEL_IPC_INVALID;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    uint64_t status = KERNEL_IPC_INVALID;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        uint64_t owner = slot->EndpointBase | side;
        unsigned char staging[KERNEL_IPC_MESSAGE_BYTES];
        zero_bytes(staging, sizeof(staging));
        if (len >= 1 && len <= KERNEL_IPC_MESSAGE_BYTES)
            copy_bytes(staging, bytes, (size_t)len);
        status = x86_ipc_send(&slot->Channel, owner, owner, staging, len);
        if (status == len) {
            uint32_t peer = side ^ 1U;
            if (slot->WaiterTask[peer] != KERNEL_TASK_NONE) {
                if (wake_task_out)
                    *wake_task_out = slot->WaiterTask[peer];
                slot->WaiterTask[peer] = KERNEL_TASK_NONE;
            }
        }
    }
    leave_private(&access);
    return status;
}

// ====== kernel_ipc_peek ======

uint64_t kernel_ipc_peek(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle,
    unsigned char *bytes, uint64_t capacity)
{
    PROCESS_ACCESS access;
    if (!bytes || !enter_private(table, &access))
        return KERNEL_IPC_INVALID;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    uint64_t status = KERNEL_IPC_INVALID;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        uint64_t owner = slot->EndpointBase | side;
        unsigned char staging[KERNEL_IPC_MESSAGE_BYTES];
        zero_bytes(staging, sizeof(staging));
        status = x86_ipc_peek(&slot->Channel, owner, owner, staging, capacity);
        if (status >= 1 && status <= KERNEL_IPC_MESSAGE_BYTES && status <= capacity)
            copy_bytes(bytes, staging, (size_t)status);
    }
    leave_private(&access);
    return status;
}

// ====== kernel_ipc_consume ======

uint64_t kernel_ipc_consume(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return KERNEL_IPC_INVALID;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    uint64_t status = KERNEL_IPC_INVALID;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        uint64_t owner = slot->EndpointBase | side;
        status = x86_ipc_consume(&slot->Channel, owner, owner);
    }
    leave_private(&access);
    return status;
}

// ====== kernel_ipc_receive_ready ======

uint64_t kernel_ipc_receive_ready(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return KERNEL_IPC_INVALID;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    uint64_t status = KERNEL_IPC_INVALID;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        uint64_t owner = slot->EndpointBase | side;
        status = x86_ipc_receive_ready(&slot->Channel, owner, owner);
    }
    leave_private(&access);
    return status;
}

// ====== kernel_ipc_wait_register ======

uint64_t kernel_ipc_wait_register(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid,
    uint64_t handle, uint32_t task_index)
{
    PROCESS_ACCESS access;
    if (task_index == KERNEL_TASK_NONE || !enter_private(table, &access))
        return KERNEL_IPC_INVALID;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    uint64_t status = KERNEL_IPC_INVALID;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        uint64_t owner = slot->EndpointBase | side;
        status = x86_ipc_receive_ready(&slot->Channel, owner, owner);
        if (status == KERNEL_IPC_WOULD_BLOCK)
            slot->WaiterTask[side] = task_index;
    }
    leave_private(&access);
    return status;
}

// ====== kernel_ipc_wait_cancel ======

int kernel_ipc_wait_cancel(KERNEL_PROCESS_TABLE *table, uint64_t caller_pid, uint64_t handle)
{
    PROCESS_ACCESS access;
    if (!enter_private(table, &access))
        return 0;
    int result = 0;
    KERNEL_IPC_SLOT *slot = NULL;
    uint32_t side = 0;
    if (resolve_handle_private(table, caller_pid, handle, &slot, &side, NULL)) {
        slot->WaiterTask[side] = KERNEL_TASK_NONE;
        result = 1;
    }
    leave_private(&access);
    return result;
}
