#include "stack.h"
#include "lib/memory/address_ranges.h"

// ====== riserve fisiche del kernel controllate prima di chiamare il firmware ====== //

static EFI_STATUS check_kernel_reservations(const BOOT_KERNEL_IMAGE *kernel)
{
    UINT32 index, previous;
    UINT64 total = 0;
    if (!kernel || !kernel->Pages.Count || kernel->Pages.Count > ELF_MAX_LOAD_SEGMENTS)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < kernel->Pages.Count; ++index) {
        const ELF_STAGED_SEGMENT *segment = &kernel->Pages.Segments[index];
        UINT64 bytes;
        if (!segment->PhysicalBase || (segment->PhysicalBase & 4095) || !segment->Pages ||
            segment->Pages > ELF_MAX_MEMORY_BYTES / 4096) return EFI_INVALID_PARAMETER;
        bytes = segment->Pages * 4096;
        if (segment->PhysicalBase > ~0ULL - bytes || bytes > ELF_MAX_MEMORY_BYTES - total)
            return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < index; ++previous)
            if (os_address_ranges_overlap(segment->PhysicalBase, bytes, kernel->Pages.Segments[previous].PhysicalBase,
                                          kernel->Pages.Segments[previous].Pages * 4096)) return EFI_INVALID_PARAMETER;
        total += bytes;
    }
    return total == kernel->Pages.AllocatedBytes ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

// ====== rilascio con conservazione delle pagine su errore ====== //

EFI_STATUS boot_release_stack(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_STACK *stack)
{
    EFI_STATUS status;
    if (!stack)
        return EFI_INVALID_PARAMETER;
    stack->Top = 0;
    if (!stack->Pages) {
        stack->PhysicalBase = 0;
        return EFI_SUCCESS;
    }
    if (!services || !services->FreePages)
        return EFI_UNSUPPORTED;
    status = services->FreePages(stack->PhysicalBase, stack->Pages);
    if (status == EFI_SUCCESS) {
        stack->PhysicalBase = 0;
        stack->Pages = 0;
    }
    return status;
}

// ====== allocazione e azzeramento senza cambio di RSP ====== //

EFI_STATUS boot_allocate_stack(EFI_BOOT_SERVICES_PREFIX *services, const BOOT_KERNEL_IMAGE *kernel,
                               BOOT_KERNEL_STACK *stack)
{
    UINT64 address = 0;
    UINT64 size = BOOT_STACK_PAGES * 4096;
    UINTN index;
    EFI_STATUS status;
    if (!stack || stack->Pages || stack->PhysicalBase || stack->Top)
        return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) ||
        !services->AllocatePages || !services->FreePages)
        return EFI_UNSUPPORTED;
    status = check_kernel_reservations(kernel);
    if (status != EFI_SUCCESS) return status;
    status = services->AllocatePages(0, 2, BOOT_STACK_PAGES, &address);
    if (status != EFI_SUCCESS)
        return status;
    if (os_address_ranges_overlap(address, size, (UINT64)(UINTN)stack, sizeof(*stack)) ||
        os_address_ranges_overlap(address, size, (UINT64)(UINTN)kernel, sizeof(*kernel)) ||
        os_address_ranges_overlap(address, size, (UINT64)(UINTN)services, services->Hdr.HeaderSize))
        return EFI_DEVICE_ERROR;
    for (index = 0; index < kernel->Pages.Count; ++index)
        if (os_address_ranges_overlap(address, size, kernel->Pages.Segments[index].PhysicalBase,
                                      kernel->Pages.Segments[index].Pages * 4096)) return EFI_DEVICE_ERROR;
    stack->PhysicalBase = address;
    stack->Pages = BOOT_STACK_PAGES;
    if (!address || (address & 4095) || address > ~0ULL - size) {
        boot_release_stack(services, stack);
        return EFI_DEVICE_ERROR;
    }
    for (index = 0; index < size; ++index)
        ((UINT8 *)address)[index] = 0;
    stack->Top = address + size;
    return EFI_SUCCESS;
}
