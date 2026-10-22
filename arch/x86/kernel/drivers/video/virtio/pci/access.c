#include "../mapping.h"
#include <intrin.h>
#include "kernel/drivers/video/virtio/discovery/configuration/read.h"

// ====== read_private ======

static int read_private(void *opaque, BOOT_U32 offset, BOOT_U32 bytes, unsigned char *output)
{
    const BOOT_U32 *selector=opaque;
    if (offset>256 || bytes>256-offset) return 0;
    for (BOOT_U32 byte=0; byte<bytes; ++byte) {
        BOOT_U32 at=offset+byte;
        __outdword(0xcf8,*selector|(at&~3U));
        output[byte]=__inbyte((unsigned short)(0xcfc+(at&3)));
    }
    return 1;
}

// ====== x86_vgpu_pci_match ======

// QEMU11.1.0 q35.c/pci_host.c: CF8 selector enable31,bus23:16,
// device15:11,function10:8,offset7:2; CFC data read only.
// BSP/IF=0 is checked through the existing CPU profile before selector changes.
int x86_vgpu_pci_match(const KERNEL_VGPU_BOOT *boot, BOOT_U32 bits)
{
    X86_FB_CPU cpu;
    KERNEL_VGPU_BOOT current;
    KERNEL_VGPU_PCI_LAYOUT expected, actual;
    unsigned long saved;
    BOOT_U32 selector;
    if (!boot || !boot->Present || !kernel_vgpu_boot_valid(boot,bits) ||
        !x86_fb_cpu(&cpu) || cpu.PhysicalBits != bits) return 0;
    current=*boot;
    selector=0x80000000U|(boot->Bus<<16)|(boot->Device<<11)|(boot->Function<<8);
    saved=__indword(0xcf8);
    int valid=kernel_vgpu_config_read(read_private,&selector,current.Config);
    __outdword(0xcf8,saved);
    if (!valid) return 0;
    if (!(current.Config[4] & 2)) return 0;
    for (BOOT_U32 offset=16; offset<40; ++offset)
        if (current.Config[offset] != boot->Config[offset]) return 0;
    if (kernel_vgpu_pci_probe(boot->Config,256,boot->Sizes,6,bits,&expected) != KERNEL_VGPU_PCI_READY ||
        kernel_vgpu_pci_probe(current.Config,256,current.Sizes,6,bits,&actual) != KERNEL_VGPU_PCI_READY) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(actual); ++byte)
        if (((const unsigned char *)&actual)[byte] != ((const unsigned char *)&expected)[byte]) return 0;
    return 1;
}
