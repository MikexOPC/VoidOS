#[path = "../../../../../../../../../arch/x86/kernel/timer/hpet/watchdog/rust/selection/routes.rs"]
mod routes;
use routes::x86_watchdog_route_mask;

// ====== timer_bounds ======

#[test]
fn timer_bounds() {
    assert_eq!(x86_watchdog_route_mask(0, 0, u32::MAX), 0);
    assert_eq!(x86_watchdog_route_mask(0, 33, u32::MAX), 0);
    assert_eq!(x86_watchdog_route_mask(1, 1, u32::MAX), 0);
    assert_eq!(x86_watchdog_route_mask(u32::MAX, 32, u32::MAX), 0);
    assert_eq!(x86_watchdog_route_mask(31, 32, u32::MAX), 0x00ff_0000);
}

// ====== only_supported_non_isa_routes ======

#[test]
fn only_supported_non_isa_routes() {
    assert_eq!(x86_watchdog_route_mask(0, 1, 0), 0);
    assert_eq!(x86_watchdog_route_mask(0, 1, 0xff00_ffff), 0);
    for pin in 0..32 {
        let route = 1u32 << pin;
        assert_eq!(x86_watchdog_route_mask(0, 1, route),
            if (16..24).contains(&pin) { route } else { 0 });
    }
}
