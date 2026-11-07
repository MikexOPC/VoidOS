#include "channel.h"
#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_AP_NOTIFY
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "arch/x86/mm/arena/objects.h"
#endif

#ifdef KERNEL_TEST_AP_NOTIFY
#define KCA_PRODUCE_LIMIT (4U * KERNEL_PM_TIMER_HZ)
#else
#define KCA_PRODUCE_LIMIT (2U * KERNEL_PM_TIMER_HZ)
#endif
KCA_RECORD kca_record;
static KERNEL_CONSOLE_REQUEST_QUEUE queue;
static KERNEL_CONSOLE_SERVICE service;
static KERNEL_VIDEO_CONSOLE view;
#ifdef KERNEL_TEST_AP_NOTIFY
static X86_WATCHDOG_ENV watchdog_env;
#endif

// ====== kernel_console_requests_fail_stop ======

__declspec(noreturn) void kernel_console_requests_fail_stop(void) { xat_abort(); }

// ====== kca_prepare ======

void kca_prepare(void) {
    if (xis_native_index() || kca_record.Self || !kernel_console_requests_initialize(&queue) ||
        !kernel_console_service_init(&service, &view, 40, 8)) xat_abort();
    kca_record.Self = (uint64_t)(uintptr_t)&kca_record;
    kca_record.Root = kca_record.Token = xat_record.Root;
    kca_record.Budget = xat_record.Budget;
    kca_record.Held = xat_record.Held;
    if (!kernel_console_ap_channel(0,0,kca_record.Token,0)) xat_abort();
#ifdef KERNEL_TEST_AP_NOTIFY
    if (!xcn_prepare(kca_record.Token) || xcn_prepare(kca_record.Token) || xcn_send(0)) xat_abort();
#endif
}

// ====== kca_produce ======

void kca_produce(uint32_t index) {
    uint32_t start, now;
    uint64_t token = xat_record.Root;
    if (xis_native_index() != 1 || index >= 2 || !xat_task_valid() || x86_scheduler_driver_task_valid() ||
        !kernel_console_ap_channel(1,1,token,index) || !xis_native_now(NULL,&start)) xat_abort();
    for (uint32_t sequence = 0; sequence < 1000; ++sequence) {
        char text[8] = {'P',(char)('0' + index),':',
            (char)('0' + sequence / 1000), (char)('0' + sequence / 100 % 10),
            (char)('0' + sequence / 10 % 10), (char)('0' + sequence % 10),'\n'};
        uint32_t accepted = 0;
        for (uint32_t retry = 0; retry < 65536; ++retry) {
            uint32_t result = kernel_console_requests_push(&queue,KCR_WRITE,text,sizeof(text),sizeof(text));
            if (result == KCR_OK) { accepted = 1; break; }
            if (result != KCR_FULL || !xis_native_now(NULL,&now) ||
                ((now - start) & xp_panic_native.Source.Mask) >= KCA_PRODUCE_LIMIT) xat_abort();
            ks_atomic_store_release(&kca_record.Full[index],1);
            xat_io_yield();
        }
        if (!accepted) xat_abort();
        ++kca_record.Sent[index];
#ifdef KERNEL_TEST_AP_NOTIFY
        if (!xcn_send(index)) xat_abort();
#endif
        if (!(sequence % 16)) xat_io_yield();
    }
#ifdef KERNEL_TEST_AP_CONSOLE_INCOMPLETE
    if (index == 1) return;
#endif
    if (!kernel_console_ap_channel(2,1,token,index)) xat_abort();
}

// ====== consume_private ======

static uint32_t consume_private(uint32_t limit) {
    uint32_t count = 0;
    for (; count < limit; ++count) {
        KERNEL_CONSOLE_REQUEST packet;
        uint32_t result = kernel_console_requests_take(&queue,&packet), producer, sequence = 0;
        if (result == KCR_EMPTY) return count;
        if (result != KCR_OK || packet.Operation != KCR_WRITE || packet.Length != 8 ||
            packet.Text[0] != 'P' || packet.Text[1] < '0' || packet.Text[1] > '1' ||
            packet.Text[2] != ':' || packet.Text[7] != '\n') xat_abort();
        producer = packet.Text[1] - '0';
        for (uint32_t digit = 3; digit < 7; ++digit) {
            if (packet.Text[digit] < '0' || packet.Text[digit] > '9') xat_abort();
            sequence = sequence * 10 + packet.Text[digit] - '0';
        }
        if (sequence != kca_record.Consumed[producer] || sequence >= 1000 ||
            !kernel_console_service_write(&service,(const char *)packet.Text,packet.Length,sizeof(packet.Text)) ||
            kernel_console_requests_complete(&queue,1) != KCR_OK) xat_abort();
        ++kca_record.Consumed[producer];
    }
    return count;
}

// ====== kca_pump ======

