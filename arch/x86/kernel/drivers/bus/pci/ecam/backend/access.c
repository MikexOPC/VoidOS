#include "../ecam.h"
#include "arch/x86/mm/window/access.h"
#include <intrin.h>

// ====== x86_ecam_load ======

// Intel316966-002 4.3.2 p78: base+bus*1MiB+device*32KiB+function*4KiB+offset.
// Tables5-1/6-1: ID0, class8, header0x0c, buses0x18; aligned u32 reads, no config writes.
int x86_ecam_load(BOOT_U32 offset,BOOT_U32 *value)
{
    const volatile unsigned char *page=x86_window_bytes();
    if (!page || !value || !x86_ecam_offset(offset)) return 0;
    _ReadWriteBarrier(); *value=*(const volatile BOOT_U32 *)(page+offset); _ReadWriteBarrier();
    return 1;
}
