#ifndef X86_CONSOLE_RUNTIME_H
#define X86_CONSOLE_RUNTIME_H
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "kernel/console/video/service/console.h"
#include "kernel/console/video/requests/queue.h"
#include "kernel/console/video/measurement/budget.h"
#include "kernel/console/video/pointer/state.h"
#include "kernel/drivers/video/display.h"
#include "lib/geometry/damage.h"

// G6 driver object: the interactive console of one input session. The input session owns the
// instance (X86_PS2_SESSION.Console) and every entry point reaches it from there.
// Context: open and finish run in the session setup and teardown; edit and submit in task 0
// (consumer) and the shell output in task 2, which only queue text and wake task 1; flush,
// pending and audit in task 1 (worker); mouse in task 3. The tasks are cooperative on one CPU and
// only task 1 draws. Text requests use the K4-C5 queue; mouse remains BSP-only.
typedef struct X86_CONSOLE {
    uint32_t                    Lock;
    X86_PS2_SESSION             *Source;
    // G6 O3: the presenting display (VirtIO GPU session) or none (GOP).
    KERNEL_DISPLAY              Display;
    KERNEL_CONSOLE_SERVICE      Service;
    KERNEL_CONSOLE_REQUEST_QUEUE Requests;
    uint32_t                    RequestsClosed;
    KERNEL_VIDEO_CONSOLE        View;
    KERNEL_CONSOLE_RENDER       Render;
    KERNEL_CONSOLE_BATCH        Batch;
    KERNEL_CONSOLE_MEASUREMENT  Measured;
    uint32_t                    Absent;
    // Timing of flushes in PM ticks (3.579545 MHz), reported when the session closes. Idle
    // flushes found nothing to draw; settle is the time spent in the serial pointer record.
    uint64_t                    FlushCount, FlushTotal, SettleTotal, IdleCount, IdleTotal;
    uint32_t                    FlushMax;
    // G5 V1-A: idle audits of the paging inventory, at most one per second.
    uint64_t                    AuditCount, AuditTotal;
    uint32_t                    AuditMax, AuditLast, AuditStarted, Tampered;
    // V2c Z2-A: cells drawn since the last present, one envelope (lib/geometry, Limit 1).
    OS_DAMAGE                   Damage;
    uint32_t                    PointerX, PointerY, PointerSeen;
    KERNEL_CONSOLE_POINTER      Pointer;
    uint64_t                    PointerFrame, PointerCells;
    uint32_t                    PointerVisibility, PointerPending;
} X86_CONSOLE;

// The session's Console must point at a zeroed object; display Ops 0 draws on the GOP directly.
int x86_console_open(X86_PS2_SESSION *session, KERNEL_DISPLAY display);
int x86_console_edit(X86_PS2_SESSION *session);
int x86_console_mouse(X86_PS2_SESSION *session, int32_t dx, int32_t dy, uint32_t buttons);
int x86_console_submit(X86_PS2_SESSION *session);
int x86_console_flush(X86_PS2_SESSION *session);
int x86_console_pending(X86_PS2_SESSION *session);
// G5: idle audit of the paging inventory (at most once per second); 0 when it failed.
int x86_console_audit(X86_PS2_SESSION *session);
int x86_console_finish(X86_PS2_SESSION *session);
int x86_console_requests_send(X86_CONSOLE *self, uint32_t operation,
    const void *text, uint32_t length, uint32_t capacity);
int x86_console_requests_step(X86_CONSOLE *self);
int x86_console_requests_pending(X86_CONSOLE *self);
#endif
