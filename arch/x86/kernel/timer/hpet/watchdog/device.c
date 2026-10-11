#include "watchdog.h"
static BOOT_U32 arm_detail;
static BOOT_U64 arm_registers;
static BOOT_U32 rollover_seen;

// ====== x86_watchdog_arm_detail ======

BOOT_U32 x86_watchdog_arm_detail(void) { return arm_detail; }

// ====== x86_watchdog_arm_registers ======

BOOT_U64 x86_watchdog_arm_registers(void) { return arm_registers; }

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
    if (op == 2) arm_detail = 2;
    if (!x86_watchdog_mmio_read(offset,&config) || !x86_watchdog_mmio_read(0x20,&pending)) { return 0; }
    if (op == 2) arm_registers = ((BOOT_U64)config << 32) | pending;
    if (op == 1) {
        BOOT_U32 index;
        if (!confirmed(offset,owner->ConfigBase | (owner->Pin << 9)) ||
            !confirmed(0x10,owner->GlobalSaved | 1)) { return 0; }
        if (!x86_watchdog_mmio_read(0xf0,&now)) { return 0; }
        for (index = 0; index < 2000; ++index) {
            BOOT_U32 current;
            if (!x86_watchdog_mmio_read(0xf0,&current)) { return 0; }
            if (current != now) {
                /* HPET 1.0a 2.3.5/2.3.6: quiesce after the liveness probe;
                 * no deadline exists yet, so only this timer's status may be cleared. */
                BOOT_U32 bit = 1u << owner->Timer;
                return confirmed(0x10,owner->GlobalSaved) &&
                    x86_watchdog_mmio_read(0x20,&pending) && !(pending & ~bit) &&
                    (!pending || x86_watchdog_mmio_write(0x20,bit)) &&
                    x86_watchdog_mmio_read(0x20,&pending) && !pending;
            }
        }
        return 0;
    }
    if (op == 2) {
        BOOT_U32 expected = owner->ConfigBase | (owner->Pin << 9);
        arm_detail = 3;
        if (owner->Armed) return 0;
        arm_detail = 13;
        if (pending & ~(1u << owner->Timer)) return 0;
        arm_detail = 14;
        if (config != expected) return 0;
        arm_detail = 4;
        if (!x86_watchdog_mmio_read(0x10,&now) ||
            (now != owner->GlobalSaved && now != (owner->GlobalSaved | 1))) return 0;
        arm_detail = 5;
        if (!confirmed(0x10,owner->GlobalSaved)) return 0;
        /* HPET 1.0a 2.3.6/2.3.8: INT_ENB_CNF bit2 masks delivery, not status.
         * Halt at 0x10 before W1C at 0x20; discard only the owned inactive timer. */
        arm_detail = 13;
        if (!x86_watchdog_mmio_read(0x20,&pending) ||
            (pending & ~(1u << owner->Timer)) ||
            (pending && !x86_watchdog_mmio_write(0x20,1u << owner->Timer)) ||
            !x86_watchdog_mmio_read(0x20,&pending) || pending) return 0;
        arm_detail = 6;
        if (!x86_watchdog_mmio_read(0xf0,&owner->Start)) return 0;
        owner->Target = owner->Start + owner->Ticks;
        arm_detail = 7;
        if (!confirmed(offset+8,owner->Target)) { return 0; }
        owner->Armed = 1;
        rollover_seen = 0;
        arm_detail = 8;
        if (!confirmed(offset,expected | 4)) goto arm_failed;
        arm_detail = 9;
        if (!x86_watchdog_mmio_read(0xf0,&now) || now != owner->Start ||
            !x86_watchdog_ahead(owner->Start,now,owner->Ticks)) goto arm_failed;
        arm_detail = 10;
        if (!x86_watchdog_mmio_read(0x20,&pending) || pending) goto arm_failed;
        arm_detail = 11;
        if (!confirmed(0x10,owner->GlobalSaved | 1)) goto arm_failed;
        arm_detail = 0;
        return 1;
arm_failed:
        {
            if (confirmed(offset,expected) && confirmed(0x10,owner->GlobalSaved) &&
                x86_watchdog_mmio_read(0x20,&pending) && !pending) {
                owner->Armed = 0;
            }
            return 0;
        }
    }
    if (op == 3) {
        BOOT_U32 bit = 1u << owner->Timer;
        if (!owner->Armed || config != (owner->ConfigBase | (owner->Pin << 9) | 4) ||
            pending != bit || !x86_watchdog_mmio_read(0xf0,&now) ||
            (BOOT_U32)(now - owner->Start) >= 0x80000000u) return 0;
        /* HPET 1.0a 2.4.3.1: a 32-bit one-shot also interrupts at rollover.
         * Acknowledge at most one wrap before the target; retain the deadline.
         * Read W1C status before time so a newly pending match cannot be mistaken
         * for an early duplicate using an older counter sample. */
        if ((BOOT_U32)(now - owner->Start) < owner->Ticks) {
            if (rollover_seen || owner->Target >= owner->Start || now >= owner->Start ||
                !x86_watchdog_mmio_write(0x20,bit) ||
                !x86_watchdog_mmio_read(0x20,&pending) || (pending & ~bit) ||
                !x86_watchdog_mmio_read(0xf0,&now) ||
                (BOOT_U32)(now - owner->Start) >= 0x80000000u) return 0;
            rollover_seen = 1;
            if ((BOOT_U32)(now - owner->Start) < owner->Ticks) return !pending;
        }
        /* HPET 1.0a 2.3.5/2.3.6/2.3.8: stop owned block (0x10 bit0) before
         * masking comparator (bit2) and W1C status, preventing idle wrap events. */
        if (!confirmed(0x10,owner->GlobalSaved) ||
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
    int result;
    if (operation == 2) { arm_detail = 1; arm_registers = 0; }
    if (!x86_watchdog_valid() || operation > 7 ||
        (operation == 6 && (owner->Ready || owner->Armed || owner->RouteOwned)) ||
        (operation && operation != 7 && !owner->Acquired) || (!operation && owner->Acquired) ||
        (operation == 7 && (owner->Timer >= 32 || owner->Armed || owner->Error))) { return 0; }
    result = x86_apic_borrow(owner->Env.Window,owner->Physical,owner->Cache,
        operation != 0 && operation != 6 && operation != 7,device_operation,&operation);
    if (operation == 2 && !result && !arm_detail) arm_detail = 12;
    return result;
}
