#include "internal.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/descriptors/segments.h"
uint32_t xp_panic_apic_id(void);

// ====== xp_cpu_id ======

uint32_t xp_cpu_id(void) { return xp_panic_apic_id(); }

// ====== xp_image_contains ======

int xp_image_contains(uintptr_t address, size_t bytes) {
    return bytes && X86_KERNEL_IMAGE_CONTAINS(address, bytes);
}

// ====== xp_descriptors_ready ======

int xp_descriptors_ready(void) { return x86_kernel_segments_retire_ready(); }
