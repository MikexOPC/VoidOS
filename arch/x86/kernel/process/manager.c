#include "manager.h"
#include "gui.h"
#include "internal.h"
#include "arch/x86/mm/heap/binding.h"
#include "kernel/mm/physical/holdings.h"
#include "kernel/vfs/vfs.h"

typedef struct {
    X86_PROCESS_SLOT    Slot;
    X86_PROCESS_RESULT  Result;
    uint64_t            Pid;
    uint64_t            InitialArg;
    uint64_t            WaitHandle;
    uint32_t            Flags;
    uint32_t            Reserved;
} X86_PROCESS_ARCH;

static KERNEL_PROCESS_TABLE manager_table;
static KERNEL_HOLDING manager_holding;
static uint64_t manager_held_pages;
static uint32_t manager_ready;

// ====== exit_kind_private ======

static uint32_t exit_kind_private(BOOT_U64 kind)
{
    if (kind == X86_PROCESS_EXIT) return KERNEL_PROC_EXIT_NORMAL;
    if (kind == X86_PROCESS_FAULT) return KERNEL_PROC_EXIT_FAULT;
    if (kind == X86_PROCESS_BAD_FRAME) return KERNEL_PROC_EXIT_BAD_FRAME;
    if (kind == X86_PROCESS_BUDGET) return KERNEL_PROC_EXIT_BUDGET;
    return KERNEL_PROC_EXIT_KILLED;
}

// ====== arch_lookup_private ======

static X86_PROCESS_ARCH *arch_lookup_private(uint64_t pid, KERNEL_PROCESS_INFO *info_out)
{
    KERNEL_PROCESS_INFO info;
    if (!manager_ready || !pid || !kernel_process_info(&manager_table, pid, &info) || !info.ArchContext)
        return NULL;
    if (info_out)
        *info_out = info;
    return (X86_PROCESS_ARCH *)(uintptr_t)info.ArchContext;
}

// ====== x86_process_manager_init ======

int x86_process_manager_init(void)
{
    if (manager_ready)
        return 1;
    KERNEL_HEAP *heap = kernel_heap();
    if (!heap || !x86_process_runtime_ready())
        return 0;
    if (!kernel_process_table_init(&manager_table, heap,
        KERNEL_PROCESS_INIT_CAPACITY, KERNEL_IPC_INIT_CHANNELS))
        return 0;
    manager_held_pages = 0;
    if (!kernel_holdings_register(&manager_holding, 0)) {
        (void)kernel_process_table_destroy(&manager_table);
        return 0;
    }
    if (!x86_gui_init()) {
        (void)kernel_process_table_destroy(&manager_table);
        return 0;
    }
    manager_ready = 1;
    return 1;
}

// ====== x86_process_manager_ready ======

int x86_process_manager_ready(void)
{
    return manager_ready != 0;
}

// ====== x86_process_manager_table ======

KERNEL_PROCESS_TABLE *x86_process_manager_table(void)
{
    return manager_ready ? &manager_table : NULL;
}

// ====== spawn_private ======

static int spawn_private(const X86_PROCESS_IMAGE *image, uint64_t parent_pid,
    uint32_t flags, const KERNEL_PROCESS_BUDGET *requested_budget, int publish, uint64_t *pid_out)
{
    KERNEL_HEAP *heap = kernel_heap();
    if (!manager_ready || !heap || !image || !pid_out)
        return 0;
    KERNEL_PROCESS_BUDGET parent_budget = {0};
    const KERNEL_PROCESS_BUDGET *parent_ptr = NULL;
    if (parent_pid != KERNEL_PID_KERNEL) {
        if (!kernel_process_get_budget(&manager_table, parent_pid, &parent_budget) ||
            !(parent_budget.Flags & KERNEL_BUDGET_FLAG_SPAWN))
            return 0;
        parent_ptr = &parent_budget;
    }
    KERNEL_PROCESS_BUDGET derived = {0};
    if (!kernel_process_budget_derive(parent_ptr, requested_budget, &derived))
        return 0;
    X86_PROCESS_ARCH *arch = kernel_heap_alloc(heap, sizeof(*arch), 16);
    if (!arch)
        return 0;
    if (!x86_process_slot_prepare_budgeted(&arch->Slot, image,
        derived.MaxImagePages, derived.CpuBudgetMs)) {
        (void)kernel_heap_free(heap, arch);
        return 0;
    }
    arch->InitialArg = 0;
    arch->WaitHandle = 0;
    uint64_t pages = arch->Slot.Lease.Pages;
    if (!kernel_holdings_update(&manager_holding, manager_held_pages + pages)) {
        (void)x86_process_slot_reap(&arch->Slot);
        (void)kernel_heap_free(heap, arch);
        return 0;
    }
    manager_held_pages += pages;
    uint64_t pid = 0;
    if (!kernel_process_create_with_budget(&manager_table, parent_pid, flags,
        (uint64_t)(uintptr_t)arch, requested_budget, &pid) ||
        (publish && !kernel_process_set_state(&manager_table, pid, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE))
    ) {
        if (pid)
            (void)kernel_process_reap(&manager_table, pid, NULL);
        manager_held_pages -= pages;
        (void)kernel_holdings_update(&manager_holding, manager_held_pages);
        (void)x86_process_slot_reap(&arch->Slot);
        (void)kernel_heap_free(heap, arch);
        return 0;
    }
    arch->Pid = pid;
    arch->Flags = flags;
    *pid_out = pid;
    return 1;
}

