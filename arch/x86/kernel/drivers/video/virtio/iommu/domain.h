#ifndef X86_VGPU_IOMMU_DOMAIN_H
#define X86_VGPU_IOMMU_DOMAIN_H
#include "arch/x86/kernel/drivers/video/virtio/mapping.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/activation/activation.h"

// The VirtIO GPU under VT-d translation (VIRTIO_F_ACCESS_PLATFORM): V1 diagnostic and the V2
// persistent session share this owner. Domain buffers: 0 the four queue pages (read/write),
// 1 the surface backing (read only). One GPU domain at a time.
#define X86_VGPU_IOMMU_QUEUE   0U
#define X86_VGPU_IOMMU_SURFACE 1U

// Reads only (ECAM, topology, DMAR, GPU route): no allocation, before any lease exists.
// X86_VGPU_IOMMU_ABSENT: no DMAR or a DMAR with RMRR (firmware-owned DMA would break, Y2-A).
#define X86_VGPU_IOMMU_FAILED 0
#define X86_VGPU_IOMMU_READY  1
#define X86_VGPU_IOMMU_ABSENT 2
int x86_vgpu_iommu_route(X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory);
// Needs an idle page window: the queue lease is parked by the caller.
int x86_vgpu_iommu_enable(const KERNEL_PHYSICAL_LEASE *queue, const KERNEL_PHYSICAL_LEASE *surface);
BOOT_U64 x86_vgpu_iommu_device(BOOT_U64 physical, BOOT_U64 bytes, BOOT_U32 access);
int x86_vgpu_iommu_revoke(BOOT_U32 index);
int x86_vgpu_iommu_expect(BOOT_U32 reason, BOOT_U32 read, BOOT_U64 address);
int x86_vgpu_iommu_quiet(void);
// V2d: the ACPI PM timer captured for VT-d (port and counter mask); 0 before the capture.
int x86_vgpu_iommu_clock(BOOT_U32 *port, BOOT_U32 *mask);
// After the device reset is confirmed and with an idle window: TE off, flush, release.
int x86_vgpu_iommu_disable(void);
#endif
