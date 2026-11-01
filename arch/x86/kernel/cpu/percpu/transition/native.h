#ifndef OS_X86_AP_TRANSITION_NATIVE_H
#define OS_X86_AP_TRANSITION_NATIVE_H
#include "transition.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/mm/paging/inventory.h"
int x86_ap_transition_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
#endif
