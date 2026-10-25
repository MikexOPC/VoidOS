#ifndef X86_VGPU_PRESENT_H
#define X86_VGPU_PRESENT_H
#include "../domain.h"
#include "arch/x86/kernel/drivers/video/virtio/resources/mapping.h"
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
#include "arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#include "arch/x86/mm/dma/arena.h"
#include "kernel/drivers/video/virtio/resources/scanout.h"
#include "kernel/diagnostics/latency.h"
#include "lib/geometry/damage.h"
#include "kernel/time/clock.h"

// V2c console presentation through the translated VirtIO GPU (Z1-A..Z5-A), asynchronous since
// R1-A. G6 driver object: the session owns the instance and passes it to every call.
// Context: open and close run in the session start and stop (IF=0, before and after the session
// tasks); every other call runs in the console task only. One owner, so no lock today.
typedef struct {
    // G6: reserved for the K2 lock; 0 today, the console task is the only user (see above).
    BOOT_U32                    Lock;
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
    // CPU view of the surface (X86_SURFACE_VIRTUAL), cleared by the R3-A recovery.
    volatile uint32_t           *Pixels;
    BOOT_U32                    Phase, SlowMarked, SubmittedAt;
    BOOT_U32                    FlyX, FlyY, FlyW, FlyH;
    OS_DAMAGE                   Damage;
    BOOT_U32                    Recoverable, Recoveries;
    // G4: the transport PM clock through kernel/time; Recover measures the spacing of R3-A.
    KERNEL_CLOCK                Clock;
    KERNEL_STOPWATCH            Recover;
    BOOT_U64                    Slows, Timeouts, Recovered;
    // Test-only (KERNEL_TEST_VGPU_PRESENT_STALL): the observation of a completed TRANSFER is held.
    BOOT_U32                    StallHold;
} X86_VGPU_PRESENT;

// Z5-A: inside the V2b start, after TE=1. Returns 0 only when the GPU reset could not be confirmed.
int x86_vgpu_present_open(X86_VGPU_PRESENT *self, X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory,
    X86_DMA_ARENA *arena, X86_SURFACE_MAPPING *surface);
int x86_vgpu_present_rect(X86_VGPU_PRESENT *self, BOOT_U32 x, BOOT_U32 y, BOOT_U32 width, BOOT_U32 height);
// R1-A/R3-A: progress of the update in flight or the recovery; busy keeps the console flushing.
int x86_vgpu_present_poll(X86_VGPU_PRESENT *self);
int x86_vgpu_present_busy(X86_VGPU_PRESENT *self);
// 1 once after the target changed (bring-up or fallback): the console must redraw every cell.
int x86_vgpu_present_redraw(X86_VGPU_PRESENT *self);
int x86_vgpu_present_abandon(X86_VGPU_PRESENT *self);
int x86_vgpu_present_close(X86_VGPU_PRESENT *self);
#endif
