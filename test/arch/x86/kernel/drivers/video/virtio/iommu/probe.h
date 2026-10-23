#ifndef X86_VGPU_IOMMU_PROBE_H
#define X86_VGPU_IOMMU_PROBE_H
#include "arch/x86/kernel/drivers/video/virtio/mapping.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/activation/activation.h"

// Diagnostic V1: the VirtIO GPU under VT-d translation (VIRTIO_F_ACCESS_PLATFORM). Domain
// buffers: 0 the four queue pages (read/write), 1 the surface backing (read only).
#define X86_VGPU_IOMMU_QUEUE   0U
#define X86_VGPU_IOMMU_SURFACE 1U

// Reads only (ECAM, topology, DMAR, GPU route): no allocation, before any lease exists.
int x86_vgpu_iommu_route(X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory);
// Needs an idle page window: the queue lease is parked by the caller.
int x86_vgpu_iommu_enable(const KERNEL_PHYSICAL_LEASE *queue, const KERNEL_PHYSICAL_LEASE *surface);
BOOT_U64 x86_vgpu_iommu_device(BOOT_U64 physical, BOOT_U64 bytes, BOOT_U32 access);
int x86_vgpu_iommu_revoke(BOOT_U32 index);
int x86_vgpu_iommu_expect(BOOT_U32 reason, BOOT_U32 read, BOOT_U64 address);
int x86_vgpu_iommu_quiet(void);
// After the device reset is confirmed and with an idle window: TE off, flush, release.
int x86_vgpu_iommu_disable(void);
#endif
