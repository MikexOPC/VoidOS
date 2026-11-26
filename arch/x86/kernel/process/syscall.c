#include "internal.h"
#include "gui.h"
#include "manager.h"
#include "kernel/mm/heap/heap.h"
#include "kernel/vfs/vfs.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/scheduler/irq/preempt.h"
#endif

extern KERNEL_HEAP *kernel_heap(void);

#define CONSOLE_RING_CAPACITY 512U

static unsigned char console_ring[CONSOLE_RING_CAPACITY];
static uint32_t console_ring_head;
static uint32_t console_ring_tail;
static uint32_t console_ring_count;

// ====== x86_process_console_push ======

int x86_process_console_push(const unsigned char *bytes, uint32_t length)
{
    if (!bytes || !length)
        return 0;
    uint64_t saved_if = ks_irq_save();
    uint32_t pushed = 0U;
    for (uint32_t idx = 0; idx < length && console_ring_count < CONSOLE_RING_CAPACITY; ++idx) {
        console_ring[console_ring_tail] = bytes[idx];
        console_ring_tail = (console_ring_tail + 1U) % CONSOLE_RING_CAPACITY;
        ++console_ring_count;
        ++pushed;
    }
    ks_irq_restore(saved_if);
    return pushed == length;
}

// ====== copy_checked ======

static BOOT_U64 copy_checked(X86_PROCESS_CPU *cpu, void *target, const void *source,
    BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write)
{
    if (!length)
        return 0;
    // Stage through cpu->CopyScratch (in kernel .bss, mapped in both KernelRoot and UserRoot)
    // so kernel buffers on heap-backed per-process syscall stacks stay KPTI-isolated from UserRoot.
    unsigned char *dst_bytes = (unsigned char *)target;
    const unsigned char *src_bytes = (const unsigned char *)source;
    BOOT_U64 offset = 0;
    while (offset < length) {
        BOOT_U64 chunk = length - offset;
        if (chunk > sizeof(cpu->CopyScratch))
            chunk = sizeof(cpu->CopyScratch);
        if (write) {
            for (BOOT_U64 i = 0; i < chunk; ++i)
                cpu->CopyScratch[i] = src_bytes[offset + i];
        }
        // Critical section: UserRoot CR3 and STAC/CLAC must never be interrupted by an IRQ.
        uint64_t saved_if = ks_irq_save();
        cpu->CopyPointer = pointer + offset;
        cpu->CopyLength = chunk;
        cpu->CopyWrite = write;
        BOOT_U64 failed = write
            ? x86_process_copy(dst_bytes + offset, cpu->CopyScratch, chunk)
            : x86_process_copy(cpu->CopyScratch, src_bytes + offset, chunk);
        cpu->CopyPointer = 0;
        cpu->CopyLength = 0;
        if (__readeflags() & FLAG_AC) process_die("PROCESS_AC_RETAINED");
        ks_irq_restore(saved_if);
        if (failed)
            return failed;
        if (!write) {
            for (BOOT_U64 i = 0; i < chunk; ++i)
                dst_bytes[offset + i] = cpu->CopyScratch[i];
        }
        offset += chunk;
    }
    return 0;
}

// ====== x86_process_copy_user ======

BOOT_U64 x86_process_copy_user(X86_PROCESS_CPU *cpu, void *target, const void *source,
    BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write)
{
    return copy_checked(cpu, target, source, pointer, length, write);
}

// ====== sys_write ======

static BOOT_U64 sys_write(X86_PROCESS_CPU *cpu, BOOT_U64 pointer, BOOT_U64 length)
{
    BOOT_U32 range = x86_process_copy_range(pointer, length);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    if (!length) return 0;
    if (!x86_process_write_allowed(cpu->WrittenBytes, length)) return RESULT_QUOTA;
    if (copy_checked(cpu, cpu->Buffer, (const void *)pointer, pointer, length, 0)) {
        for (BOOT_U32 index = 0; index <= MAX_COPY; ++index) ((volatile unsigned char *)cpu->Buffer)[index] = 0;
        return RESULT_RANGE;
    }
    x86_process_console_filter(cpu->Buffer, length);
    if (!kernel_text("USER: ")) process_die("PROCESS_CONSOLE_FAILED");
    for (BOOT_U64 offset = 0; offset < length; offset += TEXT_BLOCK) {
        BOOT_U64 end = offset + TEXT_BLOCK < length ? offset + TEXT_BLOCK : length;
        unsigned char saved = cpu->Buffer[end];
        cpu->Buffer[end] = 0;
        if (!kernel_text((const char *)cpu->Buffer + offset)) process_die("PROCESS_CONSOLE_FAILED");
        cpu->Buffer[end] = saved;
    }
    if (cpu->Buffer[length - 1] != '\n') {
        if (!kernel_text("\n")) process_die("PROCESS_CONSOLE_FAILED");
    }
    cpu->WrittenBytes += length;
    ++cpu->Current->Writes;
    if (cpu->Table && cpu->Pid)
        (void)kernel_process_record_write(cpu->Table, cpu->Pid, length);
    return length;
}

