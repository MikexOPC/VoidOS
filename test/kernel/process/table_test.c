#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/mm/heap/heap.h"
#include "kernel/process/table.h"
#include "kernel/sync/internal.h"

#define TEST_HEAP_PAGES 1024U

static KERNEL_LOCK_CONTEXT host_lock_context;
static uint64_t host_irq_flags = 0x200ULL;
static uint32_t host_clock_ticks = 1;

// ====== host_clock_read ======

static int host_clock_read(void *context, uint32_t *out)
{
    (void)context;
    *out = host_clock_ticks++;
    return 1;
}

// ====== ks_irq_save ======

uint64_t ks_irq_save(void)
{
    uint64_t previous = host_irq_flags;
    host_irq_flags = 0;
    return previous;
}

// ====== ks_irq_restore ======

void ks_irq_restore(uint64_t flags)
{
    host_irq_flags = flags;
}

// ====== ks_irq_state ======

uint64_t ks_irq_state(void)
{
    return host_irq_flags;
}

// ====== ks_pause ======

void ks_pause(void)
{
}

// ====== ks_fail_stop ======

__declspec(noreturn) void ks_fail_stop(const char *reason)
{
    fprintf(stderr, "HOST_FAIL_STOP: %s\n", reason ? reason : "(null)");
    abort();
}

// ====== kernel_lock_current_context ======

KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void)
{
    return &host_lock_context;
}

// ====== setup_heap ======

static void setup_heap(KERNEL_HEAP *heap, void **backing_out)
{
    void *backing = _aligned_malloc((size_t)TEST_HEAP_PAGES * 4096U, 4096U);
    assert(backing != NULL);
    memset(heap, 0, sizeof(*heap));
    assert(kernel_heap_init(heap, backing, TEST_HEAP_PAGES));
    *backing_out = backing;
}

// ====== test_dynamic_growth_and_loopback ======

static void test_dynamic_growth_and_loopback(KERNEL_HEAP *heap)
{
    KERNEL_PROCESS_TABLE table = {0};
    KERNEL_PROCESS_STATS stats = {0};
    uint64_t pids[40] = {0};
    uint64_t h0[20] = {0};
    uint64_t h1[20] = {0};

    assert(kernel_process_table_init(&table, heap, 4, 4));
    assert(!kernel_process_table_init(&table, heap, 4, 4));

    for (uint32_t index = 0; index < 40; ++index) {
        assert(kernel_process_create(&table, 0, 0x1000ULL + index, &pids[index]));
        assert(pids[index] == (uint64_t)index + 1ULL);
        assert(kernel_process_bind_task(&table, pids[index], 100U + index));
        assert(kernel_process_set_state(&table, pids[index], KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));
    }

    assert(kernel_process_table_stats(&table, &stats));
    assert(stats.ProcessCount == 40U);
    assert(stats.ActiveProcesses == 40U);
    assert(stats.ZombieProcesses == 0U);
    assert(stats.ProcessCapacity >= 64U);

    // Loopback channels on pids[0] force handle array growth (8 -> 16 -> 32 -> 64)
    // and channel array growth (4 -> 8 -> 16 -> 32) on the same process.
    for (uint32_t index = 0; index < 20; ++index) {
        assert(kernel_ipc_channel_create(&table, pids[0], pids[0], &h0[index], &h1[index]));
        assert((h0[index] ^ 1ULL) == h1[index]);
    }

    assert(kernel_process_table_stats(&table, &stats));
    assert(stats.ChannelCount == 20U);
    assert(stats.ChannelCapacity >= 32U);

    for (uint32_t index = 0; index < 20; ++index) {
        unsigned char payload[4] = { (unsigned char)index, 0x55, 0xaa, 0x01 };
        unsigned char reply[4] = {0};
        uint32_t wake = KERNEL_TASK_NONE;
        assert(kernel_ipc_send(&table, pids[0], h0[index], payload, 4, &wake) == 4);
        assert(wake == KERNEL_TASK_NONE);
        assert(kernel_ipc_peek(&table, pids[0], h1[index], reply, 4) == 4);
        assert(memcmp(payload, reply, 4) == 0);
        assert(kernel_ipc_consume(&table, pids[0], h1[index]) == 0);
    }

    for (uint32_t index = 0; index < 40; ++index) {
        uint32_t wake_count = 0;
        assert(kernel_process_terminate(&table, pids[index], KERNEL_PROC_EXIT_NORMAL, index,
            0, 0, 0, 0, NULL, 0, &wake_count));
        assert(wake_count == 0);
        KERNEL_PROCESS_INFO info = {0};
        assert(kernel_process_reap(&table, pids[index], &info));
        assert(info.Pid == pids[index]);
        assert(info.ExitKind == KERNEL_PROC_EXIT_NORMAL);
        assert(info.ExitCode == index);
    }

    assert(kernel_process_table_stats(&table, &stats));
    assert(stats.ProcessCount == 0U);
    assert(stats.ChannelCount == 0U);
    assert(kernel_process_table_destroy(&table));
}

// ====== test_state_transitions_and_isolation ======

