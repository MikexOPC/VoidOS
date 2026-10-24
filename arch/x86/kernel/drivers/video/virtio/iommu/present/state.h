#ifndef X86_VGPU_PRESENT_STATE_H
#define X86_VGPU_PRESENT_STATE_H
#include "../session.h"
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
#include "arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#include "kernel/drivers/video/virtio/resources/scanout.h"
#include "kernel/diagnostics/log.h"
#include "kernel/diagnostics/latency.h"
#include "lib/geometry/damage.h"

// V2c console presentation through the translated VirtIO GPU (Z1-A..Z5-A), asynchronous since
// R1-A. One instance today; the state is explicit so a driver object can own it later (G6).
//
// R1-A: one update in flight. Phase 1 TRANSFER posted, phase 2 FLUSH posted; damage that arrives
// meanwhile is merged (lib/geometry, Limit 1) and presented next. R2: SLOW mark past 100 ms,
// fallback at 1 s. R3-A: after a timeout fallback, at most 3 bring-ups, 5 s apart.
#define PRESENT_SLOW_PM     357955U
#define PRESENT_LIMIT_PM    3579545U
#define RECOVER_SPACING_PM  (5ULL * 3579545U)
#define RECOVER_TRIES       3U

typedef struct {
    X86_VGPU_TRANSPORT          Transport;
    KERNEL_VGPU_IO              Io;
    X86_VGPU_QUEUE_TRANSPORT    Native;
    VGPU_QUEUE                  Queue;
    VGPU_RESOURCE               Resource;
    X86_FB_TARGET               Target;
    const X86_SURFACE_MAPPING   *Bound;
    BOOT_U64                    Presents;
    BOOT_U32                    Presenting, Redraw, Failures, ResetDone;
    // V2d M1-A: whole present, the FLUSH completion wait alone and the presented area (PM ticks).
    KERNEL_LATENCY              PresentTicks, FlushTicks, PresentArea;
    // R3-A: what the bring-up needs again after a reset inside the session tasks. The transport
    // and the native queue stay open (queue open would recapture the inventory).
    X86_VGPU_QUEUE_TRANSPORT    NativeSaved;
    VGPU_QUEUE_IO_CONTEXT       AccessSaved;
    VGPU_BUFFERS                BuffersSaved;
    BOOT_U64                    BackingSaved;
    X86_SURFACE_MAPPING         *SurfaceSaved;
    BOOT_U32                    Phase, SlowMarked, SubmittedAt;
    BOOT_U32                    FlyX, FlyY, FlyW, FlyH;
    OS_DAMAGE                   Damage;
    BOOT_U32                    Recoverable, Recoveries, RecoverLast;
    BOOT_U64                    RecoverElapsed, Slows, Timeouts, Recovered;
    // Test-only (KERNEL_TEST_VGPU_PRESENT_STALL): the observation of a completed TRANSFER is held.
    BOOT_U32                    StallHold;
} X86_VGPU_PRESENT;

extern X86_VGPU_PRESENT x86_vgpu_present_state;

// lifecycle.c
int x86_vgpu_present_fallback(const char *reason);
const char *x86_vgpu_present_bringup(void);
// update.c
int x86_vgpu_present_fault(void);
int x86_vgpu_present_pump(void);
#endif
