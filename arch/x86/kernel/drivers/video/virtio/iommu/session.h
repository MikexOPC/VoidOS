#ifndef X86_VGPU_IOMMU_SESSION_H
#define X86_VGPU_IOMMU_SESSION_H
#include "present/present.h"
#include "kernel/mm/physical/holdings.h"
#include "kernel/drivers/video/display.h"

// V2b: persistent VT-d translation of the VirtIO GPU for the interactive session (Y1-A..Y4-A).
// States: OFF (no GPU, no DMAR or RMRR present: the session runs as before), TRANSLATING,
// COMPROMISED (an owned GPU fault was seen: the GPU must not be used), RETAINED (teardown
// uncertain: nothing is released).
#define X86_VGPU_SESSION_OFF         0U
#define X86_VGPU_SESSION_TRANSLATING 1U
#define X86_VGPU_SESSION_COMPROMISED 2U
#define X86_VGPU_SESSION_RETAINED    3U

// Geometry A: the surface is bound before GET_DISPLAY_INFO (the GPU domain maps it), so it is
// sized for the larger of the GOP and this capacity; a display that does not fit keeps GOP.
// 1920x1080x4 = 7.9 MiB, inside the 16 MiB single-entry backing limit.
#define X86_VGPU_SURFACE_WIDTH  1920U
#define X86_VGPU_SURFACE_HEIGHT 1080U

// G6 driver object. The VirtIO integration owns the one instance (x86_vgpu_kernel_session).
// Context: start and stop run before and after the session tasks; poll and the display operations
// run in the console task only. One owner, so no lock today; Lock is reserved for K2.
typedef struct {
    BOOT_U32                    Lock;
    BOOT_U32                    State;
    BOOT_U32                    DirectDma;
    BOOT_U32                    Reserved;
    X86_DMA_ARENA               Arena;
    X86_SURFACE_MAPPING         Surface;
    BOOT_U64                    Budget, Polls, Held;
    KERNEL_HOLDING              Holding;
    // V2d: fault scan per flush, and the IF=0 sections of start and stop (one sample each).
    KERNEL_LATENCY              PollTicks, QuietTicks;
    // ACPI 6.5 4.8.3.3 PM timer, kept after enable so the stop section is timed after the QI is gone.
    BOOT_U32                    ClockPort, ClockMask;
    X86_VGPU_PRESENT            Present;
} X86_VGPU_SESSION;

// Before the session tasks run. A failure before translation releases everything and leaves OFF.
int x86_vgpu_session_start(X86_VGPU_SESSION *self, X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory);
// Console task, at every flush: register-only fault scan, no window borrow (Y3-A).
int x86_vgpu_session_poll(X86_VGPU_SESSION *self);
// After the session tasks stopped: TE off, invalidation, release of domain, arena and surface.
int x86_vgpu_session_stop(X86_VGPU_SESSION *self, X86_VGPU_MAPPING *gpu);
BOOT_U32 x86_vgpu_session_state(const X86_VGPU_SESSION *self);
// Pages the session holds (arena, surface, VT-d tables, queue, mapping) for the console accounting.
BOOT_U64 x86_vgpu_session_pages(const X86_VGPU_SESSION *self);
// G6 O3: the console draws through this interface (fault scan, then the present operations).
KERNEL_DISPLAY x86_vgpu_session_display(X86_VGPU_SESSION *self);
#endif
