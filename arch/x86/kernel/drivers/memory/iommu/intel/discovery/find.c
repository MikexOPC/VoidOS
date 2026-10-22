#include "find.h"
#include "lib/memory/address_ranges.h"

// ====== word_private ======

static uint32_t word_private(const unsigned char *bytes)
{
    return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|(uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
}

// ====== x86_dmar_find ======

int x86_dmar_find(const KERNEL_ACPI_DIRECTORY *directory,uint32_t bits,KERNEL_ACPI_READ read,void *context,
    unsigned char *scratch,size_t capacity,X86_DMAR_INFO *output)
{
    X86_DMAR_INFO draft={0};
    unsigned char header[36];
    int found=0;
    if (!output || (uintptr_t)output%_Alignof(X86_DMAR_INFO) || !scratch || capacity<X86_DMAR_MAX_BYTES ||
        !read || !kernel_acpi_directory_valid(directory,bits) ||
        os_address_ranges_overlap((uintptr_t)scratch,capacity,(uintptr_t)output,sizeof(*output)) ||
        os_address_ranges_overlap((uintptr_t)directory,sizeof(*directory),(uintptr_t)output,sizeof(*output)) ||
        os_address_ranges_overlap((uintptr_t)directory,sizeof(*directory),(uintptr_t)scratch,capacity)) return 0;
    for (uint32_t index=0; index<directory->Count; ++index) {
        if (!read(context,directory->Entries[index],36,header)) return 0;
        if (word_private(header)!=0x52414d44) continue;
        uint32_t bytes=word_private(header+4);
        if (found || bytes<48 || bytes>X86_DMAR_MAX_BYTES ||
            !kernel_acpi_physical_span(directory->Entries[index],bytes,bits) ||
            !read(context,directory->Entries[index],bytes,scratch)) return 0;
        for (uint32_t byte=0; byte<36; ++byte) if (scratch[byte]!=header[byte]) return 0;
        if (!x86_dmar_decode(scratch,bytes,bits,&draft)) return 0;
        draft.Physical=directory->Entries[index]; found=1;
    }
    if (!found) return X86_DMAR_ABSENT;
    for (uint32_t byte=0; byte<sizeof(draft); ++byte) ((volatile unsigned char *)output)[byte]=((const unsigned char *)&draft)[byte];
    return X86_DMAR_FOUND;
}