static void test_state_transitions_and_isolation(KERNEL_HEAP *heap)
{
    KERNEL_PROCESS_TABLE table = {0};
    uint64_t pid_a = 0, pid_b = 0, pid_c = 0;
    uint64_t handle_a = 0, handle_b = 0;
    uint64_t handle_k = 0, handle_u = 0;
    unsigned char buf[16] = {0};
    uint32_t wake = KERNEL_TASK_NONE;

    assert(kernel_process_table_init(&table, heap, 8, 8));
    assert(kernel_process_create(&table, 0, 0xaaULL, &pid_a));
    assert(kernel_process_create(&table, 0, 0xbbULL, &pid_b));
    assert(kernel_process_create(&table, KERNEL_PROC_FLAG_RESIDENT, 0xccULL, &pid_c));

    // Illegal transitions before runnable.
    assert(!kernel_process_set_state(&table, pid_a, KERNEL_PROC_CREATED, KERNEL_PROC_BLOCKED));
    assert(!kernel_process_set_state(&table, pid_a, KERNEL_PROC_CREATED, KERNEL_PROC_ZOMBIE));
    assert(!kernel_process_set_state(&table, pid_a, KERNEL_PROC_CREATED, KERNEL_PROC_FREE));

    assert(kernel_process_set_state(&table, pid_a, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));
    assert(kernel_process_set_state(&table, pid_b, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));
    assert(kernel_process_set_state(&table, pid_c, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));

    // Cannot reap a live runnable process.
    assert(!kernel_process_reap(&table, pid_a, NULL));

    // Create user<->user and kernel<->user channels.
    assert(kernel_ipc_channel_create(&table, pid_a, pid_b, &handle_a, &handle_b));
    assert(kernel_ipc_channel_create(&table, KERNEL_PID_KERNEL, pid_c, &handle_k, &handle_u));

    // Cross-process handle forgery must fail.
    assert(kernel_ipc_send_check(&table, pid_a, handle_b, 2) == KERNEL_IPC_INVALID);
    assert(kernel_ipc_send_check(&table, pid_c, handle_a, 2) == KERNEL_IPC_INVALID);
    assert(kernel_ipc_send_check(&table, pid_c, handle_k, 2) == KERNEL_IPC_INVALID);
    assert(kernel_ipc_send(&table, pid_b, handle_a, (const unsigned char *)"hi", 2, &wake) == KERNEL_IPC_INVALID);
    assert(kernel_ipc_peek(&table, pid_c, handle_b, buf, sizeof(buf)) == KERNEL_IPC_INVALID);
    assert(!kernel_ipc_close_handle(&table, pid_c, handle_a, &wake));
    assert(!kernel_ipc_close_handle(&table, pid_c, handle_k, &wake));

    // Kernel<->user bridge channel works in both directions.
    assert(kernel_ipc_wait_register(&table, pid_c, handle_u, 12U) == KERNEL_IPC_WOULD_BLOCK);
    assert(kernel_ipc_send(&table, KERNEL_PID_KERNEL, handle_k, (const unsigned char *)"\x01\x01", 2, &wake) == 2);
    assert(wake == 12U);
    assert(kernel_ipc_receive_ready(&table, pid_c, handle_u) == 0);
    assert(kernel_ipc_peek(&table, pid_c, handle_u, buf, 2) == 2);
    assert(buf[0] == 1 && buf[1] == 1);
    assert(kernel_ipc_consume(&table, pid_c, handle_u) == 0);
    assert(kernel_ipc_send(&table, pid_c, handle_u, (const unsigned char *)"\x01\x00", 2, &wake) == 2);
    assert(kernel_ipc_peek(&table, KERNEL_PID_KERNEL, handle_k, buf, 2) == 2);
    assert(buf[0] == 1 && buf[1] == 0);
    assert(kernel_ipc_consume(&table, KERNEL_PID_KERNEL, handle_k) == 0);

    // Write & IRQ accounting.
    assert(kernel_process_record_write(&table, pid_a, 19));
    assert(kernel_process_record_irq(&table, pid_a));
    KERNEL_PROCESS_INFO info = {0};
    assert(kernel_process_info(&table, pid_a, &info));
    assert(info.WrittenBytes == 19 && info.Writes == 1 && info.Irqs == 1);

    assert(kernel_process_table_destroy(&table));
}

// ====== test_fault_cleanup_and_generation_reuse ======