// ====== x86_process_spawn_with_budget ======

int x86_process_spawn_with_budget(const X86_PROCESS_IMAGE *image, uint64_t parent_pid,
    uint32_t flags, const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out)
{
    return spawn_private(image, parent_pid, flags, requested_budget, 1, pid_out);
}

// ====== x86_process_spawn ======

int x86_process_spawn(const X86_PROCESS_IMAGE *image, uint32_t flags, uint64_t *pid_out)
{
    return x86_process_spawn_with_budget(image, KERNEL_PID_KERNEL, flags, NULL, pid_out);
}

// ====== spawn_vfs_private ======

static int spawn_vfs_private(const char *path, uint64_t parent_pid, uint32_t flags,
    const KERNEL_PROCESS_BUDGET *requested_budget, int publish, uint64_t *pid_out)
{
    KERNEL_VFS *vfs = kernel_vfs();
    KERNEL_HEAP *heap = kernel_heap();
    if (!manager_ready || !vfs || !heap || !path || !pid_out)
        return 0;
    KERNEL_VFS_STAT st = {0};
    if (kernel_vfs_stat(vfs, path, &st) != KERNEL_STORAGE_OK || st.IsDirectory ||
        st.Size < 64U || st.Size > (uint32_t)(KERNEL_PROCESS_MAX_IMAGE_PAGES * 4096U))
        return 0;
    unsigned char *buffer = kernel_heap_alloc(heap, st.Size, 16);
    if (!buffer)
        return 0;
    uint32_t read_bytes = 0;
    int ok = kernel_vfs_read_file(vfs, path, 0, buffer, st.Size, &read_bytes) == KERNEL_STORAGE_OK &&
        read_bytes == st.Size;
    if (ok) {
        X86_PROCESS_IMAGE image = { buffer, st.Size };
        ok = spawn_private(&image, parent_pid, flags, requested_budget, publish, pid_out);
    }
    (void)kernel_heap_free(heap, buffer);
    return ok;
}

// ====== x86_process_spawn_vfs ======

int x86_process_spawn_vfs(const char *path, uint64_t parent_pid, uint32_t flags,
    const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out)
{
    return spawn_vfs_private(path, parent_pid, flags, requested_budget, 1, pid_out);
}

// ====== x86_process_spawn_connected ======

int x86_process_spawn_connected(const char *path, uint64_t parent_pid,
    const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out, uint64_t *handle_out)
{
    if (!pid_out ||
        !handle_out
    )
        return 0;
    uint64_t pid = 0, parent_handle = 0, child_handle = 0;
    if (!spawn_vfs_private(path, parent_pid, 0, requested_budget, 0, &pid))
        return 0;
    if (!x86_process_channel_create(parent_pid, pid, &parent_handle, &child_handle) ||
        (child_handle & (1ULL << 63)) ||
        !x86_process_set_arg(pid, child_handle | (1ULL << 63)) ||
        !kernel_process_set_state(&manager_table, pid, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE)
    ) {
        uint32_t wake = KERNEL_TASK_NONE;
        if (parent_handle)
            (void)kernel_ipc_close_handle(&manager_table, parent_pid, parent_handle, &wake);
        (void)x86_process_kill(pid);
        (void)x86_process_reap(pid, NULL);
        return 0;
    }
    *pid_out = pid;
    *handle_out = parent_handle;
    return 1;
}

