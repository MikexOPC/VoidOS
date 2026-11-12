#include "resident.h"
#include "slot.h"
#include "kernel/diagnostics/log.h"
#include "kernel/mm/physical/holdings.h"

static X86_PROCESS_SLOT slot;
static IPC_CHANNEL channel;
static KERNEL_HOLDING holding;
static BOOT_U64 endpoint;
static int failed;
static X86_PAGE_WINDOW *owner_window;
static X86_LAPIC_TIMER *owner_timer;

// ====== release ======

static void release(void)
{
    if (!x86_process_slot_reap(&slot)) kernel_panic("PROCESS_RESIDENT_REAP_FAILED");
    if (holding.Self && !kernel_holdings_unregister(&holding))
        kernel_panic("PROCESS_RESIDENT_ACCOUNTING_FAILED");
    if (endpoint) x86_ipc_finish(&channel);
    endpoint = 0;
    failed = 0;
    owner_window = 0;
    owner_timer = 0;
}

// ====== restore ======

static void restore(void)
{
    if (!x86_process_close()) kernel_panic("PROCESS_RESIDENT_RESTORE_FAILED");
}

// ====== x86_process_resident_open ======

int x86_process_resident_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer,
    const X86_PROCESS_IMAGE *image)
{
    if (endpoint || !x86_process_open(window, timer)) return 0;
    if (x86_process_slot_prepare(&slot, image)) {
        endpoint = x86_ipc_open(&channel);
        if (endpoint && kernel_holdings_register(&holding, slot.Lease.Pages)) {
            owner_window = window;
            owner_timer = timer;
        } else release();
    }
    restore();
    return endpoint != 0;
}

// ====== x86_process_resident_request ======

int x86_process_resident_request(const unsigned char request[2], unsigned char response[2],
    BOOT_U32 input_mask, X86_PROCESS_RESULT *result, int retain_on_failure)
{
    unsigned char message[IPC_MESSAGE_BYTES] = {0};
    if (!endpoint || failed || !request || !response || !result ||
        !x86_process_open(owner_window, owner_timer)) return 0;
    if (!x86_process_input_irqs(input_mask)) {
        restore();
        return 0;
    }
    *result = (X86_PROCESS_RESULT){0};
    message[0] = request[0];
    message[1] = request[1];
    int passed = x86_ipc_send(&channel, endpoint, endpoint, message, 2) == 2 &&
        x86_process_slot_step(&slot, result, &channel, endpoint | 1U, 1) &&
        !result->Kind && slot.Waiting &&
        x86_ipc_peek(&channel, endpoint, endpoint, message, 2) == 2 &&
        !x86_ipc_consume(&channel, endpoint, endpoint) &&
        x86_ipc_receive_ready(&channel, endpoint, endpoint) == IPC_WOULD_BLOCK &&
        x86_ipc_receive_ready(&channel, endpoint | 1U, endpoint | 1U) == IPC_WOULD_BLOCK;
    if (!passed && retain_on_failure) {
        x86_ipc_close(&channel);
        failed = 1;
    } else if (!passed) release();
    restore();
    if (passed) {
        response[0] = message[0];
        response[1] = message[1];
    }
    return passed;
}

// ====== x86_process_resident_close ======

int x86_process_resident_close(void)
{
    if (!endpoint || !x86_process_open(owner_window, owner_timer)) return 0;
    release();
    restore();
    return 1;
}
