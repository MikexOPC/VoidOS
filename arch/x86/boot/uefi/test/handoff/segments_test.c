#include "handoff/segments.h"

void make_kernel_fixture(UINT8 *buffer);
static UINT8 file[512];
static ELF_LOAD_PLAN plan;
static ELF_STAGED_IMAGE image;
static BOOT_SEGMENTS description;

// ====== piano verificato e pagine simulate ====== //

static int prepare(void)
{
    UINTN index;
    make_kernel_fixture(file);
    if (elf_validate(file, sizeof(file), &plan) != ELF_VALID)
        return 0;
    image.Count = 2;
    image.AllocatedBytes = 8192;
    for (index = 0; index < 2; ++index) {
        image.Segments[index].PhysicalBase = 0x400000 + index * 4096;
        image.Segments[index].VirtualBase = plan.Segments[index].VirtualAddress & ~4095ULL;
        image.Segments[index].Pages = 1;
        image.Segments[index].Flags = plan.Segments[index].Flags;
    }
    return 1;
}

// ====== campi ABI e descrizioni incoerenti ====== //

__declspec(dllexport) int run_handoff_tests(void)
{
    UINTN index;
    int test;
    if ((UINTN)((UINT8 *)&description.Segments - (UINT8 *)&description) != 40 ||
        (UINTN)((UINT8 *)&description.VirtualEntry - (UINT8 *)&description) != 16)
        return 601;
    if (!prepare() || boot_describe_segments(&plan, &image, &description) != EFI_SUCCESS ||
        description.Signature != BOOT_SEGMENTS_SIGNATURE || description.Version != 1 ||
        description.Size != 552 || description.VirtualEntry != 0x100100 ||
        description.AllocatedBytes != 8192 || description.SegmentCount != 2 || description.Reserved)
        return 602;
    for (index = 0; index < ELF_MAX_LOAD_SEGMENTS; ++index) {
        const BOOT_SEGMENT *segment = &description.Segments[index];
        if (segment->Reserved)
            return 603;
        if (index < 2) {
            if (segment->PhysicalBase != image.Segments[index].PhysicalBase ||
                segment->VirtualBase != image.Segments[index].VirtualBase ||
                segment->Bytes != 4096 || segment->Flags != image.Segments[index].Flags)
                return 604;
        } else if (segment->PhysicalBase || segment->VirtualBase || segment->Bytes || segment->Flags) {
            return 605;
        }
    }
    for (test = 0; test < 15; ++test) {
        if (!prepare())
            return 606;
        switch (test) {
        case 0: image.Count = 0; break;
        case 1: image.Count = ELF_MAX_LOAD_SEGMENTS + 1; break;
        case 2: image.Segments[1].Pages = 0; break;
        case 3: image.Segments[0].Pages = ~0ULL; break;
        case 4: image.Segments[1].PhysicalBase = image.Segments[0].PhysicalBase; break;
        case 5: image.Segments[0].PhysicalBase++; break;
        case 6: image.Segments[0].PhysicalBase = ~4095ULL; break;
        case 7: image.Segments[0].Flags = 7; break;
        case 8: image.AllocatedBytes--; break;
        case 9: plan.TotalMemoryBytes--; break;
        case 10: plan.Entry = plan.Segments[0].VirtualAddress + plan.Segments[0].FileBytes; break;
        case 11: plan.SegmentCount = 1; break;
        case 12: image.Segments[1].VirtualBase = image.Segments[0].VirtualBase; break;
        case 13: plan.Segments[0].MemoryBytes = ~0ULL; break;
        case 14: plan.Segments[0].FileBytes = 65; break;
        }
        description.Signature = BOOT_SEGMENTS_SIGNATURE;
        if (boot_describe_segments(&plan, &image, &description) != EFI_INVALID_PARAMETER ||
            description.Signature || description.Version || description.Size ||
            description.VirtualEntry || description.AllocatedBytes || description.SegmentCount)
            return 610 + test;
        for (index = 0; index < ELF_MAX_LOAD_SEGMENTS; ++index)
            if (description.Segments[index].PhysicalBase || description.Segments[index].VirtualBase ||
                description.Segments[index].Bytes || description.Segments[index].Flags ||
                description.Segments[index].Reserved)
                return 630;
    }
    if (!prepare() || boot_describe_segments(0, &image, &description) != EFI_INVALID_PARAMETER ||
        boot_describe_segments(&plan, 0, &description) != EFI_INVALID_PARAMETER ||
        boot_describe_segments(&plan, &image, 0) != EFI_INVALID_PARAMETER)
        return 631;
    return 0;
}
