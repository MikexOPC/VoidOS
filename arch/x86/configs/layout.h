#ifndef OS_X86_LAYOUT_H
#define OS_X86_LAYOUT_H

// ====== K1: virtual layout of the kernel, the only place that names slots and windows ===== //
//
// Intel SDM Vol. 3A 4.5 (4-level paging): bits 47:39 of a linear address select one of the 512
// PML4 entries (one slot = 512 GiB); bits 63:48 must equal bit 47 (canonical form, 3A 3.3.7.1),
// so slots 256-511 are the upper half 0xFFFF8000_00000000 and above.
// K1 stage 5b: kernel regions are high; the loader identity branch is retired at startup
// (docs/design/consolidamento.md).

#define X86_LAYOUT_SLOT_SHIFT 39U
#define X86_LAYOUT_SLOT_BYTES (1ULL << X86_LAYOUT_SLOT_SHIFT)
#define X86_LAYOUT_SLOT_ADDRESS(slot) \
    (((unsigned long long)(slot) << X86_LAYOUT_SLOT_SHIFT) | ((slot) >= 256U ? 0xFFFF000000000000ULL : 0ULL))
// PML4 index of a canonical address (the mask drops the sign extension of the upper half).
#define X86_LAYOUT_SLOT_OF(address) ((unsigned)(((unsigned long long)(address) >> X86_LAYOUT_SLOT_SHIFT) & 511U))

// ====== kernel image ===== //

// Link base (/base: in kernel/scripts/build.ps1, checked by convert-image.ps1) and the code window
// that RIP, IDT, GDT and owners of the image are checked against. K1 stage 5: the last 2 GiB of
// the address space (slot 511, the classic kernel code model), window of 4 MiB (the image was
// 692 KiB of the former 1 MiB window).
#define X86_KERNEL_IMAGE_BASE   0xFFFFFFFF80000000ULL
#define X86_KERNEL_IMAGE_BYTES  0x0000000000400000ULL
#define X86_KERNEL_IMAGE_LIMIT  (X86_KERNEL_IMAGE_BASE + X86_KERNEL_IMAGE_BYTES)

// ====== slots ===== //

#define X86_LAYOUT_RECURSIVE_SLOT  510U
#define X86_LAYOUT_MAP_SLOT        479U  // boot memory map window
#define X86_LAYOUT_INFO_SLOT       480U  // boot contract
#define X86_LAYOUT_WINDOWS_SLOT    488U  // boot stack, page window, arena, LAPIC, task stacks
#define X86_LAYOUT_FB_SLOT         481U
#define X86_LAYOUT_GPU_SLOT        482U
#define X86_LAYOUT_SURFACE_SLOT    483U
#define X86_LAYOUT_IOMMU_SLOT      484U
#define X86_LAYOUT_DMA_SLOT        485U
#define X86_LAYOUT_HEAP_SLOT       486U
#define X86_LAYOUT_AP_SLOT         487U
#define X86_LAYOUT_GUI_BACKBUFFER_SLOT 489U

#define X86_LAYOUT_MAP_VIRTUAL        X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_MAP_SLOT)
#define X86_LAYOUT_INFO_VIRTUAL       X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_INFO_SLOT)
#define X86_LAYOUT_WINDOWS_VIRTUAL    X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_WINDOWS_SLOT)
#define X86_LAYOUT_RECURSIVE_VIRTUAL  X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_RECURSIVE_SLOT)
#define X86_LAYOUT_GUI_BACKBUFFER_VIRTUAL X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_GUI_BACKBUFFER_SLOT)

// Recursive view (Intel SDM Vol. 3A 4.5): with the recursive slot in index a, the next indices
// select which table is read; all four recursive = the PML4 itself.
#define X86_LAYOUT_RECURSIVE_VIEW(a, b, c, d) \
    (X86_LAYOUT_SLOT_ADDRESS(a) | ((unsigned long long)(b) << 30) | \
     ((unsigned long long)(c) << 21) | ((unsigned long long)(d) << 12))

// Framebuffer, GPU, surface and IOMMU: four consecutive slots that owners must never overlap.
#define X86_LAYOUT_DEVICES_VIRTUAL  X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_FB_SLOT)
#define X86_LAYOUT_DEVICES_BYTES    (4ULL * X86_LAYOUT_SLOT_BYTES)

#ifdef __cplusplus
#define X86_LAYOUT_ASSERT(condition, message) static_assert(condition, message)
#else
#define X86_LAYOUT_ASSERT(condition, message) _Static_assert(condition, message)
#endif
X86_LAYOUT_ASSERT(X86_LAYOUT_GPU_SLOT == X86_LAYOUT_FB_SLOT + 1 && X86_LAYOUT_SURFACE_SLOT == X86_LAYOUT_FB_SLOT + 2 &&
    X86_LAYOUT_IOMMU_SLOT == X86_LAYOUT_FB_SLOT + 3, "Device slots must stay consecutive");

// ====== predicates ===== //

// Intel SDM Vol. 3A 3.3.7.1: canonical addresses are [0, 2^47) and [0xFFFF8000_00000000, 2^64).
#define X86_LAYOUT_LOWER_LIMIT  (1ULL << 47)
#define X86_LAYOUT_UPPER_BASE   0xFFFF800000000000ULL
// K1: a kernel virtual range [address, address + bytes) of at least one byte, outside page 0,
// inside one canonical half (never across the non-canonical hole). For the lower half it is the
// former "address >= 4096 && bytes <= 2^47 - address" check.
#define X86_LAYOUT_RANGE(address, bytes) \
    ((bytes) != 0 && \
     (((address) >= 4096ULL && (address) < X86_LAYOUT_LOWER_LIMIT && (bytes) <= X86_LAYOUT_LOWER_LIMIT - (address)) || \
      ((address) >= X86_LAYOUT_UPPER_BASE && (bytes) - 1 <= ~0ULL - (address))))

// The whole object [address, address + bytes) lies inside the kernel image window.
#define X86_KERNEL_IMAGE_CONTAINS(address, bytes) \
    ((address) >= X86_KERNEL_IMAGE_BASE && (address) <= X86_KERNEL_IMAGE_LIMIT && \
     (bytes) <= X86_KERNEL_IMAGE_LIMIT - (address))

#endif
