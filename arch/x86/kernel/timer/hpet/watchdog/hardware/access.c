#include "../watchdog.h"
#include "arch/x86/mm/window/access.h"

// ====== readable ======

static int readable(BOOT_U32 offset)
{
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 base, kind;
    if ((offset & 3) || owner->Env.Window->Active != 2) { return 0; }
    if (offset == 0 || offset == 4 || offset == 0x10 || offset == 0x20 ||
        offset == 0xf0 || offset == 0xf4) { return 1; }
    if (offset < 0x100 || offset > 0x4ec) { return 0; }
    base = (offset - 0x100) / 0x20; kind = (offset - 0x100) % 0x20;
    return base <= ((owner->Id >> 8) & 31) && kind <= 12;
}

// ====== x86_watchdog_mmio_read ======

/* HPET 1.0a sections 2.3.2/2.3.4..2.3.9: aligned dword accesses,
 * exactly one hardware register half. Owner supplies the active UC borrow. */
int x86_watchdog_mmio_read(BOOT_U32 offset, BOOT_U32 *value)
{
    if (!value || !readable(offset)) { return 0; }
    *value = *(const volatile BOOT_U32 *)(x86_window_bytes() + offset);
    return 1;
}

// ====== x86_watchdog_mmio_write ======

/* Write whitelist: general config, W1C interrupt status, stopped counter,
 * acquired timer config/comparator. No RO capabilities or FSB register. */
int x86_watchdog_mmio_write(BOOT_U32 offset, BOOT_U32 value)
{
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 timer = 0x100 + owner->Timer * 0x20;
    if (!owner->Acquired || !readable(offset) ||
        (offset != 0x10 && offset != 0x20 && offset != 0xf0 && offset != 0xf4 &&
         offset != timer && offset != timer + 8 && offset != timer + 12)) { return 0; }
    *(volatile BOOT_U32 *)(x86_window_bytes() + offset) = value;
    return 1;
}
