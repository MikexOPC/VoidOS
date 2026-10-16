#include "controller.h"
#include "lib/memory/address_ranges.h"

// ====== table_length ======

static uint32_t table_length(const unsigned char *header)
{
    return (uint32_t)header[4] | ((uint32_t)header[5] << 8) |
        ((uint32_t)header[6] << 16) | ((uint32_t)header[7] << 24);
}

// ====== kernel_ps2_discover ======

int kernel_ps2_discover(const KERNEL_ACPI_DIRECTORY *directory, uint32_t bits,
    KERNEL_ACPI_READ read, void *context, KERNEL_PS2_DESCRIPTION *output)
{
    KERNEL_PS2_DESCRIPTION draft = {0};
    unsigned char header[36], prefix[116], chunk[64];
    uint32_t index, offset, count, byte, sum = 0, flags;
    if (!directory || !read || !output || !kernel_acpi_directory_valid(directory, bits) ||
        os_address_ranges_overlap((uint64_t)(uintptr_t)directory, sizeof(*directory),
            (uint64_t)(uintptr_t)output, sizeof(*output))) return 0;
    for (index = 0; index < directory->Count; ++index) {
        if (!read(context, directory->Entries[index], sizeof(header), header)) return 0;
        if (header[0] != 'F' || header[1] != 'A' || header[2] != 'C' || header[3] != 'P') continue;
        if (draft.Physical) return 0;
        draft.Physical = directory->Entries[index];
        draft.Length = table_length(header);
        if (draft.Length < sizeof(prefix) || draft.Length > 4096 ||
            !kernel_acpi_physical_span(draft.Physical, draft.Length, bits) ||
            os_address_ranges_overlap(draft.Physical, draft.Length, directory->Physical, directory->Length) ||
            !read(context, draft.Physical, sizeof(prefix), prefix)) return 0;
        for (byte = 0; byte < sizeof(header); ++byte) if (header[byte] != prefix[byte]) return 0;
    }
    if (!draft.Physical) { *output = draft; return 1; }
    for (offset = 0; offset < draft.Length; offset += count) {
        count = draft.Length - offset;
        if (count > sizeof(chunk)) count = sizeof(chunk);
        if (!read(context, draft.Physical + offset, count, chunk)) return 0;
        for (byte = 0; byte < count; ++byte) {
            sum += chunk[byte];
            if (offset + byte < sizeof(prefix) && prefix[offset + byte] != chunk[byte]) return 0;
        }
    }
    if (sum & 255) return 0;

    /* 
        ACPI 6.6 Table5.9: IAPC_BOOT_ARCH u16 at109; Table5.11 bit1=8042.
        Revision1 predates this field: do not guess presence from reserved bytes.
        Unknown revisions are outside this explicit v0 discovery profile. 
    */
   
    if  (prefix[8] >= 3 && prefix[8] <= 6) {
        flags = prefix[109] | ((uint32_t)prefix[110] << 8);
        if (flags & 0xffc0) return 0;
        draft.Present = (flags >> 1) & 1;
    }
    *output = draft;
    return 1;
}
