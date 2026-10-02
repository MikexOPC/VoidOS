#include "paging/plan.h"

// ====== fixture numerica senza dereferenziare indirizzi fisici ====== //

__declspec(dllexport) int run_plan_tests(void)
{
    BOOT_KERNEL_IMAGE kernel = {0};
    BOOT_KERNEL_STACK stack = {0x800000, 16, 0x810000};
    BOOT_MAPPING_PLAN plan;
    UINT32 scenario;
    kernel.Pages.Count = 1;
    kernel.Pages.AllocatedBytes = 4096;
    kernel.Pages.Segments[0].PhysicalBase = 0x400000;
    kernel.Pages.Segments[0].VirtualBase = 0x100000;
    kernel.Pages.Segments[0].Pages = 1;
    kernel.Pages.Segments[0].Flags = 5;
    kernel.Description.Signature = BOOT_SEGMENTS_SIGNATURE;
    kernel.Description.Version = BOOT_SEGMENTS_VERSION;
    kernel.Description.Size = sizeof(kernel.Description);
    kernel.Description.VirtualEntry = 0x100100;
    kernel.Description.AllocatedBytes = 4096;
    kernel.Description.SegmentCount = 1;
    kernel.Description.Segments[0].PhysicalBase = 0x400000;
    kernel.Description.Segments[0].VirtualBase = 0x100000;
    kernel.Description.Segments[0].Bytes = 4096;
    kernel.Description.Segments[0].Flags = 5;
    if (boot_plan_mappings(&kernel, &stack, &plan) != EFI_SUCCESS || plan.Count != 2 ||
        plan.Mappings[1].Flags != 6 || plan.StackTop != BOOT_STACK_VIRTUAL + 65536)
        return 1;
    for (scenario = 0; scenario < 14; ++scenario) {
        BOOT_MAPPING_PLAN bad = plan;
        switch (scenario) {
        case 0: bad.Count = BOOT_MAPPING_LIMIT + 1; break;
        case 1: bad.Mappings[0].Flags = 7; break;
        case 2: bad.Mappings[0].VirtualBase = BOOT_STACK_GUARD; break;
        case 3: bad.Mappings[0].PhysicalBase = stack.PhysicalBase; break;
        case 4: bad.Mappings[0].Pages = ~0ULL; break;
        case 5: bad.Mappings[0].VirtualBase = 1ULL << 47; break;
        case 6: bad.Entry = 0x101000; break;
        case 7: bad.Mappings[1].Flags = 5; break;
        case 8: bad.Mappings[0].PhysicalBase = ~0ULL - 4095; break;
        case 9: bad.StackTop++; break;
        case 10: bad.Mappings[0].VirtualBase = (UINT64)X86_BOOT_RECURSIVE_SLOT << 39; bad.Entry = bad.Mappings[0].VirtualBase; break;
        case 11: bad.Mappings[0].VirtualBase = X86_KERNEL_PAGE_WINDOW - 4096; bad.Entry = bad.Mappings[0].VirtualBase; break;
        case 12: bad.Mappings[0].VirtualBase = X86_KERNEL_PAGE_WINDOW; bad.Entry = bad.Mappings[0].VirtualBase; break;
        default: bad.Mappings[0].VirtualBase = X86_KERNEL_PAGE_WINDOW + 4096; bad.Entry = bad.Mappings[0].VirtualBase; break;
        }
        if (boot_validate_mapping_plan(&bad) == EFI_SUCCESS)
            return 10 + (int)scenario;
    }
    kernel.Description.Segments[0].Bytes++;
    if (boot_plan_mappings(&kernel, &stack, &plan) == EFI_SUCCESS || plan.Count || plan.Entry)
        return 30;
    return 0;
}