// ====== x86_process_set_arg ======

int x86_process_set_arg(uint64_t pid, uint64_t initial_arg)
{
    KERNEL_PROCESS_INFO info;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, &info);
    if (!arch ||
        (info.State != KERNEL_PROC_RUNNABLE && info.State != KERNEL_PROC_CREATED) ||
        arch->Slot.Yields
    )
        return 0;
    arch->InitialArg = initial_arg;
    return 1;
}

// ====== x86_process_channel_create ======

int x86_process_channel_create(uint64_t pid0, uint64_t pid1,
    uint64_t *handle0_out, uint64_t *handle1_out)
{
    if (!manager_ready)
        return 0;
    return kernel_ipc_channel_create(&manager_table, pid0, pid1, handle0_out, handle1_out);
}

// ====== x86_process_step ======

int x86_process_step(uint64_t pid, int renew_budget, uint32_t *state_out)
{
    if (x86_process_cpu_current() == &x86_process_cpus[0]) {
        uint64_t saved_if = ks_irq_save();
        x86_gui_clock_tick();
        ks_irq_restore(saved_if);
    }
    KERNEL_PROCESS_INFO info;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, &info);
    if (!arch)
        return 0;
    if (info.State == KERNEL_PROC_ZOMBIE) {
        if (state_out) *state_out = KERNEL_PROC_ZOMBIE;
        return 1;
    }
    if (info.State == KERNEL_PROC_BLOCKED) {
        if ((arch->WaitHandle & WAIT_HANDLE_GUI_MASK) != 0ULL) {
            uint64_t deadline = arch->WaitHandle & ~WAIT_HANDLE_GUI_MASK;
            if (!x86_gui_wait_ready(pid) &&
                !kernel_ipc_any_readable(&manager_table, pid) &&
                x86_gui_uptime_now_ms() < deadline) {
                if (state_out) *state_out = KERNEL_PROC_BLOCKED;
                return 1;
            }
            arch->WaitHandle = 0;
        } else {
            BOOT_U64 ready = kernel_ipc_receive_ready(&manager_table, pid, arch->WaitHandle);
            if (ready == KERNEL_IPC_WOULD_BLOCK) {
                if (state_out) *state_out = KERNEL_PROC_BLOCKED;
                return 1;
            }
        }
        (void)kernel_process_set_state(&manager_table, pid, KERNEL_PROC_BLOCKED, KERNEL_PROC_RUNNABLE);
    }
    if (!x86_process_slot_step_managed(&arch->Slot, &arch->Result,
        &manager_table, pid, arch->InitialArg, &arch->WaitHandle, renew_budget))
        return 0;
    if (arch->Result.Kind) {
        uint32_t wake_count = 0;
        x86_gui_release_owned(pid, &arch->Slot);
        if (kernel_vfs())
            kernel_vfs_close_owned(kernel_vfs(), pid);
        (void)kernel_process_terminate(&manager_table, pid,
            exit_kind_private(arch->Result.Kind), arch->Result.Code,
            arch->Result.Vector, arch->Result.Error, arch->Result.Rip, arch->Result.Cr2,
            NULL, 0, &wake_count);
        if (state_out) *state_out = KERNEL_PROC_ZOMBIE;
        return 1;
    }
    if (arch->Slot.Waiting) {
        (void)kernel_process_set_state(&manager_table, pid, KERNEL_PROC_RUNNABLE, KERNEL_PROC_BLOCKED);
        if (state_out) *state_out = KERNEL_PROC_BLOCKED;
        return 1;
    }
    if (state_out) *state_out = KERNEL_PROC_RUNNABLE;
    return 1;
}

// ====== x86_process_kill ======

int x86_process_kill(uint64_t pid)
{
    uint32_t wake_count = 0;
    if (!manager_ready || !pid)
        return 0;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, NULL);
    x86_gui_release_owned(pid, arch ? &arch->Slot : NULL);
    if (kernel_vfs())
        kernel_vfs_close_owned(kernel_vfs(), pid);
    return kernel_process_terminate(&manager_table, pid, KERNEL_PROC_EXIT_KILLED, 0,
        0, 0, 0, 0, NULL, 0, &wake_count);
}

