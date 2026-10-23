#ifndef X86_VGPU_IOMMU_SESSION_H
#define X86_VGPU_IOMMU_SESSION_H
#include "domain.h"

// V2b: persistent VT-d translation of the VirtIO GPU for the interactive session (Y1-A..Y4-A).
// States: OFF (no GPU, no DMAR or RMRR present: the session runs as before), TRANSLATING,
// COMPROMISED (an owned GPU fault was seen: the GPU must not be used), RETAINED (teardown
// uncertain: nothing is released).
#define X86_VGPU_SESSION_OFF         0U
#define X86_VGPU_SESSION_TRANSLATING 1U
#define X86_VGPU_SESSION_COMPROMISED 2U
#define X86_VGPU_SESSION_RETAINED    3U

// Before the session tasks run. A failure before translation releases everything and leaves OFF.
int x86_vgpu_session_start(const KERNEL_ACPI_DIRECTORY *directory);
// Console task, at every flush: register-only fault scan, no window borrow (Y3-A).
int x86_vgpu_session_poll(void);
// After the session tasks stopped: TE off, invalidation, release of domain, arena and surface.
int x86_vgpu_session_stop(void);
BOOT_U32 x86_vgpu_session_state(void);
// Pages the session holds (arena, surface, VT-d tables, queue, mapping) for the console accounting.
BOOT_U64 x86_vgpu_session_pages(void);
#endif
