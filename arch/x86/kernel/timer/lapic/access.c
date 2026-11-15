#include "timer.h"
#include "arch/x86/kernel/interrupts/apic/lvt.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== sola lettura dei registri xAPIC timer attraverso un owner valido ===== //

int x86_timer_read(const X86_LAPIC_MAPPING *mapping, BOOT_U32 offset, BOOT_U32 *output)
{
    BOOT_U64 address = (BOOT_U64)output;
    if (!output || (address & 3) || address > ~0ULL-sizeof(*output) ||
        os_address_ranges_overlap(address,sizeof(*output),(BOOT_U64)mapping,sizeof(*mapping)) ||
        os_address_ranges_overlap(address,sizeof(*output),X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(address,sizeof(*output),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap(address,sizeof(*output),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
        (offset != 0x30 && offset != 0x320 && offset != 0x380 && offset != 0x390 && offset != 0x3e0) ||
        !x86_lapic_validate(mapping)) return 0;
    *output = *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL+offset);
    return 1;
}

// ====== scritture limitate a oneshot maschera divisore e initial count ===== //

int x86_timer_write(const X86_LAPIC_MAPPING *mapping, BOOT_U32 offset, BOOT_U32 value)
{
    if (!x86_lapic_validate(mapping)) return 0;
    if (offset == 0x320) {
#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
        // Intel SDM 078US Vol. 3A §11.5.4: LVT Timer +320h, periodic bit 17.
        if (value == 0x20041U) {
            *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL+offset) = value;
            return 1;
        }
#endif
        // Intel SDM 078US Vol. 3A §11.5.4: LVT Timer +320h, periodic bit 17, only for the preemption vector.
        if (value == (X86_LVT_TIMER_PERIODIC | X86_PREEMPT_VECTOR)) {
            *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL+offset) = value;
            return 1;
        }
        if ((value & ~0x100ffU) || (value & 255) < 32 || (value & 255) == 255) return 0;
    } else if (offset == 0x3e0) {
        if (value & ~0xbU) return 0;
    } else if (offset != 0x380) return 0;
    *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL+offset) = value;
    return 1;
}