// ====== reap_descendants_private ======

static int reap_descendants_private(uint64_t pid)
{
    KERNEL_HEAP *heap = kernel_heap();
    uint32_t capacity = KERNEL_PROCESS_MAX_CAPACITY;
    uint64_t *children = kernel_heap_alloc(heap, (size_t)capacity * sizeof(*children), 16);
    if (!children)
        return 0;
    uint32_t count = kernel_process_list_children(&manager_table, pid, children, capacity);
    for (uint32_t index = 0; index < count && count < capacity; ++index) {
        count += kernel_process_list_children(&manager_table, children[index],
            children + count, capacity - count);
    }
    int ok = 1;
    while (ok && count) {
        uint64_t child = children[--count];
        KERNEL_PROCESS_INFO info = {0};
        ok = x86_process_query(child, &info);
        if (ok && info.State != KERNEL_PROC_ZOMBIE)
            ok = x86_process_kill(child);
        if (ok)
            ok = x86_process_reap(child, NULL);
    }
    (void)kernel_heap_free(heap, children);
    return ok;
}

// ====== x86_process_reap ======

int x86_process_reap(uint64_t pid, KERNEL_PROCESS_INFO *info_out)
{
    KERNEL_HEAP *heap = kernel_heap();
    KERNEL_PROCESS_INFO info;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, &info);
    if (!heap || !arch || (info.State != KERNEL_PROC_ZOMBIE && info.State != KERNEL_PROC_CREATED))
        return 0;
    if (!reap_descendants_private(pid))
        return 0;
    x86_gui_release_owned(pid, &arch->Slot);
    if (kernel_vfs())
        kernel_vfs_close_owned(kernel_vfs(), pid);
    uint64_t pages = arch->Slot.Lease.Pages;
    if (!x86_process_slot_reap(&arch->Slot))
        return 0;
    if (pages > manager_held_pages)
        kernel_panic("PROCESS_HOLDING_UNDERFLOW");
    manager_held_pages -= pages;
    if (!kernel_holdings_update(&manager_holding, manager_held_pages))
        kernel_panic("PROCESS_HOLDING_UPDATE_FAILED");
    if (!kernel_process_reap(&manager_table, pid, info_out))
        kernel_panic("PROCESS_TABLE_REAP_FAILED");
    if (!kernel_heap_free(heap, arch))
        kernel_panic("PROCESS_ARCH_FREE_FAILED");
    return 1;
}

// ====== x86_process_query ======

int x86_process_query(uint64_t pid, KERNEL_PROCESS_INFO *info_out)
{
    if (!manager_ready || !info_out)
        return 0;
    return kernel_process_info(&manager_table, pid, info_out);
}

// ====== x86_process_stats ======

int x86_process_stats(KERNEL_PROCESS_STATS *stats_out)
{
    if (!manager_ready || !stats_out)
        return 0;
    return kernel_process_table_stats(&manager_table, stats_out);
}

// ====== x86_process_slot_for_pid ======

X86_PROCESS_SLOT *x86_process_slot_for_pid(uint64_t pid)
{
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, NULL);
    return arch ? &arch->Slot : NULL;
}

// Reusable BSP scratch buffer to avoid heap alloc/free on every GUI step.
static uint64_t manager_step_children_buf[KERNEL_PROCESS_MAX_CAPACITY];

// ====== x86_process_step_children_budget ======

int x86_process_step_children_budget(uint64_t parent_pid, int renew_budget)
{
    if (!manager_ready || !parent_pid)
        return 0;
    uint32_t capacity = KERNEL_PROCESS_MAX_CAPACITY;
    uint64_t *children = manager_step_children_buf;
    uint32_t count = kernel_process_list_children(&manager_table, parent_pid, children, capacity);
    int ok = 1;
    for (uint32_t index = 0; ok && index < count; ++index) {
        uint32_t state = 0;
        ok = x86_process_step(children[index], renew_budget, &state);
        if (ok && state != KERNEL_PROC_ZOMBIE && count < capacity) {
            count += kernel_process_list_children(&manager_table, children[index],
                children + count, capacity - count);
        }
    }
    return ok;
}

// ====== x86_process_step_children ======

int x86_process_step_children(uint64_t parent_pid)
{
    return x86_process_step_children_budget(parent_pid, 0);
}
