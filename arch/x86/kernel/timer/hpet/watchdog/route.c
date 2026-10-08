#include "watchdog.h"
#include "arch/x86/kernel/interrupts/apic/control/masked.h"

// ====== io_read ======

/* Intel 82093AA, 290566-001, sections 3.1.1/3.1.2: selector at 0,
 * window at 0x10; selector index identifies a dword register. */
static int io_read(BOOT_U32 index, BOOT_U32 *value)
{
    BOOT_U32 selected;
    return x86_apic_control_write(0,index) && x86_apic_control_read(0,&selected) &&
        (selected & 255) == index && x86_apic_control_read(0x10,value);
}

// ====== io_write ======

static int io_write(BOOT_U32 index, BOOT_U32 value)
{
    BOOT_U32 selected, actual;
    return x86_apic_control_write(0,index) && x86_apic_control_read(0,&selected) &&
        (selected & 255) == index && x86_apic_control_write(0x10,value) &&
        io_read(index,&actual) && actual == value;
}

// ====== allowed_pin ======

static int allowed_pin(const KERNEL_MADT_TOPOLOGY *topology, BOOT_U32 pin)
{
    BOOT_U32 index;
    if (pin < 16 || pin >= 24) { return 0; }
#ifdef KERNEL_TEST_WATCHDOG_ALTERNATE
    /* Test-only policy reservation; no actual IOAPIC/HPET register is altered. */
    if (x86_watchdog_state()->Timer == 0) { return 0; }
#endif
    for (index = 0; index < topology->IsoCount; ++index) {
        if (topology->Iso[index].Gsi == pin) { return 0; }
    }
    for (index = 0; index < topology->NmiCount; ++index) {
        if (topology->Nmi[index].Kind == 3 && topology->Nmi[index].Value == pin) { return 0; }
    }
    return 1;
}

// ====== route_operation ======

/* 82093AA section 3.2.4: entry indices 0x10+2*pin / +1; mask bit16,
 * level bit15, active-high bit13=0, physical destination in high bits31:24.
 * Delivery status bit12 / remote IRR bit14 must be clear before restore.
 * v0 owns a non-ISA masked pin on a single base-zero, 24-input controller. */
static int route_operation(void *argument)
{
    BOOT_U32 op = *(const BOOT_U32 *)argument;
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 selector, actual, index, reg, low, high;
    int result = 0;
    if (!x86_apic_control_read(0,&selector)) { return 0; }
    selector &= 255;
    if (!io_read(0,&actual) || ((actual >> 24) & 15) != owner->IoId ||
        !io_read(1,&actual) || actual != owner->IoVersion) { goto done; }
    if (op == 0) {
        result = 1; /* Completed read-only search; RouteOwned distinguishes no available pin. */
        for (index = 16; index < 24; ++index) {
            if (!(owner->Target & (1u << index)) || !allowed_pin(owner->Env.Topology,index)) { continue; }
            reg = 0x10 + index * 2;
            if (!io_read(reg,&low) || !io_read(reg+1,&high)) { result = 0; goto done; }
            if (!(low & 0x10000) || (low & ~0x1afffU) || (high & 0xffffff)) { continue; }
            owner->Pin = index; owner->IoLow = low; owner->IoHigh = high;
            /* Record before the first write so failure retains cleanup ownership. */
            owner->RouteOwned = 1;
            result = io_write(reg,0x18040) && io_write(reg+1,owner->Env.Profile->InitialId << 24);
            break;
        }
    } else {
        reg = 0x10 + owner->Pin * 2;
        if (!io_read(reg,&low) || !io_read(reg+1,&high)) { goto done; }
        if (op == 4) {
            /* 82093AA 3.2.4: masked bit16, no delivery bit12/remote IRR bit14.
             * Read-only redirection check; only the selector is written/restored. */
            result = (low == 0x18040 || low == owner->IoLow) && (low & 0x10000) &&
                !(low & 0x5000) &&
                (high == owner->IoHigh || high == owner->Env.Profile->InitialId << 24);
            goto done;
        }
        if (op == 3) {
            if ((low != 0x18040 && low != owner->IoLow) ||
                (high != owner->IoHigh && high != owner->Env.Profile->InitialId << 24)) { goto done; }
            result = io_write(reg+1,owner->IoHigh) && io_write(reg,owner->IoLow);
            if (result) { owner->RouteOwned = 0; }
            goto done;
        }
        if ((low != 0x18040 && low != 0x8040) ||
            high != owner->Env.Profile->InitialId << 24) { goto done; }
        if (op == 1) { result = io_write(reg,0x8040); }
        if (op == 2) { result = io_write(reg,0x18040); }
    }
done:
    return x86_apic_control_write(0,selector) && x86_apic_control_read(0,&actual) &&
        (actual & 255) == selector && result;
}

// ====== x86_watchdog_route ======

int x86_watchdog_route(BOOT_U32 operation)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    if (!x86_watchdog_valid() || operation > 4 ||
        (operation && !owner->RouteOwned) || (!operation && owner->RouteOwned)) { return 0; }
    return x86_apic_borrow(owner->Env.Window,owner->IoAddress,owner->Cache,1,
        route_operation,&operation);
}
