#include "../../watchdog.h"
#include "arch/x86/mm/window/access.h"
#include <stdio.h>
static X86_HPET_WATCHDOG owner;
static X86_PAGE_WINDOW window;
static BOOT_U32 registers[1024];

// ====== real_adapter_with_host_storage ======

X86_HPET_WATCHDOG *x86_watchdog_state(void) { return &owner; }
volatile unsigned char *x86_window_bytes(void) { return (volatile unsigned char *)registers; }

// ====== main ======

/* Exercise the actual MMIO adapter bounds/whitelist under ASan.
 * RAM storage models access, not HPET W1C or hardware ordering. */
int main(void)
{
    BOOT_U32 value, index;
    const BOOT_U32 reads[] = {0,4,0x10,0x20,0xf0,0xf4,0x100,0x104,0x108,0x10c,0x14c};
    const BOOT_U32 invalid[] = {1,8,0x14,0x24,0xfc,0x110,0x150,0x4ec,4096,~0u};
    const BOOT_U32 writes[] = {0x10,0x20,0xf0,0xf4,0x100,0x108,0x10c};
    owner.Env.Window = &window; owner.Id = 0x8086a201; owner.Timer = 0;
    if (x86_watchdog_mmio_read(0,&value) || x86_watchdog_mmio_write(0x10,1)) { return 1; }
    window.Active = 2;
    if (x86_watchdog_mmio_read(0,NULL) || x86_watchdog_mmio_write(0x10,1)) { return 2; }
    owner.Acquired = 1;
    for (index = 0; index < sizeof(reads)/sizeof(reads[0]); ++index) {
        if (!x86_watchdog_mmio_read(reads[index],&value)) { return 3; }
    }
    for (index = 0; index < sizeof(invalid)/sizeof(invalid[0]); ++index) {
        if (x86_watchdog_mmio_read(invalid[index],&value) ||
            x86_watchdog_mmio_write(invalid[index],1)) { return 4; }
    }
    for (index = 0; index < sizeof(writes)/sizeof(writes[0]); ++index) {
        if (!x86_watchdog_mmio_write(writes[index],index+1) ||
            !x86_watchdog_mmio_read(writes[index],&value) || value != index+1) { return 5; }
    }
    if (x86_watchdog_mmio_write(0,1) || x86_watchdog_mmio_write(4,1) ||
        x86_watchdog_mmio_write(0x104,1) || x86_watchdog_mmio_write(0x120,1) ||
        x86_watchdog_mmio_write(0x128,1) || x86_watchdog_mmio_write(0x110,1)) { return 6; }
    owner.Id = 0x8086bf01; owner.Timer = 31;
    if (!x86_watchdog_mmio_read(0x4ec,&value) || !x86_watchdog_mmio_write(0x4ec,42) ||
        x86_watchdog_mmio_write(0x500,1) || registers[0x4ec/4] != 42) { return 7; }
    window.Active = 0;
    if (x86_watchdog_mmio_read(0,&value) || x86_watchdog_mmio_write(0x10,1)) { return 8; }
    puts("WATCHDOG_REAL_MMIO_ADAPTER_ASAN_PASS");
    return 0;
}
