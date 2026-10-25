#include "bundle.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== confini e separazione prima di accedere al profilo ====== //

static int valid_layout(const BOOT_DESCRIPTOR_BUNDLE *bundle, const BOOT_DESCRIPTOR_LAYOUT *layout)
{
    const uint64_t bytes = X86_BOOT_EXCEPTION_STACK_BYTES;
    uint32_t vector;
    if (!bundle || !layout || ((uintptr_t)bundle & 15) || ((uintptr_t)layout & 7)) return 0;
    if (BOOT_DESCRIPTOR_OFFSET(bundle, TaskDescriptor) != BOOT_DESCRIPTOR_OFFSET(bundle, Gdt) + 24 ||
        (BOOT_DESCRIPTOR_OFFSET(bundle, Task) & 15) || (BOOT_DESCRIPTOR_OFFSET(bundle, Idt) & 15)) return 0;
    if (os_address_ranges_overlap((uint64_t)(uintptr_t)bundle, sizeof(*bundle),
                                  (uint64_t)(uintptr_t)layout, sizeof(*layout))) return 0;
    // K1: each range in one canonical half (layout.h). Stacks keep the former strict end margin.
    if ((layout->LinearBase & 4095) || !X86_LAYOUT_RANGE(layout->LinearBase, 4096) ||
        (layout->PrivilegeStack & 4095) || !X86_LAYOUT_RANGE(layout->PrivilegeStack, bytes + 1) ||
        (layout->ExceptionStack & 4095) || !X86_LAYOUT_RANGE(layout->ExceptionStack, bytes + 1)) return 0;
    if (os_address_ranges_overlap(layout->LinearBase, 4096, layout->PrivilegeStack, bytes) ||
        os_address_ranges_overlap(layout->LinearBase, 4096, layout->ExceptionStack, bytes) ||
        os_address_ranges_overlap(layout->PrivilegeStack, bytes, layout->ExceptionStack, bytes)) return 0;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector) {
        uint64_t target = layout->Targets[vector];
        if (!X86_LAYOUT_RANGE(target, 1) ||
            os_address_ranges_overlap(target, 1, layout->LinearBase, 4096) ||
            os_address_ranges_overlap(target, 1, layout->PrivilegeStack, bytes) ||
            os_address_ranges_overlap(target, 1, layout->ExceptionStack, bytes)) return 0;
    }
    return 1;
}

// ====== preparazione locale senza pubblicare uno stato parziale ====== //

int boot_prepare_descriptor_bundle(BOOT_DESCRIPTOR_BUNDLE *bundle, const BOOT_DESCRIPTOR_LAYOUT *layout)
{
    BOOT_DESCRIPTOR_BUNDLE draft;
    uint8_t *output = (uint8_t *)bundle;
    uint8_t *bytes = (uint8_t *)&draft;
    uint32_t index;
    if (!valid_layout(bundle, layout)) return 0;
    for (index = 0; index < sizeof(draft); ++index) bytes[index] = 0;
    if (!x86_prepare_boot_gdt(&draft.Gdt, &draft.Gdtr,
                              layout->LinearBase + BOOT_DESCRIPTOR_OFFSET(bundle, Gdt)) ||
        !x86_prepare_boot_tss(&draft.Task, &draft.TaskDescriptor,
                              layout->LinearBase + BOOT_DESCRIPTOR_OFFSET(bundle, Task),
                              layout->PrivilegeStack, layout->ExceptionStack) ||
        !x86_prepare_boot_idt(&draft.Idt, &draft.Idtr,
                              layout->LinearBase + BOOT_DESCRIPTOR_OFFSET(bundle, Idt), layout->Targets)) return 0;
    draft.Gdtr.Bytes[0] = 39;
    for (index = 0; index < X86_BOOT_EXCEPTION_COUNT; ++index) draft.Idt.Gates[index][4] = 1;
    draft.Signature = BOOT_DESCRIPTOR_SIGNATURE;
    if (!boot_validate_descriptor_bundle(&draft, layout)) return 0;
    bundle->Signature = 0;
    for (index = sizeof(bundle->Signature); index < sizeof(draft); ++index) output[index] = bytes[index];
    bundle->Signature = BOOT_DESCRIPTOR_SIGNATURE;
    return 1;
}

// ====== verifica incrociata di GDT IDT e TSS prima del caricamento ====== //

int boot_validate_descriptor_bundle(const BOOT_DESCRIPTOR_BUNDLE *bundle, const BOOT_DESCRIPTOR_LAYOUT *layout)
{
    X86_GDT_POINTER gdt_pointer;
    X86_IDT_POINTER idt_pointer;
    X86_BOOT_IDT idt;
    uint32_t vector, byte;
    if (!valid_layout(bundle, layout) || bundle->Signature != BOOT_DESCRIPTOR_SIGNATURE) return 0;
    for (byte = 0; byte < sizeof(bundle->ReservedBeforeTask); ++byte)
        if (bundle->ReservedBeforeTask[byte]) return 0;
    for (byte = 0; byte < sizeof(bundle->ReservedBeforeIdt); ++byte)
        if (bundle->ReservedBeforeIdt[byte]) return 0;
    for (byte = 0; byte < 10; ++byte) {
        gdt_pointer.Bytes[byte] = bundle->Gdtr.Bytes[byte];
        idt_pointer.Bytes[byte] = bundle->Idtr.Bytes[byte];
    }
    if (gdt_pointer.Bytes[0] != 39) return 0;
    gdt_pointer.Bytes[0] = 23;
    if (!x86_validate_boot_gdt(&bundle->Gdt, &gdt_pointer,
                               layout->LinearBase + BOOT_DESCRIPTOR_OFFSET(bundle, Gdt)) ||
        !x86_validate_boot_tss(&bundle->Task, &bundle->TaskDescriptor,
                               layout->LinearBase + BOOT_DESCRIPTOR_OFFSET(bundle, Task),
                               layout->PrivilegeStack, layout->ExceptionStack)) return 0;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector) {
        if (bundle->Idt.Gates[vector][4] != 1) return 0;
        for (byte = 0; byte < 16; ++byte) idt.Gates[vector][byte] = bundle->Idt.Gates[vector][byte];
        idt.Gates[vector][4] = 0;
    }
    return x86_validate_boot_idt(&idt, &idt_pointer,
                                 layout->LinearBase + BOOT_DESCRIPTOR_OFFSET(bundle, Idt), layout->Targets);
}

// ====== revoca del preflight senza azzerare descrittori o frame attivi ====== //

void boot_invalidate_descriptor_bundle(BOOT_DESCRIPTOR_BUNDLE *bundle)
{
    if (bundle) bundle->Signature = 0;
}
