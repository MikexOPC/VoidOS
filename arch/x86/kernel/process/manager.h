#ifndef OS_X86_PROCESS_MANAGER_H
#define OS_X86_PROCESS_MANAGER_H

#include "process.h"
#include "kernel/process/table.h"

int x86_process_manager_init(void);
int x86_process_manager_ready(void);
KERNEL_PROCESS_TABLE *x86_process_manager_table(void);

int x86_process_spawn(const X86_PROCESS_IMAGE *image, uint32_t flags, uint64_t *pid_out);
int x86_process_spawn_with_budget(const X86_PROCESS_IMAGE *image, uint64_t parent_pid,
    uint32_t flags, const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out);
int x86_process_spawn_vfs(const char *path, uint64_t parent_pid, uint32_t flags,
    const KERNEL_PROCESS_BUDGET *requested_budget, uint64_t *pid_out);
int x86_process_set_arg(uint64_t pid, uint64_t initial_arg);
int x86_process_channel_create(uint64_t pid0, uint64_t pid1,
    uint64_t *handle0_out, uint64_t *handle1_out);
int x86_process_step(uint64_t pid, int renew_budget, uint32_t *state_out);
int x86_process_step_children(uint64_t parent_pid);
int x86_process_kill(uint64_t pid);
int x86_process_reap(uint64_t pid, KERNEL_PROCESS_INFO *info_out);
int x86_process_query(uint64_t pid, KERNEL_PROCESS_INFO *info_out);
int x86_process_stats(KERNEL_PROCESS_STATS *stats_out);
int x86_process_console_push(const unsigned char *bytes, uint32_t length);

#endif
