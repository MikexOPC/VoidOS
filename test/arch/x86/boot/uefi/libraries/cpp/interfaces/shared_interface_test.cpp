#include "lib/memory/address_ranges.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/buffer_capacity.h"
#include "arch/x86/lib/processor/paging_validation.h"
#include "arch/x86/lib/processor/processor_access.h"
#include "arch/x86/lib/descriptors/gdt.h"
#include "arch/x86/lib/descriptors/idt.h"
#include "arch/x86/boot/uefi/handoff/descriptors/transition/bundle.h"
#include "arch/x86/configs/boot_profile.h"

static_assert(sizeof(uint32_t) == 4 && sizeof(uint64_t) == 8, "Unexpected integer ABI");
static_assert(sizeof(X86_PAGING_REGISTERS) == 32, "Unexpected register ABI");
static_assert(X86_BOOT_STACK_BYTES == 65536 && X86_BOOT_STACK_TOP == (X86_LAYOUT_WINDOWS_VIRTUAL + 0x11000ULL),
              "Unexpected boot stack profile");
static_assert((UINT64_C(0x800018) & X86_PAGING_ROOT_MASK) == UINT64_C(0x800000), "Unexpected CR3 mask");

// ====== consumatore C++ delle librerie C e Rust senza interfacce UEFI ====== //

extern "C" __declspec(dllexport) int run_shared_interface_tests(void)
{
    X86_BOOT_GDT gdt;
    X86_GDT_POINTER pointer;
    X86_BOOT_IDT idt;
    X86_IDT_POINTER interrupt_pointer;
    uint64_t targets[32];
    BOOT_DESCRIPTOR_BUNDLE bundle;
    BOOT_DESCRIPTOR_LAYOUT layout;
    layout.LinearBase = 0x100000;
    layout.PrivilegeStack = 0x200000;
    layout.ExceptionStack = 0x300000;
    for (uint32_t vector = 0; vector < 32; ++vector) layout.Targets[vector] = 0x400000 + vector;
    if (!boot_prepare_descriptor_bundle(&bundle, &layout) ||
        !boot_validate_descriptor_bundle(&bundle, &layout)) return 9;
    boot_invalidate_descriptor_bundle(&bundle);
    if (boot_validate_descriptor_bundle(&bundle, &layout)) return 10;
    for (uint32_t vector = 0; vector < 32; ++vector) targets[vector] = 4096 + vector;
    if (!x86_prepare_boot_idt(&idt, &interrupt_pointer, 4096, targets) ||
        !x86_validate_boot_idt(&idt, &interrupt_pointer, 4096, targets)) return 8;
    if (!x86_prepare_boot_gdt(&gdt, &pointer, 4096) ||
        !x86_validate_boot_gdt(&gdt, &pointer, 4096)) return 7;
    if (!os_address_ranges_overlap(4096, 8192, 8192, 4096) ||
        os_address_ranges_overlap(4096, 4096, 8192, 4096))
        return 1;
    if (x86_validate_paging_profile(0x80000008U, (1U << 29) | (1U << 20), (48U << 8) | 52U) != 52)
        return 2;
    if (x86_validate_paging_profile(0x80000007U, UINT32_MAX, (48U << 8) | 52U))
        return 3;
    if (x86_validate_paging_state((UINT64_C(1) << 31) | 1, UINT64_C(0x800018),
                                  UINT64_C(1) << 5, (UINT64_C(1) << 8) | (UINT64_C(1) << 10), 52) != X86_PAGING_VALID)
        return 4;
    if (os_buffer_capacity(96, 48, 8, 1, 1024 * 1024) != 480 ||
        os_buffer_capacity(96, 48, 8, 4096, 1024 * 1024) != 4096 ||
        os_buffer_capacity(UINT64_MAX, 48, 8, 4096, UINT64_MAX)) return 5;
    if (!os_buffer_extent_is_valid(4096, 4096, 4096) ||
        os_buffer_extent_is_valid(UINT64_MAX, 1, UINT64_MAX)) return 6;
    return 0;
}