// ====== sys_send ======

static BOOT_U64 sys_send(X86_PROCESS_CPU *cpu, BOOT_U64 handle, BOOT_U64 pointer, BOOT_U64 len)
{
    BOOT_U64 status = cpu->Table
        ? kernel_ipc_send_check(cpu->Table, cpu->Pid, handle, len)
        : x86_ipc_send_check(cpu->IpcChannel, cpu->IpcEndpoint, handle, len);
    if (status) return status;
    unsigned char data[IPC_MESSAGE_BYTES] = {0};
    if (x86_process_copy_range(pointer, len) != 1 ||
        copy_checked(cpu, data, (const void *)pointer, pointer, len, 0)) return RESULT_RANGE;
    if (cpu->Table) {
        uint32_t wake_task = KERNEL_TASK_NONE;
        return kernel_ipc_send(cpu->Table, cpu->Pid, handle, data, len, &wake_task);
    }
    return x86_ipc_send(cpu->IpcChannel, cpu->IpcEndpoint, handle, data, len);
}

// ====== sys_recv ======

static BOOT_U64 sys_recv(X86_PROCESS_CPU *cpu, BOOT_U64 handle, BOOT_U64 pointer, BOOT_U64 capacity)
{
    unsigned char data[IPC_MESSAGE_BYTES] = {0};
    BOOT_U64 len = cpu->Table
        ? kernel_ipc_peek(cpu->Table, cpu->Pid, handle, data, capacity)
        : x86_ipc_peek(cpu->IpcChannel, cpu->IpcEndpoint, handle, data, capacity);
    if (len > sizeof(data)) return len;
    if (!len || x86_process_copy_range(pointer, len) != 1 ||
        copy_checked(cpu, (void *)pointer, data, pointer, len, 1)) return RESULT_RANGE;
    BOOT_U64 consumed = cpu->Table
        ? kernel_ipc_consume(cpu->Table, cpu->Pid, handle)
        : x86_ipc_consume(cpu->IpcChannel, cpu->IpcEndpoint, handle);
    if (consumed) process_die("PROCESS_IPC_STATE");
    return len;
}

// ====== copy_user_path_private ======

static BOOT_U64 copy_user_path_private(
    X86_PROCESS_CPU *cpu,
    BOOT_U64 pointer,
    BOOT_U64 length,
    char *path_out
)
{
    if (!length || length >= KERNEL_VFS_PATH_CAPACITY - 1U) return RESULT_LENGTH;
    BOOT_U32 range = x86_process_copy_range(pointer, length);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    unsigned char raw[KERNEL_VFS_PATH_CAPACITY] = {0};
    if (copy_checked(cpu, raw, (const void *)pointer, pointer, length, 0))
        return RESULT_RANGE;
    uint32_t out_idx = 0;
    if (raw[0] != '/') path_out[out_idx++] = '/';
    for (BOOT_U64 idx = 0; idx < length; ++idx) {
        if (!raw[idx] || raw[idx] < 32 || raw[idx] >= 127)
            return RESULT_RANGE;
        path_out[out_idx++] = (char)raw[idx];
    }
    path_out[out_idx] = 0;
    return 0;
}

// ====== sys_open ======

