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

// ====== x86_process_spawn_with_budget ======

int x86_process_spawn_with_budget(const X86_PROCESS_IMAGE *image, uint64_t parent_pid,
    uint32_t flags, const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out)
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
        !kernel_process_set_state(&manager_table, pid, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE)) {
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

// ====== x86_process_spawn ======

int x86_process_spawn(const X86_PROCESS_IMAGE *image, uint32_t flags, uint64_t *pid_out)
{
    return x86_process_spawn_with_budget(image, KERNEL_PID_KERNEL, flags, NULL, pid_out);
}

// ====== x86_process_spawn_vfs ======

int x86_process_spawn_vfs(const char *path, uint64_t parent_pid, uint32_t flags,
    const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out)
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
        ok = x86_process_spawn_with_budget(&image, parent_pid, flags, requested_budget, pid_out);
    }
    (void)kernel_heap_free(heap, buffer);
    return ok;
}

// ====== x86_process_set_arg ======

int x86_process_set_arg(uint64_t pid, uint64_t initial_arg)
{
    KERNEL_PROCESS_INFO info;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, &info);
    if (!arch || info.State != KERNEL_PROC_RUNNABLE || arch->Slot.Yields)
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
    KERNEL_PROCESS_INFO info;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, &info);
    if (!arch)
        return 0;
    if (info.State == KERNEL_PROC_ZOMBIE) {
        if (state_out) *state_out = KERNEL_PROC_ZOMBIE;
        return 1;
    }
    if (info.State == KERNEL_PROC_BLOCKED) {
        BOOT_U64 ready = kernel_ipc_receive_ready(&manager_table, pid, arch->WaitHandle);
        if (ready == KERNEL_IPC_WOULD_BLOCK) {
            if (state_out) *state_out = KERNEL_PROC_BLOCKED;
            return 1;
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

// ====== x86_process_reap ======

int x86_process_reap(uint64_t pid, KERNEL_PROCESS_INFO *info_out)
{
    KERNEL_HEAP *heap = kernel_heap();
    KERNEL_PROCESS_INFO info;
    X86_PROCESS_ARCH *arch = arch_lookup_private(pid, &info);
    if (!heap || !arch || (info.State != KERNEL_PROC_ZOMBIE && info.State != KERNEL_PROC_CREATED))
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

// ====== x86_process_step_children ======

int x86_process_step_children(uint64_t parent_pid)
{
    if (!manager_ready || !parent_pid)
        return 0;
    uint64_t children[16] = {0};
    uint32_t count = kernel_process_list_children(&manager_table, parent_pid, children, 16U);
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t state = 0;
        if (!x86_process_step(children[index], 0, &state))
            return 0;
    }
    return 1;
}
