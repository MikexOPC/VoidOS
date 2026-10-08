#![cfg_attr(not(test), no_std)]

// ====== x86_watchdog_route_mask ======

// HPET 1.0a 2.3.8: route-capability bits identify IOAPIC inputs.
// Current profile admits only non-ISA pins16..23 on the verified controller.
#[unsafe(no_mangle)]
pub extern "C" fn x86_watchdog_route_mask(timer: u32, count: u32, routes: u32) -> u32 {
    if !(1..=32).contains(&count) || timer >= count { return 0; }
    routes & 0x00ff_0000
}
