#include "inventory.h"

// ====== lettura little endian senza strutture packed o accessi non allineati ====== //

static BOOT_U64 read_number(const unsigned char *bytes, BOOT_U32 width)
{
    BOOT_U64 value = 0;
    BOOT_U32 index;
    for (index = 0; index < width; ++index) value |= (BOOT_U64)bytes[index] << (index * 8);
    return value;
}

// ====== lettura limitata del descrittore riusata dall'allocatore fisico ====== //

int kernel_read_region(const unsigned char *map, BOOT_U64 bytes, BOOT_U64 stride,
                       BOOT_U64 index, KERNEL_MEMORY_REGION *region)
{
    const unsigned char *source;
    KERNEL_MEMORY_REGION draft;
    if (!map || !region || !bytes || bytes > 1048576 || stride < 40 || stride > 4096 ||
        bytes % stride || bytes / stride > 4096 || index >= bytes / stride ||
        (BOOT_U64)map > ~0ULL - bytes || (BOOT_U64)region > ~0ULL - sizeof(*region) ||
        ((BOOT_U64)region < (BOOT_U64)map + bytes && (BOOT_U64)map < (BOOT_U64)region + sizeof(*region))) return 0;
    source = map + index * stride;
    draft.Type = (BOOT_U32)read_number(source, 4);
    draft.Base = read_number(source + 8, 8);
    draft.Pages = read_number(source + 24, 8);
    draft.Attributes = read_number(source + 32, 8);
    *region = draft;
    return 1;
}

// ====== controllo completo prima della pubblicazione del conteggio ====== //

int kernel_inventory_memory(const unsigned char *map, BOOT_U64 bytes, BOOT_U64 stride,
                            KERNEL_MEMORY_INVENTORY *output)
{
    KERNEL_MEMORY_INVENTORY draft = {0};
    BOOT_U64 count, index, previous;
    if (!map || !output || !bytes || bytes > 1048576 || stride < 40 || stride > 4096 ||
        bytes % stride || bytes / stride > 4096) return 0;
    if ((BOOT_U64)map > ~0ULL - bytes || (BOOT_U64)output > ~0ULL - sizeof(*output) ||
        ((BOOT_U64)output < (BOOT_U64)map + bytes && (BOOT_U64)map < (BOOT_U64)output + sizeof(*output))) return 0;
    count = bytes / stride;
    for (index = 0; index < count; ++index) {
        const unsigned char *region = map + index * stride;
        BOOT_U32 type = (BOOT_U32)read_number(region, 4);
        BOOT_U64 base = read_number(region + 8, 8), pages = read_number(region + 24, 8);
        BOOT_U64 attributes = read_number(region + 32, 8);
        BOOT_U64 *total;
        if (!kernel_region_valid(base, pages, attributes, type)) return 0;
        for (previous = 0; previous < index; ++previous) {
            const unsigned char *other = map + previous * stride;
            if (kernel_regions_overlap(base, pages, read_number(other + 8, 8), read_number(other + 24, 8))) return 0;
        }
        total = base >= 4096 && kernel_region_candidate(attributes, type) ? &draft.ConventionalPages : &draft.ReservedPages;
        if (pages > ~0ULL - *total) return 0;
        *total += pages;
    }
    draft.Regions = (BOOT_U32)count;
    *output = draft;
    return 1;
}