static void test_fault_cleanup_and_generation_reuse(KERNEL_HEAP *heap)
{
    KERNEL_PROCESS_TABLE table = {0};
    uint64_t pid_a = 0, pid_b = 0, pid_c = 0;
    uint64_t ha1 = 0, hb = 0, ha2 = 0, hc = 0, ha3 = 0, hk = 0;
    uint32_t wakes[8] = {0};
    uint32_t wake_count = 0;
    uint32_t wake = KERNEL_TASK_NONE;

    assert(kernel_process_table_init(&table, heap, 4, 4));
    assert(kernel_process_create(&table, 0, 1, &pid_a));
    assert(kernel_process_create(&table, 0, 2, &pid_b));
    assert(kernel_process_create(&table, 0, 3, &pid_c));
    assert(kernel_process_set_state(&table, pid_a, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));
    assert(kernel_process_set_state(&table, pid_b, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));
    assert(kernel_process_set_state(&table, pid_c, KERNEL_PROC_CREATED, KERNEL_PROC_RUNNABLE));

    assert(kernel_ipc_channel_create(&table, pid_a, pid_b, &ha1, &hb));
    assert(kernel_ipc_channel_create(&table, pid_a, pid_c, &ha2, &hc));
    assert(kernel_ipc_channel_create(&table, pid_a, KERNEL_PID_KERNEL, &ha3, &hk));

    // Park B and C waiting for A.
    assert(kernel_ipc_wait_register(&table, pid_b, hb, 21U) == KERNEL_IPC_WOULD_BLOCK);
    assert(kernel_ipc_wait_register(&table, pid_c, hc, 22U) == KERNEL_IPC_WOULD_BLOCK);
    assert(kernel_process_set_state(&table, pid_b, KERNEL_PROC_RUNNABLE, KERNEL_PROC_BLOCKED));
    assert(kernel_process_set_state(&table, pid_c, KERNEL_PROC_RUNNABLE, KERNEL_PROC_BLOCKED));

    // A faults: both B and C must be woken and observe PEER_CLOSED.
    assert(kernel_process_terminate(&table, pid_a, KERNEL_PROC_EXIT_FAULT, 0,
        14ULL, 4ULL, 0x400123ULL, 0xdeadbeefULL, wakes, 8, &wake_count));
    assert(wake_count == 2U);
    assert((wakes[0] == 21U && wakes[1] == 22U) || (wakes[0] == 22U && wakes[1] == 21U));

    // Double terminate must fail.
    assert(!kernel_process_terminate(&table, pid_a, KERNEL_PROC_EXIT_KILLED, 0,
        0, 0, 0, 0, wakes, 8, &wake_count));

    // Peers observe PEER_CLOSED immediately.
    assert(kernel_ipc_receive_ready(&table, pid_b, hb) == KERNEL_IPC_PEER_CLOSED);
    assert(kernel_ipc_receive_ready(&table, pid_c, hc) == KERNEL_IPC_PEER_CLOSED);
    assert(kernel_ipc_receive_ready(&table, KERNEL_PID_KERNEL, hk) == KERNEL_IPC_PEER_CLOSED);

    // Closing remaining endpoints finishes and reclaims all 3 channel slots.
    assert(kernel_ipc_close_handle(&table, pid_b, hb, &wake));
    assert(kernel_ipc_close_handle(&table, pid_c, hc, &wake));
    assert(kernel_ipc_close_handle(&table, KERNEL_PID_KERNEL, hk, &wake));

    KERNEL_PROCESS_STATS stats = {0};
    assert(kernel_process_table_stats(&table, &stats));
    assert(stats.ChannelCount == 0U);
    assert(stats.ZombieProcesses == 1U);

    // Reap A and verify fault fields.
    KERNEL_PROCESS_INFO info = {0};
    assert(kernel_process_reap(&table, pid_a, &info));
    assert(info.ExitKind == KERNEL_PROC_EXIT_FAULT);
    assert(info.FaultVector == 14ULL);
    assert(info.FaultError == 4ULL);
    assert(info.FaultRip == 0x400123ULL);
    assert(info.FaultCr2 == 0xdeadbeefULL);
    assert(!kernel_process_info(&table, pid_a, &info));

    // Opening a new channel reuses slot 0 with a new generation; old handle hb is rejected.
    uint64_t hb_new = 0, hc_new = 0;
    assert(kernel_ipc_channel_create(&table, pid_b, pid_c, &hb_new, &hc_new));
    assert(hb_new != ha1 && hb_new != hb);
    assert(kernel_ipc_receive_ready(&table, pid_b, hb) == KERNEL_IPC_INVALID);
    assert(kernel_ipc_receive_ready(&table, pid_b, hb_new) == KERNEL_IPC_WOULD_BLOCK);

    assert(kernel_process_table_destroy(&table));
}

// ====== main ======

int main(void)
{
    KERNEL_LOCK_WAIT wait = { { host_clock_read, NULL, UINT32_MAX, 1000U }, 1000U, 10000U };
    assert(kernel_lock_context_initialize(&host_lock_context, &wait));

    KERNEL_HEAP heap;
    void *backing = NULL;
    setup_heap(&heap, &backing);
    KERNEL_HEAP_STATS before = kernel_heap_stats(&heap);

    test_dynamic_growth_and_loopback(&heap);
    test_state_transitions_and_isolation(&heap);
    test_fault_cleanup_and_generation_reuse(&heap);

    KERNEL_HEAP_STATS after = kernel_heap_stats(&heap);
    assert(after.Objects == 0);
    assert(after.RunPages == 0);
    assert(after.Rejected == 0);
    assert(after.FreePages == before.FreePages);

    _aligned_free(backing);
    printf("PASS: process_table_host (dynamic growth, isolation, IPC wakeup, fault cleanup, zero heap leaks)\n");
    return 0;
}