static BOOT_U64 sys_open(X86_PROCESS_CPU *cpu, BOOT_U64 path_ptr, BOOT_U64 path_len, BOOT_U64 flags)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    if (cpu->Table) {
        KERNEL_PROCESS_BUDGET budget = {0};
        if (kernel_process_get_budget(cpu->Table, cpu->Pid, &budget) &&
            kernel_vfs_count_owned(vfs, cpu->Pid) >= budget.MaxOpenFiles)
            return RESULT_QUOTA;
    }
    char path[KERNEL_VFS_PATH_CAPACITY] = {0};
    BOOT_U64 err = copy_user_path_private(cpu, path_ptr, path_len, path);
    if (err) return err;
    uint32_t fd = 0;
    if (kernel_vfs_open(vfs, cpu->Pid, path, (uint32_t)flags, &fd) != KERNEL_STORAGE_OK)
        return RESULT_UNKNOWN;
    return (BOOT_U64)fd;
}

// ====== sys_read ======

static BOOT_U64 sys_read(X86_PROCESS_CPU *cpu, BOOT_U64 fd, BOOT_U64 buf_ptr, BOOT_U64 length)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    if (!length) return 0;
    BOOT_U32 range = x86_process_copy_range(buf_ptr, length);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    unsigned char data[MAX_COPY] = {0};
    uint32_t bytes_read = 0;
    if (kernel_vfs_read(vfs, cpu->Pid, (uint32_t)fd, data, (uint32_t)length, &bytes_read) != KERNEL_STORAGE_OK)
        return RESULT_UNKNOWN;
    if (bytes_read && copy_checked(cpu, (void *)buf_ptr, data, buf_ptr, bytes_read, 1))
        return RESULT_RANGE;
    return (BOOT_U64)bytes_read;
}

// ====== sys_write_file ======

static BOOT_U64 sys_write_file(X86_PROCESS_CPU *cpu, BOOT_U64 fd, BOOT_U64 buf_ptr, BOOT_U64 length)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    if (!length) return 0;
    BOOT_U32 range = x86_process_copy_range(buf_ptr, length);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    unsigned char data[MAX_COPY] = {0};
    if (copy_checked(cpu, data, (const void *)buf_ptr, buf_ptr, length, 0))
        return RESULT_RANGE;
    uint32_t written = 0;
    if (kernel_vfs_write(vfs, cpu->Pid, (uint32_t)fd, data, (uint32_t)length, &written) != KERNEL_STORAGE_OK)
        return RESULT_UNKNOWN;
    return (BOOT_U64)written;
}

// ====== sys_close ======

static BOOT_U64 sys_close(X86_PROCESS_CPU *cpu, BOOT_U64 fd)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    return kernel_vfs_close(vfs, cpu->Pid, (uint32_t)fd) == KERNEL_STORAGE_OK ? 0ULL : RESULT_UNKNOWN;
}

// ====== sys_unlink ======

static BOOT_U64 sys_unlink(X86_PROCESS_CPU *cpu, BOOT_U64 path_ptr, BOOT_U64 path_len)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    char path[KERNEL_VFS_PATH_CAPACITY] = {0};
    BOOT_U64 err = copy_user_path_private(cpu, path_ptr, path_len, path);
    if (err) return err;
    return kernel_vfs_unlink(vfs, path) == KERNEL_STORAGE_OK ? 0ULL : RESULT_UNKNOWN;
}

// ====== sys_spawn ======

static BOOT_U64 sys_spawn(X86_PROCESS_CPU *cpu, BOOT_U64 path_ptr, BOOT_U64 path_len, BOOT_U64 args_ptr)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!cpu->Table || !cpu->Pid || !x86_process_manager_ready())
        return RESULT_UNKNOWN;
    char path[KERNEL_VFS_PATH_CAPACITY] = {0};
    BOOT_U64 err = copy_user_path_private(cpu, path_ptr, path_len, path);
    if (err) return err;
    X86_SPAWN_ARGS args = {0};
    const KERNEL_PROCESS_BUDGET *budget_ptr = NULL;
    if (args_ptr) {
        if (x86_process_copy_range(args_ptr, sizeof(args)) != 1 ||
            copy_checked(cpu, &args, (const void *)args_ptr, args_ptr, sizeof(args), 0))
            return RESULT_RANGE;
        if (args.UseBudget)
            budget_ptr = &args.Budget;
    }
    if (args.Flags & ~1U)
        return RESULT_RANGE;
    if (args.Flags == 1U) {
        if (x86_process_copy_range(args.InitialArg, 16U) != 1)
            return RESULT_RANGE;
        uint64_t result[2] = {0};
        if (!x86_process_spawn_connected(path, cpu->Pid, budget_ptr, &result[0], &result[1]))
            return RESULT_UNKNOWN;
        if (copy_checked(cpu, (void *)args.InitialArg, result, args.InitialArg, sizeof(result), 1)) {
            uint32_t wake = KERNEL_TASK_NONE;
            (void)kernel_ipc_close_handle(cpu->Table, cpu->Pid, result[1], &wake);
            (void)x86_process_kill(result[0]);
            (void)x86_process_reap(result[0], NULL);
            return RESULT_RANGE;
        }
        return result[0];
    }
    uint64_t child_pid = 0;
    if (!x86_process_spawn_vfs(path, cpu->Pid, 0, budget_ptr, &child_pid))
        return RESULT_UNKNOWN;
    if (args_ptr && args.InitialArg) {
        if (!x86_process_set_arg(child_pid, args.InitialArg)) {
            (void)x86_process_kill(child_pid);
            (void)x86_process_reap(child_pid, NULL);
            return RESULT_UNKNOWN;
        }
    }
    return child_pid;
}

