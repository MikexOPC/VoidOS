#ifndef X86_VGPU_IOMMU_SESSION_H
#define X86_VGPU_IOMMU_SESSION_H
#include "domain.h"
#include "arch/x86/kernel/drivers/video/virtio/resources/mapping.h"
#include "arch/x86/mm/dma/arena.h"

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

// Before the session tasks run. A failure before translation releases everything and leaves OFF.
int x86_vgpu_session_start(const KERNEL_ACPI_DIRECTORY *directory);
// Console task, at every flush: register-only fault scan, no window borrow (Y3-A).
int x86_vgpu_session_poll(void);
// After the session tasks stopped: TE off, invalidation, release of domain, arena and surface.
int x86_vgpu_session_stop(void);
BOOT_U32 x86_vgpu_session_state(void);
// Pages the session holds (arena, surface, VT-d tables, queue, mapping) for the console accounting.
BOOT_U64 x86_vgpu_session_pages(void);

// V2c (present.c): the console drawn through the translated GPU, GOP fallback (Z1-A..Z5-A).
int x86_vgpu_present_open(X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory,
    X86_DMA_ARENA *arena, X86_SURFACE_MAPPING *surface);
int x86_vgpu_present_rect(BOOT_U32 x, BOOT_U32 y, BOOT_U32 width, BOOT_U32 height);
// R1-A/R3-A: progress of the update in flight or the recovery; busy keeps the console flushing.
int x86_vgpu_present_poll(void);
int x86_vgpu_present_busy(void);
// 1 once after the target changed (bring-up or fallback): the console must redraw every cell.
int x86_vgpu_present_redraw(void);
int x86_vgpu_present_abandon(void);
int x86_vgpu_present_close(void);
#endif