void kca_pump(void) {
    if (xis_native_index()) xat_abort();
#ifndef KERNEL_TEST_AP_NOTIFY
    if (!kca_record.Self || !ks_atomic_load_acquire(&kca_record.Full[0]) ||
        !ks_atomic_load_acquire(&kca_record.Full[1])) return;
    (void)consume_private(8);
#endif
}

#ifdef KERNEL_TEST_AP_NOTIFY
KCW_RECORD kcw_record;
static X86_DISPATCH_STATS worker_stats;
#define KCW_TASK XAT_TASKS

// ====== kca_watchdog ======

void kca_watchdog(int open) {
    if (xis_native_index() || ks_irq_state()) xat_abort();
    if (!open) {
        if (!x86_watchdog_close()) xat_abort();
        kcw_record.Closed = 1;
        return;
    }
    if (!x86_watchdog_open(&watchdog_env)) xat_abort();
}

// ====== kca_capture ======

void kca_capture(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging) {
    KERNEL_BOOTSTRAP_MEMORY *memory = startup->Memory;
    watchdog_env.Context = context; watchdog_env.Window = &memory->Window; watchdog_env.Inventory = paging;
    watchdog_env.Mapping = x86_uart_lapic_mapping(); watchdog_env.Profile = startup->ApicProfile;
    watchdog_env.Capabilities = startup->ApicCapabilities; watchdog_env.Description = startup->Hpet;
    watchdog_env.Topology = (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&memory->Arena,startup->Madt);
}

// ====== worker_private ======

static void worker_private(uint32_t index) {
    if (index != KCW_TASK || xis_native_index() || !x86_scheduler_task_valid() ||
        !x86_scheduler_driver_task_valid()) xat_abort();
    while (kca_record.Consumed[0] + kca_record.Consumed[1] < 2000) {
        KERNEL_EVENT_REPLY observed = x86_scheduler_event_request(X86_TASK_OBSERVE,XCN_EVENT,0);
        if (observed.outcome) xat_abort();
        if (consume_private(2000)) continue;
        KERNEL_EVENT_REPLY waited = x86_scheduler_event_request(X86_TASK_WAIT,XCN_EVENT,observed.epoch);
        ++kcw_record.Waits;
        if (waited.outcome == KERNEL_EVENT_WAITING) ++kcw_record.Woken;
        else if (waited.outcome == KERNEL_EVENT_CHANGED) ++kcw_record.Changed;
        else xat_abort();
    }
    kcw_record.Exited = 1;
}

// ====== idle_private ======

static int idle_private(void) {
    uint32_t slept, result;
    xcn_wake[0] = 0;
    ++kcw_record.Idles;
    slept = x86_watchdog_idle(xcn_wake);
    result = x86_watchdog_result();
    if (!slept || slept > X86_WATCHDOG_WAKE_LIMIT ||
        (result != X86_WATCHDOG_EVENT && result != X86_WATCHDOG_IDLE)) return 0;
    if (result == X86_WATCHDOG_EVENT) {
        ++kcw_record.IpiWakes;
        if (slept >= 3) ++kcw_record.IpiHalts;
    }
    kcw_record.Slept += slept;
    KERNEL_EVENT_REPLY drained = kernel_scheduler_irq_drain();
    if (drained.reply.status != KERNEL_SCHEDULER_OK) return 0;
    kcw_record.Drained += drained.woken;
    return 1;
}

// ====== retain_private ======

static int retain_private(const KERNEL_SCHEDULER_REPLY *task) {
    return task->index == KCW_TASK && task->state == KERNEL_TASK_FINISHED &&
        task->generation == kcw_record.Generation && kcw_record.Exited == 1;
}

// ====== kca_serve ======

void kca_serve(void) {
    uint32_t start, now;
    if (xis_native_index() || ks_irq_state() || !kca_record.Self || kcw_record.Self) xat_abort();
    kcw_record.Self = (uint64_t)(uintptr_t)&kcw_record;
    KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
    if (reply.status || reply.index != KCW_TASK || reply.state != KERNEL_TASK_RESERVED || !reply.generation) xat_abort();
    kcw_record.Generation = reply.generation;
    if (!x86_scheduler_task_assign(KCW_TASK,0,worker_private) ||
        kernel_scheduler_affinity_on(0,KCW_TASK,reply.generation,1) ||
        !x86_task_stack_acquire(&x86_task_stacks[KCW_TASK],xat_pool,xat_record.Root,KCW_TASK,reply.generation) ||
        !x86_scheduler_context_prepare(&x86_task_contexts[KCW_TASK],x86_task_stack_base(KCW_TASK),65536) ||
        kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_PUBLISH,KCW_TASK,reply.generation).state != KERNEL_TASK_READY ||
        !x86_watchdog_budget_begin_four(&x86_task_stacks[0].lease,&x86_task_stacks[1].lease,
            &x86_task_stacks[KCW_TASK].lease,NULL) ||
        !xis_native_now(NULL,&start)) xat_abort();
    while (!ks_atomic_load_acquire(&kca_record.Full[0]) || !ks_atomic_load_acquire(&kca_record.Full[1])) {
        if (!xis_native_now(NULL,&now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
            xat_abort();
        ks_pause();
    }
    X86_DISPATCH_CONFIG config = {1,65536,32768,1,x86_scheduler_dispatch_run,retain_private,
        x86_scheduler_dispatch_reply,x86_scheduler_dispatch_release,idle_private};
    if (!x86_scheduler_dispatch_retained_on(0,&config,&worker_stats) || worker_stats.completed != 1) xat_abort();
    kcw_record.Steps = worker_stats.steps;
    if (!x86_watchdog_budget_end()) xat_abort();
    if (!x86_task_stack_release(&x86_task_stacks[KCW_TASK],KCW_TASK,kcw_record.Generation) ||
        kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_COLLECT,KCW_TASK,kcw_record.Generation).status) xat_abort();
    kcw_record.Released = 1;
}
#endif