// ====== sys_process_stop ======

static BOOT_U64 sys_process_stop(X86_PROCESS_CPU *cpu, BOOT_U64 pid)
{
    KERNEL_PROCESS_INFO info = {0};
    if (!cpu->Pid ||
        !x86_process_query(pid, &info) ||
        info.ParentPid != cpu->Pid
    )
        return RESULT_RANGE;
    if (info.State == KERNEL_PROC_ZOMBIE)
        return 0;
    return x86_process_kill(pid) ? 0 : RESULT_UNKNOWN;
}

// ====== sys_ipc_close ======

static BOOT_U64 sys_ipc_close(X86_PROCESS_CPU *cpu, BOOT_U64 handle)
{
    uint32_t wake = KERNEL_TASK_NONE;
    return kernel_ipc_close_handle(cpu->Table, cpu->Pid, handle, &wake) ? 0 : RESULT_RANGE;
}

// ====== sys_wait_pid ======

static BOOT_U64 sys_wait_pid(X86_PROCESS_CPU *cpu, BOOT_U64 child_pid, BOOT_U64 status_ptr)
{
    if (!cpu->Table || !cpu->Pid || !child_pid || child_pid == cpu->Pid)
        return RESULT_UNKNOWN;
    KERNEL_PROCESS_INFO info = {0};
    if (!x86_process_query(child_pid, &info) || info.ParentPid != cpu->Pid)
        return RESULT_UNKNOWN;
    if (info.State != KERNEL_PROC_ZOMBIE)
        return KERNEL_IPC_WOULD_BLOCK;
    if (status_ptr && x86_process_copy_range(status_ptr, sizeof(X86_WAIT_STATUS)) != 1)
        return RESULT_RANGE;
    KERNEL_PROCESS_INFO reaped = {0};
    if (!x86_process_reap(child_pid, &reaped))
        return RESULT_UNKNOWN;
    if (status_ptr) {
        X86_WAIT_STATUS out = {0};
        out.ExitKind = reaped.ExitKind;
        out.ExitCode = reaped.ExitCode;
        out.FaultVector = reaped.FaultVector;
        if (copy_checked(cpu, (void *)status_ptr, &out, status_ptr, sizeof(out), 1))
            return RESULT_RANGE;
    }
    return 0ULL;
}

// ====== sys_list_dir ======

static BOOT_U64 sys_list_dir(X86_PROCESS_CPU *cpu, BOOT_U64 path_ptr, BOOT_U64 packed_lens, BOOT_U64 buf_ptr)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    BOOT_U64 path_len = packed_lens >> 32;
    BOOT_U64 buf_len = packed_lens & 0xFFFFFFFFULL;
    if (!buf_len) return 0ULL;
    if (buf_len > MAX_COPY) return RESULT_LENGTH;
    BOOT_U32 range = x86_process_copy_range(buf_ptr, buf_len);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    char path[KERNEL_VFS_PATH_CAPACITY] = {0};
    BOOT_U64 err = copy_user_path_private(cpu, path_ptr, path_len, path);
    if (err) return err;
    unsigned char out[MAX_COPY] = {0};
    uint32_t pos = 0U;
    for (uint32_t idx = 0; idx < 64U; ++idx) {
        KERNEL_VFS_STAT st = {0};
        int r = kernel_vfs_readdir(vfs, path, idx, &st);
        if (r == KERNEL_STORAGE_ERR_NOT_FOUND)
            break;
        if (r != KERNEL_STORAGE_OK)
            return RESULT_UNKNOWN;
        uint32_t needed = st.NameLen + (st.IsDirectory ? 2U : 1U);
        if (pos + needed > (uint32_t)buf_len)
            break;
        for (uint32_t c = 0; c < st.NameLen; ++c)
            out[pos++] = (unsigned char)st.Name[c];
        if (st.IsDirectory)
            out[pos++] = '/';
        out[pos++] = '\n';
    }
    if (pos && copy_checked(cpu, (void *)buf_ptr, out, buf_ptr, pos, 1))
        return RESULT_RANGE;
    return (BOOT_U64)pos;
}

