#include "arch/x86/kernel/timer/hpet/description/probe/probe.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include <stdio.h>
#include <stdlib.h>
static KERNEL_BOOT_CONTEXT context;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_LAPIC_MAPPING lapic;
static X86_PAGING_INVENTORY inventory;
static BOOT_U64 leaves[512];
static BOOT_U32 valid = 1, calls, checks;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL HPET console policy line %d\n", __LINE__); exit(1); } } while (0)

// ====== preflight_mocks ======

int x86_fb_kernel_inventory_verify(const KERNEL_BOOT_CONTEXT *ctx,
    const KERNEL_PHYSICAL_ALLOCATOR *physical, const X86_LAPIC_MAPPING *mapping,
    const X86_PAGING_INVENTORY *snapshot)
{
    ++calls;
    return valid && ctx == &context && physical == &pool && mapping == &lapic && snapshot == &inventory;
}
int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *ctx,
    const KERNEL_PHYSICAL_ALLOCATOR *physical, const X86_PAGING_INVENTORY *snapshot)
{
    return valid && ctx == &context && physical == &pool && snapshot == &inventory;
}
int kernel_inventory_memory(const unsigned char *map, BOOT_U64 bytes,
    BOOT_U64 stride, KERNEL_MEMORY_INVENTORY *output)
{
    (void)map; (void)bytes; (void)stride;
    output->Regions = 0;
    return 1;
}
int kernel_read_region(const unsigned char *map, BOOT_U64 bytes,
    BOOT_U64 stride, BOOT_U64 index, KERNEL_MEMORY_REGION *output)
{
    (void)map; (void)bytes; (void)stride; (void)index; (void)output;
    return 0;
}
BOOT_U32 kernel_regions_overlap(BOOT_U64 first, BOOT_U64 pages,
    BOOT_U64 second, BOOT_U64 other_pages)
{
    return pages && other_pages && first < second + other_pages * 4096 && second < first + pages * 4096;
}
BOOT_U32 x86_hpet_table_span(BOOT_U64 address, BOOT_U32 bytes, BOOT_U32 bits)
{
    return bits == 48 && bytes == 4 && address < (1ULL << 48);
}
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    return address == 0x8000 ? leaves : NULL;
}

// ====== preflight ======

static int preflight(BOOT_U64 physical)
{
    return x86_hpet_owned_preflight(&context, &pool, &inventory, physical, &lapic);
}

// ====== main ======

int main(void)
{
    BOOT_U32 pass, page;
    pool.PhysicalBits = 48;
    for (pass = 0; pass < 100; ++pass) {
        inventory.Count = 0; inventory.Video.AperturePages = 0;
        CHECK(preflight(0x10000000));
        inventory.Video.Physical = 0x10000000;
        inventory.Video.Pages = 1; inventory.Video.AperturePages = 3;
        for (page = 0; page < 3; ++page) CHECK(!preflight(0x10000000 + page * 4096ULL));
        CHECK(preflight(0x0ffff000)); CHECK(preflight(0x10003000));
        valid = 0; CHECK(!preflight(0x10003000)); valid = 1;
        inventory.Count = 1;
        inventory.Tables[0].Physical = 0x10003000;
        CHECK(!preflight(0x10003000));
        inventory.Tables[0].Physical = 0x8000;
        inventory.Tables[0].View = 0x8000; inventory.Tables[0].Level = 3;
        leaves[0] = 0x10003003; CHECK(!preflight(0x10003000)); leaves[0] = 0;
        CHECK(preflight(0x10003000));
        CHECK(!preflight(0x10003001));
    }
    printf("PASS active HPET policy: %u checks, %u validator calls; whole video aperture excluded\n", checks, calls);
    return 0;
}
