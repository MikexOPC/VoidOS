#include "handoff/stack.h"

__declspec(align(4096)) static UINT8 memory[64 * 1024 + 4096];
static UINT64 returned_address;
static UINTN allocations, releases;
static int mode, owned, mock_error;

// ====== allocatore controllato e output falliti non acquisiti ====== //

static EFI_STATUS EFIAPI allocate(UINT32 type, UINT32 memory_type, UINTN pages, UINT64 *address)
{
    ++allocations;
    if (type || memory_type != 2 || pages != BOOT_STACK_PAGES || owned) {
        mock_error = 1;
        return EFI_INVALID_PARAMETER;
    }
    *address = (UINT64)memory;
    if (mode == 1) {
        *address = ~0ULL;
        return EFI_OUT_OF_RESOURCES;
    }
    if (mode == 2 || mode == 7) ++*address;
    if (mode == 3) *address = 0;
    if (mode == 4) *address = ~4095ULL;
    returned_address = *address;
    owned = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(UINT64 address, UINTN pages)
{
    ++releases;
    if (!owned || address != returned_address || pages != BOOT_STACK_PAGES) {
        mock_error = 1;
        return EFI_INVALID_PARAMETER;
    }
    if (mode == 5 || mode == 7)
        return EFI_DEVICE_ERROR;
    owned = 0;
    return EFI_SUCCESS;
}

// ====== azzeramento esatto, rilascio ritentabile e sentinelle ====== //

__declspec(dllexport) int run_stack_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services;
    BOOT_KERNEL_STACK stack = {0};
    UINTN index;
    int test;
    services.Hdr.HeaderSize = sizeof(services);
    services.AllocatePages = allocate;
    services.FreePages = release;
    for (test = 0; test <= 7; ++test) {
        EFI_STATUS status;
        mode = test;
        owned = mock_error = 0;
        allocations = releases = 0;
        for (index = 0; index < sizeof(memory); ++index)
            memory[index] = 0xcc;
        status = boot_allocate_stack(&services, &stack);
        if (test == 0 || test == 5 || test == 6) {
            if (status != EFI_SUCCESS || stack.Pages != 16 || stack.PhysicalBase != (UINT64)memory ||
                stack.Top != (UINT64)memory + 64 * 1024 || (stack.Top & 15) || releases)
                return 901;
            for (index = 0; index < sizeof(memory); ++index)
                if (memory[index] != (index < 64 * 1024 ? 0 : 0xcc))
                    return 902;
            if (boot_allocate_stack(&services, &stack) != EFI_INVALID_PARAMETER || allocations != 1)
                return 903;
        } else {
            if (status != (test == 1 ? EFI_OUT_OF_RESOURCES : EFI_DEVICE_ERROR) ||
                (test != 7 && stack.Pages) || stack.Top)
                return 904;
            for (index = 0; index < sizeof(memory); ++index)
                if (memory[index] != 0xcc)
                    return 905;
        }
        status = boot_release_stack(&services, &stack);
        if (test == 5 || test == 7) {
            if (status != EFI_DEVICE_ERROR || !stack.Pages || stack.Top || !owned)
                return 906;
            mode = 0;
            status = boot_release_stack(&services, &stack);
        }
        if (status != EFI_SUCCESS || stack.Pages || stack.PhysicalBase || stack.Top || owned || mock_error)
            return 907;
        index = releases;
        if (boot_release_stack(0, &stack) != EFI_SUCCESS || releases != index)
            return 908;
    }
    services.AllocatePages = 0;
    if (boot_allocate_stack(&services, &stack) != EFI_UNSUPPORTED ||
        boot_allocate_stack(&services, 0) != EFI_INVALID_PARAMETER ||
        boot_release_stack(&services, 0) != EFI_INVALID_PARAMETER)
        return 909;
    return 0;
}
