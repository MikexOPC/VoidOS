#include "handoff/resources.h"

static UINT8 bytes[4 * 48 + 1];
static BOOT_MEMORY_MAP map;
static BOOT_KERNEL_IMAGE kernel;
static BOOT_KERNEL_STACK stack;

// ====== copertura disallineata, non ordinata e suddivisa ====== //

static void put(UINTN offset, UINT64 value, UINTN length)
{
    UINTN index;
    for (index = 0; index < length; ++index)
        bytes[1 + offset + index] = (UINT8)(value >> (index * 8));
}

static void prepare(void)
{
    UINTN index;
    static const UINT64 bases[] = {0x300000, 0x101000, 0x200000, 0x100000};
    for (index = 0; index < sizeof(bytes); ++index)
        bytes[index] = 0;
    map.Buffer = bytes + 1;
    map.Size = map.Capacity = 4 * 48;
    map.DescriptorSize = 48;
    map.DescriptorVersion = map.Valid = 1;
    kernel.Pages.Count = 2;
    kernel.Pages.AllocatedBytes = 3 * 4096;
    kernel.Description.Signature = BOOT_SEGMENTS_SIGNATURE;
    kernel.Description.Version = BOOT_SEGMENTS_VERSION;
    kernel.Description.Size = sizeof(kernel.Description);
    kernel.Description.VirtualEntry = 0x400100;
    kernel.Description.AllocatedBytes = kernel.Pages.AllocatedBytes;
    kernel.Description.SegmentCount = 2;
    for (index = 0; index < 2; ++index) {
        ELF_STAGED_SEGMENT *segment = &kernel.Pages.Segments[index];
        BOOT_SEGMENT *description = &kernel.Description.Segments[index];
        segment->PhysicalBase = index ? 0x200000 : 0x100000;
        segment->VirtualBase = index ? 0x500000 : 0x400000;
        segment->Pages = index ? 1 : 2;
        segment->Flags = index ? 6 : 5;
        description->PhysicalBase = segment->PhysicalBase;
        description->VirtualBase = segment->VirtualBase;
        description->Bytes = segment->Pages * 4096;
        description->Flags = segment->Flags;
        description->Reserved = 0;
    }
    stack.PhysicalBase = bases[0];
    stack.Pages = BOOT_STACK_PAGES;
    stack.Top = stack.PhysicalBase + stack.Pages * 4096;
    for (index = 0; index < 4; ++index) {
        put(index * 48, 2, 4);
        put(index * 48 + 8, bases[index], 8);
        put(index * 48 + 24, index ? 1 : 16, 8);
        put(index * 48 + 32, 8, 8);
    }
}

// ====== assenza di buchi, tipo corretto e risorse distinte ====== //

__declspec(dllexport) int run_resource_tests(void)
{
    int test;
    prepare();
    if (boot_check_resources(&map, &kernel, &stack) != EFI_SUCCESS ||
        boot_check_loader_range(&map, 0x100000, 2) != EFI_SUCCESS)
        return 951;
    for (test = 0; test < 17; ++test) {
        EFI_STATUS expected = EFI_INVALID_PARAMETER;
        prepare();
        switch (test) {
        case 0: put(48 + 8, 0x102000, 8); expected = EFI_LOAD_ERROR; break;
        case 1: put(48, 7, 4); expected = EFI_LOAD_ERROR; break;
        case 2: put(48 + 32, 1ULL << 63, 8); expected = EFI_LOAD_ERROR; break;
        case 3: put(48, 0xffffffff, 4); expected = EFI_LOAD_ERROR; break;
        case 4: stack.PhysicalBase = 0x100000; stack.Top = 0x110000; expected = EFI_LOAD_ERROR; break;
        case 5: stack.Top++; break;
        case 6: stack.Pages = ~0ULL; break;
        case 7: kernel.Description.Signature = 0; break;
        case 8: kernel.Description.Segments[0].PhysicalBase++; break;
        case 9: kernel.Pages.AllocatedBytes--; break;
        case 10: map.Valid = 0; break;
        case 11: kernel.Pages.Segments[0].Pages = ~0ULL; break;
        case 12: kernel.Description.Version++; break;
        case 13: put(48 + 8, 0x100000, 8); expected = EFI_LOAD_ERROR; break;
        case 14: kernel.Description.Reserved = 1; break;
        case 15: kernel.Pages.Segments[0].Flags = kernel.Description.Segments[0].Flags = 7; break;
        case 16: kernel.Pages.Segments[0].VirtualBase = kernel.Description.Segments[0].VirtualBase = 0; break;
        }
        if (boot_check_resources(&map, &kernel, &stack) != expected)
            return 952 + test;
    }
    prepare();
    if (boot_check_loader_range(&map, 0x100000, 0) != EFI_INVALID_PARAMETER ||
        boot_check_loader_range(&map, 0x100001, 1) != EFI_INVALID_PARAMETER ||
        boot_check_loader_range(&map, ~4095ULL, 1) != EFI_INVALID_PARAMETER ||
        boot_check_loader_range(&map, 0x400000, 1) != EFI_LOAD_ERROR ||
        boot_check_resources(0, &kernel, &stack) != EFI_INVALID_PARAMETER ||
        boot_check_resources(&map, 0, &stack) != EFI_INVALID_PARAMETER ||
        boot_check_resources(&map, &kernel, 0) != EFI_INVALID_PARAMETER)
        return 970;
    return 0;
}