// ====== sys_sync ======

static BOOT_U64 sys_sync(X86_PROCESS_CPU *cpu)
{
    KERNEL_VFS *vfs = kernel_vfs();
    if (!vfs || !cpu->Pid) return RESULT_UNKNOWN;
    return kernel_vfs_sync(vfs) == KERNEL_STORAGE_OK ? 0ULL : RESULT_UNKNOWN;
}

// ====== sys_console_read ======

static BOOT_U64 sys_console_read(X86_PROCESS_CPU *cpu, BOOT_U64 buf_ptr, BOOT_U64 buf_len)
{
    if (!cpu->Pid) return RESULT_UNKNOWN;
    if (!buf_len) return 0ULL;
    if (buf_len > MAX_COPY) return RESULT_LENGTH;
    BOOT_U32 range = x86_process_copy_range(buf_ptr, buf_len);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    unsigned char data[MAX_COPY] = {0};
    uint32_t taken = 0U;
    uint64_t saved_if = ks_irq_save();
    while (taken < (uint32_t)buf_len && console_ring_count > 0U) {
        data[taken++] = console_ring[console_ring_head];
        console_ring_head = (console_ring_head + 1U) % CONSOLE_RING_CAPACITY;
        --console_ring_count;
    }
    ks_irq_restore(saved_if);
    if (!taken)
        return KERNEL_IPC_WOULD_BLOCK;
    if (copy_checked(cpu, (void *)buf_ptr, data, buf_ptr, taken, 1))
        return RESULT_RANGE;
    return (BOOT_U64)taken;
}

// ====== sys_sys_info ======

static BOOT_U64 sys_sys_info(X86_PROCESS_CPU *cpu, BOOT_U64 out_ptr)
{
    if (!out_ptr || x86_process_copy_range(out_ptr, sizeof(X86_SYS_INFO)) != 1)
        return RESULT_RANGE;
    X86_SYS_INFO info = {0};
    if (process_rt.Window && process_rt.Window->Pool) {
        KERNEL_PHYSICAL_STATUS pstat = {0};
        if (kernel_physical_status(process_rt.Window->Pool, &pstat)) {
            info.TotalPhysPages = pstat.TotalPages;
            info.FreePhysPages = pstat.FreePages;
        }
    }
    KERNEL_HEAP *heap = kernel_heap();
    if (heap) {
        KERNEL_HEAP_STATS hstat = kernel_heap_stats(heap);
        info.HeapTotalPages = (BOOT_U64)heap->Pages;
        info.HeapFreePages = hstat.FreePages;
    }
    info.UptimeMs = x86_gui_uptime_now_ms();
#ifdef KERNEL_PS2_INPUT
    info.PreemptTicks = x86_preempt_ticks();
#endif
    if (cpu->Table) {
        KERNEL_PROCESS_STATS proc_stats = {0};
        if (kernel_process_table_stats(cpu->Table, &proc_stats)) {
            info.ActiveProcesses = proc_stats.ActiveProcesses;
            info.ChannelCount = proc_stats.ChannelCount;
        }
    }
    BOOT_U32 online = 1U;
    for (uint32_t idx = 1U; idx < X86_PERCPU_CAPACITY; ++idx) {
        const X86_CPU_LOCAL *ap = x86_percpu_ap(idx);
        if (ap && ap->State == X86_PERCPU_ACTIVE)
            ++online;
    }
    info.OnlineCpus = online;
    if (copy_checked(cpu, (void *)out_ptr, &info, out_ptr, sizeof(info), 1))
        return RESULT_RANGE;
    return 0ULL;
}

// ====== process_budget_expired ======

