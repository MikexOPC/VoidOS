#include "channel.h"
#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_AP_NOTIFY
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"
#endif

KCA_RECORD kca_record;
static KERNEL_CONSOLE_REQUEST_QUEUE queue;
static KERNEL_CONSOLE_SERVICE service;
static KERNEL_VIDEO_CONSOLE view;

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
    if (xis_native_index() != 1 || index >= 2 || !xat_task_valid() ||
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
                ((now - start) & xp_panic_native.Source.Mask) >= 2 * KERNEL_PM_TIMER_HZ) xat_abort();
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

// ====== kca_pump ======

void kca_pump(void) {
    if (xis_native_index()) xat_abort();
    if (!kca_record.Self) return;
#ifdef KERNEL_TEST_AP_NOTIFY
    if (!xcn_poll()) xat_abort();
#endif
    if (!ks_atomic_load_acquire(&kca_record.Full[0]) || !ks_atomic_load_acquire(&kca_record.Full[1])) return;
    for (uint32_t count = 0; count < 8; ++count) {
        KERNEL_CONSOLE_REQUEST packet;
        uint32_t result = kernel_console_requests_take(&queue,&packet), producer, sequence = 0;
        if (result == KCR_EMPTY) return;
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
}

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
    int closed = xcn_close(1);
    if (!kernel_text("KERNEL_AP_NOTIFY_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xcn_record) ||
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
