#include "native.h"
#include <intrin.h>

// ====== x86_vgpu_queue_pci ======

// EDK2 stable202408 IndustryStandard/Pci22.h, PCI2.2 section6.1: Command0x04,
// MEMORY bit1 and MASTER bit2 must already be enabled by firmware.
// q35/pci_host.c: CF8 enable31,BDF23:8; CFC+offset data. No PCI data writes.
int x86_vgpu_queue_pci(const KERNEL_VGPU_BOOT *gpu)
{
    X86_FB_CPU cpu;
    if (!gpu || !gpu->Present || !x86_fb_cpu(&cpu) || !x86_vgpu_pci_match(gpu,cpu.PhysicalBits)) return 0;
    BOOT_U32 selector=0x80000000U|(gpu->Bus<<16)|(gpu->Device<<11)|(gpu->Function<<8);
    unsigned long saved=__indword(0xcf8);
    __outdword(0xcf8,selector|4);
    unsigned short command=__inword(0xcfc);
    __outdword(0xcf8,saved);
    return (command&6)==6;
}