int process_budget_expired(BOOT_U64 rip)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu || !cpu->Current) return 1;
    if (cpu->RenewBudget) return 0;
    if (cpu->BudgetLeft && !kernel_deadline_expired(cpu->BudgetClock, &cpu->BudgetDeadline)) return 0;
    cpu->Current->Kind = X86_PROCESS_BUDGET;
    cpu->Current->Rip = rip;
    return 1;
}

// ====== x86_process_syscall_dispatch ======

int x86_process_syscall_dispatch(BOOT_U64 *frame)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu || !cpu->Current)
        process_die("PROCESS_CPU_INVALID");
    cpu->InSyscall = 1;
    int outcome = 1;
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_negative == 31) (void)*(const volatile unsigned char *)0x10ULL;
    if (process_negative == 32) x86_process_probe_read(X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_HEAP_SLOT));
#endif
    if (!x86_process_return_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        cpu->Current->Kind = X86_PROCESS_BAD_FRAME;
        cpu->Current->Rip = IRET(frame, 0);
        cpu->InSyscall = 0;
        return 2;
    }
    if (process_budget_expired(IRET(frame, 0))) {
        cpu->InSyscall = 0;
        return 2;
    }
    BOOT_U64 number = GPR(frame, GPR_RAX);
    if (number == SYS_EXIT) {
        cpu->Current->Kind = X86_PROCESS_EXIT;
        cpu->Current->Code = GPR(frame, GPR_RDI);
        cpu->InSyscall = 0;
        return 2;
    }
    // Interruptible syscall path on the permanent common IDT: IF=1 outside minimal critical sections.
    if (process_rt.Permanent)
        _enable();
    int ipc_enabled = cpu->Table != 0 || cpu->IpcEndpoint != 0;
    if (cpu->YieldFrame && (number == SYS_YIELD || (ipc_enabled && number == SYS_WAIT_READABLE))) {
        BOOT_U64 handle = GPR(frame, GPR_RDI);
        BOOT_U64 result = 0;
        if (number == SYS_WAIT_READABLE) {
            result = cpu->Table
                ? kernel_ipc_receive_ready(cpu->Table, cpu->Pid, handle)
                : x86_ipc_receive_ready(cpu->IpcChannel, cpu->IpcEndpoint, handle);
        }
        GPR(frame, GPR_RAX) = result;
        if (number == SYS_WAIT_READABLE && result != IPC_WOULD_BLOCK) {
            outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
        } else {
            cpu->Waiting = number == SYS_WAIT_READABLE;
            cpu->WaitHandle = cpu->Waiting ? handle : 0;
            for (BOOT_U32 index = 0; index < USER_FRAME_WORDS; ++index) cpu->YieldFrame[index] = frame[index];
            cpu->Yielded = 1;
            outcome = 2;
        }
    } else if (cpu->YieldFrame && cpu->Table && number == SYS_GUI_WAIT) {
        BOOT_U64 timeout_ms = GPR(frame, GPR_RDI);
        if (timeout_ms > 10000ULL)
            timeout_ms = 10000ULL;
        BOOT_U64 now_ms = x86_gui_uptime_now_ms();
        GPR(frame, GPR_RAX) = 0;
        if (timeout_ms == 0ULL || x86_gui_wait_ready(cpu->Pid) ||
            kernel_ipc_any_readable(cpu->Table, cpu->Pid)) {
            outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
        } else {
            BOOT_U64 deadline = now_ms + timeout_ms;
            cpu->Waiting = 1;
            cpu->WaitHandle = WAIT_HANDLE_GUI_MASK | (deadline & ~WAIT_HANDLE_GUI_MASK);
            for (BOOT_U32 index = 0; index < USER_FRAME_WORDS; ++index) cpu->YieldFrame[index] = frame[index];
            cpu->Yielded = 1;
            outcome = 2;
        }
    } else if (ipc_enabled && (number == SYS_SEND || number == SYS_RECV)) {
        BOOT_U64 handle = GPR(frame, GPR_RDI), pointer = GPR(frame, GPR_RSI), length = GPR(frame, GPR_RDX);
        GPR(frame, GPR_RAX) = number == SYS_SEND ? sys_send(cpu, handle, pointer, length) : sys_recv(cpu, handle, pointer, length);
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_OPEN) {
        GPR(frame, GPR_RAX) = sys_open(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI), GPR(frame, GPR_RDX));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_READ) {
        GPR(frame, GPR_RAX) = sys_read(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI), GPR(frame, GPR_RDX));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_WRITE_FILE) {
        GPR(frame, GPR_RAX) = sys_write_file(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI), GPR(frame, GPR_RDX));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_CLOSE) {
        GPR(frame, GPR_RAX) = sys_close(cpu, GPR(frame, GPR_RDI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_UNLINK) {
        GPR(frame, GPR_RAX) = sys_unlink(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SPAWN) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = sys_spawn(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI), GPR(frame, GPR_RDX));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_WAIT_PID) {
        GPR(frame, GPR_RAX) = sys_wait_pid(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_LIST_DIR) {
        GPR(frame, GPR_RAX) = sys_list_dir(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI), GPR(frame, GPR_RDX));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SYNC) {
        GPR(frame, GPR_RAX) = sys_sync(cpu);
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_CONSOLE_READ) {
        GPR(frame, GPR_RAX) = sys_console_read(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_DISPLAY_INFO) {
        GPR(frame, GPR_RAX) = x86_gui_sys_display_info(cpu, GPR(frame, GPR_RDI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_DISPLAY_ACQUIRE) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = x86_gui_sys_display_acquire(cpu, GPR(frame, GPR_RDI));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_DISPLAY_PRESENT) {
        GPR(frame, GPR_RAX) = x86_gui_sys_display_present(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_INPUT_POLL) {
        GPR(frame, GPR_RAX) = x86_gui_sys_input_poll(cpu, GPR(frame, GPR_RDI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SHM_CREATE) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = x86_gui_sys_shm_create(cpu, GPR(frame, GPR_RDI));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SHM_MAP) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = x86_gui_sys_shm_map(cpu, GPR(frame, GPR_RDI));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_PROCESS_STOP) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = sys_process_stop(cpu, GPR(frame, GPR_RDI));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_IPC_CLOSE) {
        GPR(frame, GPR_RAX) = sys_ipc_close(cpu, GPR(frame, GPR_RDI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SHM_PUBLISH) {
        GPR(frame, GPR_RAX) = x86_gui_sys_shm_publish(cpu, GPR(frame, GPR_RDI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SHM_VIEW) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = x86_gui_sys_shm_view(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SHM_UNMAP) {
        ++cpu->SyscallNoSuspend;
        GPR(frame, GPR_RAX) = x86_gui_sys_shm_unmap(cpu, GPR(frame, GPR_RDI));
        --cpu->SyscallNoSuspend;
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else if (number == SYS_SYS_INFO) {
        GPR(frame, GPR_RAX) = sys_sys_info(cpu, GPR(frame, GPR_RDI));
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    } else {
        GPR(frame, GPR_RAX) = number == SYS_WRITE ? sys_write(cpu, GPR(frame, GPR_RDI), GPR(frame, GPR_RSI)) : RESULT_UNKNOWN;
#ifdef KERNEL_TEST_K6_PROCESS
        if (process_delay_next_syscall) {
            process_delay_next_syscall = 0;
            BOOT_U32 attempt = 0;
            while (!kernel_deadline_expired(cpu->BudgetClock, &cpu->BudgetDeadline)) {
                if (++attempt == 0x1000000U) process_die("PROCESS_CLOCK_FAILED");
                _mm_pause();
            }
            if (!kernel_log(0, "KERNEL_K6_SYSCALL_DEADLINE_OK")) process_die("PROCESS_CONSOLE_FAILED");
        }
#endif
        outcome = process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    }
    if (process_rt.Permanent)
        _disable();
    cpu->InSyscall = 0;
    return outcome;
}

// ====== x86_process_copy_recover ======

int x86_process_copy_recover(BOOT_U64 *frame, BOOT_U64 cr2)
{
    enum { VECTOR, ERROR_CODE, RIP, CS, FLAGS };
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!cpu || !x86_process_fault_recoverable(frame[RIP], frame[CS], frame[FLAGS], frame[ERROR_CODE], cr2,
        (BOOT_U64)x86_process_copy_first, (BOOT_U64)x86_process_copy_last,
        cpu->CopyPointer, cpu->CopyLength, cpu->CopyWrite)) return 0;
    frame[RIP] = (BOOT_U64)x86_process_copy_fixup;
    frame[FLAGS] &= ~FLAG_AC;
    return 1;
}
