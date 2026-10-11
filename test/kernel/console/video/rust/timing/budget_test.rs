#[path = "../../../../../../kernel/console/video/rust/timing/budget.rs"]
mod budget;

// ====== timing_contract ======

#[test]
fn masks_and_reserved_bits() {
    for mask in [0, 1, 0xffff, 0x1ff_ffff] {
        assert_eq!(budget::sample(0, 1, mask), u64::MAX);
    }
    assert_eq!(budget::sample(0x100_0000, 1, 0xff_ffff), u64::MAX);
    assert_eq!(budget::sample(0, 0x100_0000, 0xff_ffff), u64::MAX);
}

#[test]
fn boundaries() {
    assert_eq!(budget::GOAL, 35_796);
    assert_eq!(budget::LIMIT, 357_955);
    assert_eq!(budget::sample(1, 1, u32::MAX), 2_u64 << 32);
    for ticks in [1, budget::LIMIT - 1, budget::LIMIT] {
        assert_eq!(budget::sample(0, ticks, u32::MAX), (1_u64 << 32) | u64::from(ticks));
    }
    assert_eq!(budget::sample(0, budget::LIMIT + 1, u32::MAX),
        (3_u64 << 32) | u64::from(budget::LIMIT + 1));
}

#[test]
fn wrap_and_reverse() {
    for mask in [0xff_ffff, u32::MAX] {
        assert_eq!(budget::sample(mask - 5, 4, mask), (1_u64 << 32) | 10);
        assert_eq!(budget::sample(100, 99, mask), (3_u64 << 32) | u64::from(mask));
    }
}

#[test]
fn generated_samples() {
    let mut seed = 1_u32;
    for mask in [0xff_ffff, u32::MAX] {
        for _ in 0..100_000 {
            seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let start = seed & mask;
            let ticks = (seed >> 8) % 100_000;
            let end = start.wrapping_add(ticks) & mask;
            let status = if ticks == 0 { 2 } else if ticks > budget::LIMIT { 3 } else { 1 };
            assert_eq!(budget::sample(start, end, mask), (status << 32) | u64::from(ticks));
        }
    }
}
