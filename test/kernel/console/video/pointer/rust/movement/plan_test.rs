#[path = "../../../../../../../kernel/console/video/pointer/rust/movement/plan.rs"]
mod movement;
use movement::position as plan;

// ====== clamp_and_direction ======

#[test]
fn clamp_and_direction() {
    for width in [18, 32, 800, 1024, 2400] {
        for height in [18, 32, 600, 768, 1340] {
            for x in [0, (width - 18) / 2, width - 18] {
                for y in [0, (height - 18) / 2, height - 18] {
                    for dx in -256..=255 {
                        for dy in [-256, -1, 0, 1, 255] {
                            let value = plan(width, height, x, y, dx, dy);
                            assert_ne!(value, u64::MAX);
                            assert_eq!(value as u32, (i64::from(x) + i64::from(dx)).clamp(0, i64::from(width - 18)) as u32);
                            assert_eq!((value >> 32) as u32, (i64::from(y) - i64::from(dy)).clamp(0, i64::from(height - 18)) as u32);
                        }
                    }
                }
            }
        }
    }
}

// ====== invalid_state ======

#[test]
fn invalid_state() {
    for value in [0, 17, 2401, u32::MAX] { assert_eq!(plan(value, 768, 0, 0, 0, 0), u64::MAX); }
    for value in [0, 17, 1341, u32::MAX] { assert_eq!(plan(1024, value, 0, 0, 0, 0), u64::MAX); }
    assert_eq!(plan(1024, 768, 1007, 0, 0, 0), u64::MAX);
    assert_eq!(plan(1024, 768, 0, 751, 0, 0), u64::MAX);
    for value in [i32::MIN, -257, 256, i32::MAX] {
        assert_eq!(plan(1024, 768, 0, 0, value, 0), u64::MAX);
        assert_eq!(plan(1024, 768, 0, 0, 0, value), u64::MAX);
    }
}