// ====== kca_release_allowed ======

int kca_release_allowed(void) {
    return !xis_native_index() && kca_record.Closed == 1 &&
#ifdef KERNEL_TEST_AP_NOTIFY
        xcn_release_allowed() &&
#endif
        kernel_console_ap_channel(3,0,kca_record.Token,0) == 7;
}

// ====== kca_report ======

void kca_report(void) {
    if (!kernel_text("KERNEL_AP_CONSOLE_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&kca_record) ||
        !kernel_text("\n")) xat_abort();
#ifdef KERNEL_TEST_AP_CONSOLE_INCOMPLETE
    if (!kernel_text("KERNEL_AP_BATCH_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_record) ||
        !kernel_text("\nKERNEL_AP_BATCH_PTE: ") || !kernel_number((uint64_t)(uintptr_t)&x86_stack_entries(3)[320]) ||
        !kernel_text("\n")) xat_abort();
#endif
}

// ====== kca_finish ======

void kca_finish(void) {
    KERNEL_CONSOLE_REQUEST_INFO info;
    if (xis_native_index() || xad_collect_allowed(1,0,1,xat_record.Root) != 1) xat_abort();
    kca_pump();
    kca_record.Completed = kernel_console_ap_channel(3,0,kca_record.Token,0);
    if (kca_record.Sent[0] != 1000 || kca_record.Sent[1] != 1000 ||
        kca_record.Consumed[0] != 1000 || kca_record.Consumed[1] != 1000 ||
        kernel_console_requests_query(&queue,&info) != KCR_OK || info.State != KCR_OPEN || info.Pending)
        xat_abort();
#ifdef KERNEL_TEST_AP_CONSOLE_INCOMPLETE
    if (kca_record.Completed != 1 || kernel_console_ap_channel(4,0,kca_record.Token,0) ||
        kca_release_allowed() || !kernel_physical_budget_matches(xat_pool,kca_record.Held)) xat_abort();
    kca_record.Denied = 1;
    kca_report();
    if (!kernel_log(1,"KERNEL_AP_CONSOLE_INCOMPLETE")) xat_abort();
    xat_cleanup();
    xat_abort();
#else
    if (kca_record.Completed != 3 || !kernel_console_ap_channel(4,0,kca_record.Token,0) ||
        kernel_console_requests_drain(&queue) != KCR_OK || kernel_console_requests_close(&queue) != KCR_OK ||
        !kernel_console_service_close(&service)) xat_abort();
    kca_record.Closed = 1;
#ifdef KERNEL_TEST_AP_NOTIFY
    int closed = xcn_close(1,(int)xad_collect_allowed(1,0,1,xcn_record.Token));

    if (!kernel_text("KERNEL_AP_NOTIFY_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xcn_record) ||
        !kernel_text("\nKERNEL_AP_WORKER_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&kcw_record) ||
        !kernel_text("\n")) xat_abort();
#if defined(KERNEL_TEST_AP_NOTIFY_NO_ACK) || defined(KERNEL_TEST_AP_NOTIFY_INFLIGHT)
    if (closed || xcn_release_allowed() || !xcn_record.Denied ||
        !kernel_physical_budget_matches(xat_pool,kca_record.Held)) xat_abort();
    kca_record.Denied = 1;
    kca_report();
    if (!kernel_text("KERNEL_AP_BATCH_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_record) ||
        !kernel_text("\nKERNEL_AP_BATCH_PTE: ") || !kernel_number((uint64_t)(uintptr_t)&x86_stack_entries(3)[320]) ||
        !kernel_text("\n") || !kernel_log(1,"KERNEL_AP_NOTIFY_INCOMPLETE")) xat_abort();
    xat_cleanup();
    xat_abort();
#else
    if (!closed) xat_abort();
#endif
#endif
#endif
}
