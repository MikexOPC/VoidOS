#include "ecam.h"
#include "lib/memory/address_ranges.h"

// ====== word_private ======

static BOOT_U32 word_private(const unsigned char *bytes)
{ return (BOOT_U32)bytes[0]|(BOOT_U32)bytes[1]<<8|(BOOT_U32)bytes[2]<<16|(BOOT_U32)bytes[3]<<24; }

// ====== x86_mcfg_find ======

int x86_mcfg_find(const KERNEL_ACPI_DIRECTORY *directory,BOOT_U32 bits,KERNEL_ACPI_READ read,void *state,
    unsigned char *scratch,BOOT_U32 capacity,X86_MCFG *output)
{
    X86_MCFG draft={0}; unsigned char header[36]; BOOT_U32 found=0;
    if (!output || ((BOOT_U64)output&7) || !scratch || capacity<X86_MCFG_BYTES || !read ||
        !kernel_acpi_directory_valid(directory,bits) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)directory,sizeof(*directory)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)scratch,capacity) ||
        os_address_ranges_overlap((BOOT_U64)scratch,capacity,(BOOT_U64)directory,sizeof(*directory))) return 0;
    for (BOOT_U32 index=0; index<directory->Count; ++index) {
        if (!read(state,directory->Entries[index],36,header)) return 0;
        if (word_private(header)!=0x4746434d) continue;
        BOOT_U32 bytes=word_private(header+4);
        if (found || bytes<60 || bytes>X86_MCFG_BYTES || !kernel_acpi_physical_span(directory->Entries[index],bytes,bits) ||
            !read(state,directory->Entries[index],bytes,scratch)) return 0;
        for (BOOT_U32 byte=0; byte<36; ++byte) if (header[byte]!=scratch[byte]) return 0;
        if (!x86_mcfg_decode(scratch,bytes,bits,&draft)) return 0;
        draft.Physical=directory->Entries[index]; found=1;
    }
    if (!found) return 2;
    if (!x86_mcfg_valid(&draft)) return 0;
    *output=draft;
    return 1;
}
