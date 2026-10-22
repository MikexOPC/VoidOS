#include "acquire.h"

// ====== read_private ======

static UINT64 read_private(const unsigned char *bytes, UINT32 at, UINT32 count)
{
    UINT64 result = 0;
    for (UINT32 byte = 0; byte < count; ++byte) result |= (UINT64)bytes[at+byte] << (byte*8);
    return result;
}

// ====== boot_vgpu_descriptor ======

// UEFI2.10 14.4.18, tables14.10/11: QWORD 8A/len2B, memory type+3,
// granularity+6, host minimum+0E, translation+1E, length+26, EndTag+2E.
// v0 accepts one memory descriptor and zero host/PCI translation only.
int boot_vgpu_descriptor(const unsigned char *bytes, UINTN capacity, UINT32 low,
    UINT32 high, UINT32 bits, UINT64 *size)
{
    UINT64 length, base;
    if (!bytes || !size || capacity < 48 || capacity > 4096 || bytes[0] != 0x8a ||
        read_private(bytes,1,2) != 43 || bytes[3] || bytes[46] != 0x79) return 0;
    if (bytes[47]) {
        UINT8 sum = 0;
        for (UINT32 byte = 0; byte < 48; ++byte) sum = (UINT8)(sum+bytes[byte]);
        if (sum) return 0;
    }
    if (read_private(bytes,6,8) != ((low & 7) == 4 ? 64U : 32U) ||
        read_private(bytes,30,8)) return 0;
    length = read_private(bytes,38,8);
    base = kernel_vgpu_pci_bar(low,high,length,bits);
    if (base == UINT64_MAX || base != read_private(bytes,14,8)) return 0;
    *size = length;
    return 1;
}
