#include "watchdog.h"

// ====== confirmed ======

static int confirmed(BOOT_U32 offset, BOOT_U32 value)
{
    BOOT_U32 actual;
    return x86_watchdog_mmio_write(offset,value) &&
        x86_watchdog_mmio_read(offset,&actual) && actual == value;
}

// ====== capture ======

/* HPET 1.0a 2.3.5/2.3.8: claim only a globally halted block without
 * enabled/periodic/FSB comparators or pending status. No counter reset.
 * Save selected comparator and halted counter before any mutation. */
static int capture(X86_HPET_WATCHDOG *owner)
{
    BOOT_U32 id, period, global, pending, config, routes, index, low, high;
    if (!x86_watchdog_mmio_read(0,&id) || !x86_watchdog_mmio_read(4,&period) ||
        !x86_hpet_capabilities_valid(owner->Id,id,period) ||
        !x86_watchdog_mmio_read(0x10,&global) || (global & 3) ||
        !x86_watchdog_mmio_read(0x20,&pending) || pending) { return 0; }
    owner->Period = period; owner->Timer = 32;
    for (index = 0; index <= ((id >> 8) & 31); ++index) {
        BOOT_U32 offset = 0x100 + 0x20 * index;
        if (!x86_watchdog_mmio_read(offset,&config) ||
            !x86_watchdog_mmio_read(offset+4,&routes) || !x86_hpet_timer_valid(id,config) ||
            (config & ((1u << 2) | (1u << 3) | (1u << 6) | (1u << 14)))) { return 0; }
    }
    if (!(owner->Ticks = x86_watchdog_ticks(period,100)) ||
        !x86_watchdog_mmio_read(0xf0,&low) || !x86_watchdog_mmio_read(0xf4,&high)) { return 0; }
    owner->CounterSaved = ((BOOT_U64)high << 32) | low;
    owner->GlobalSaved = global;
    if (!x86_watchdog_select(0)) { return 0; }
    owner->Acquired = 1;
    return 1;
}

// ====== restore ======

/* HPET 1.0a 2.3.7: counter writes only while halted. Restore full saved
 * comparator after returning to original size mode, with interrupts off. */
static int restore(X86_HPET_WATCHDOG *owner, BOOT_U32 offset)
{
    BOOT_U32 pending;
    if (owner->Armed || !x86_watchdog_mmio_read(0x20,&pending) || pending ||
        !confirmed(0x10,owner->GlobalSaved) || !confirmed(offset,owner->ConfigSaved) ||
        !confirmed(offset+8,owner->MatchLow) ||
        ((owner->ConfigSaved & 32) && !confirmed(offset+12,owner->MatchHigh)) ||
        !confirmed(0xf0,(BOOT_U32)owner->CounterSaved) ||
        ((owner->Id & 0x2000) && !confirmed(0xf4,(BOOT_U32)(owner->CounterSaved >> 32)))) { return 0; }
    owner->Acquired = 0;
    return 1;
}

// ====== device_operation ======

static int device_operation(void *argument)
{
    BOOT_U32 op = *(const BOOT_U32 *)argument;
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 offset = 0x100 + owner->Timer * 0x20, config, pending, now;
    if (op == 0) { return capture(owner); }
    if (op == 6) { return x86_watchdog_select(owner->Timer+1); }
    if (op == 4) { return restore(owner,offset); }
    if (op == 7) {
        /* HPET 1.0a 2.3.4/2.3.5/2.3.6/2.3.8: same ID/period, no pending (0x20), IRQ enable
         * bit2 clear, halted/original enabled global state (0x10).
         * Accept only saved or owned disabled configuration during rollback. */
        BOOT_U32 id, period, global;
        return x86_watchdog_mmio_read(0,&id) && id == owner->Id &&
            x86_watchdog_mmio_read(4,&period) && period == owner->Period &&
            x86_hpet_capabilities_valid(owner->Id,id,period) &&
            x86_watchdog_mmio_read(offset,&config) && x86_watchdog_mmio_read(0x20,&pending) &&
            !pending && !(config & 4) &&
            (config == owner->ConfigSaved || config == (owner->ConfigBase | (owner->Pin << 9))) &&
            x86_watchdog_mmio_read(0x10,&global) &&
            (global == owner->GlobalSaved || global == (owner->GlobalSaved | 1));
    }
    if (!x86_watchdog_mmio_read(offset,&config) || !x86_watchdog_mmio_read(0x20,&pending)) { return 0; }
    if (op == 1) {
        BOOT_U32 index;
        if (!confirmed(offset,owner->ConfigBase | (owner->Pin << 9)) ||
            !confirmed(0x10,owner->GlobalSaved | 1)) { return 0; }
        if (!x86_watchdog_mmio_read(0xf0,&now)) { return 0; }
        for (index = 0; index < 2000; ++index) {
            BOOT_U32 current;
            if (!x86_watchdog_mmio_read(0xf0,&current)) { return 0; }
            if (current != now) { return 1; }
        }
        return 0;
    }
    if (op == 2) {
        BOOT_U32 expected = owner->ConfigBase | (owner->Pin << 9);
        if (owner->Armed || pending || config != expected ||
            !x86_watchdog_mmio_read(0xf0,&owner->Start)) { return 0; }
        owner->Target = owner->Start + owner->Ticks;
        if (!confirmed(offset+8,owner->Target)) { return 0; }
        owner->Armed = 1;
        if (!confirmed(offset,expected | 4) || !x86_watchdog_mmio_read(0xf0,&now) ||
            !x86_watchdog_ahead(owner->Start,now,owner->Ticks)) {
            if (confirmed(offset,expected) && x86_watchdog_mmio_read(0x20,&pending) && !pending) {
                owner->Armed = 0;
            }
            return 0;
        }
        return 1;
    }
    if (op == 3) {
        BOOT_U32 bit = 1u << owner->Timer;
        if (!owner->Armed || config != (owner->ConfigBase | (owner->Pin << 9) | 4) ||
            pending != bit || !x86_watchdog_mmio_read(0xf0,&now) ||
            (BOOT_U32)(now - owner->Start) < owner->Ticks ||
            (BOOT_U32)(now - owner->Start) >= 0x80000000u ||
            !confirmed(offset,config & ~4u) || !x86_watchdog_mmio_write(0x20,bit) ||
            !x86_watchdog_mmio_read(0x20,&pending) || pending) { return 0; }
        owner->Armed = 0;
        return 1;
    }
    if (op == 5) {
        return !owner->Armed && !pending && config == (owner->ConfigBase | (owner->Pin << 9));
    }
    return 0;
}

// ====== x86_watchdog_device ======

int x86_watchdog_device(BOOT_U32 operation)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    if (!x86_watchdog_valid() || operation > 7 ||
        (operation == 6 && (owner->Ready || owner->Armed || owner->RouteOwned)) ||
        (operation && operation != 7 && !owner->Acquired) || (!operation && owner->Acquired) ||
        (operation == 7 && (owner->Timer >= 32 || owner->Armed || owner->Error))) { return 0; }
    return x86_apic_borrow(owner->Env.Window,owner->Physical,owner->Cache,
        operation != 0 && operation != 6 && operation != 7,device_operation,&operation);
}
