#include "../ecam.h"
#include "arch/x86/mm/window/access.h"
#include <intrin.h>

// ====== x86_ecam_load ======

// Intel316966-002 4.3.2 p78: base+bus*1MiB+device*32KiB+function*4KiB+offset.
// Tables5-1/6-1: ID0, Command04, class8, header0x0c, BAR0 0x10, buses0x18; aligned u32 reads.
int x86_ecam_load(BOOT_U32 offset,BOOT_U32 *value)
{
    const volatile unsigned char *page=x86_window_bytes();
    if (!page || !value || !x86_ecam_offset(offset)) return 0;
    _ReadWriteBarrier(); *value=*(const volatile BOOT_U32 *)(page+offset); _ReadWriteBarrier();
    return 1;
}

// ====== x86_ecam_store_command ======

// PCI Local Bus 3.0 6.2.2: a 16-bit write of Command at 04h leaves the RW1C Status half (06h) untouched.
int x86_ecam_store_command(BOOT_U32 value)
{
    volatile unsigned char *page = x86_window_bytes();

    if (!page || value > 0xffff) return 0;

    _ReadWriteBarrier();
    *(volatile unsigned short *)(page + 4) = (unsigned short)value;
    _ReadWriteBarrier();
    return 1;
}
