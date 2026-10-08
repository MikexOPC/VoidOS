#include "../watchdog.h"

// ====== x86_watchdog_select ======

/* HPET 1.0a sections 2.3.4/2.3.8/2.3.9: NUM_TIM_CAP+1 timers;
 * config at 0x100+0x20*n, route capabilities +4, comparator +8/+12.
 * Read-only selection on the exclusively owned, globally halted block.
 * A failed selection leaves the previous saved comparator intact. */
int x86_watchdog_select(BOOT_U32 first)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 id, global, pending, period, timer, count;
    if (!x86_watchdog_valid() || owner->Env.Window->Active != 2 ||
        owner->Ready || owner->Armed || owner->RouteOwned ||
        !x86_watchdog_mmio_read(0,&id) || id != owner->Id ||
        !x86_watchdog_mmio_read(4,&period) || period != owner->Period ||
        !x86_watchdog_mmio_read(0x10,&global) || global != owner->GlobalSaved || (global & 3) ||
        !x86_watchdog_mmio_read(0x20,&pending) || pending) { return 0; }
    count = ((id >> 8) & 31) + 1;
    for (timer = first; timer < count; ++timer) {
        BOOT_U32 config, routes, low, high, offset = 0x100 + timer * 0x20;
        if (!x86_watchdog_mmio_read(offset,&config) ||
            !x86_watchdog_mmio_read(offset+4,&routes) || !x86_hpet_timer_valid(id,config) ||
            (config & ((1u << 2) | (1u << 3) | (1u << 6) | (1u << 14)))) { return 0; }
        routes = x86_watchdog_route_mask(timer,count,routes);
        if (!routes) { continue; }
        if (!x86_watchdog_mmio_read(offset+8,&low) ||
            !x86_watchdog_mmio_read(offset+12,&high)) { return 0; }
        owner->Timer = timer; owner->ConfigSaved = config;
        owner->MatchLow = low; owner->MatchHigh = high; owner->Target = routes;
        owner->ConfigBase = (config & 0x8030) | 2 | ((config & 32) ? 256u : 0u);
        return 1;
    }
    return 0;
}
