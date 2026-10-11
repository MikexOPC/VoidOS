#[path = "../../../../../../kernel/console/video/rust/cursor/plan.rs"]
mod plan;
use plan::kernel_console_plan as step;

// ====== geometry_and_states ======

#[test]
fn geometry_and_states() {
    for columns in [0, 1, 2, 80, 240, 241, u32::MAX] {
        for rows in [0, 1, 25, 67, 68, u32::MAX] {
            let valid = (1..=240).contains(&columns) && (1..=67).contains(&rows);
            assert_eq!(step(columns, rows, 0, 0, 13) != u64::MAX, valid);
            if valid {
                assert_eq!(step(columns, rows, columns + 1, 0, 65), u64::MAX);
                assert_eq!(step(columns, rows, 0, rows, 65), u64::MAX);
                for y in 0..rows {
                    for x in 0..=columns {
                        for scalar in [8, 10, 13, 65, 0xe8, 0x20ac, 0x10ffff] {
                            let value = step(columns, rows, x, y, scalar);
                            assert_ne!(value, u64::MAX);
                            assert!(value & 65535 <= u64::from(columns));
                            assert!((value >> 16) & 65535 < u64::from(rows));
                            assert!(value >> 32 <= 7);
                        }
                    }
                }
            }
        }
    }
}

// ====== unicode_exhaustive ======

#[test]
fn unicode_exhaustive() {
    for scalar in 0..=0x110000 {
        let valid = [8, 10, 13].contains(&scalar) ||
            (char::from_u32(scalar).is_some() && scalar >= 32 && !(127..=159).contains(&scalar));
        assert_eq!(step(80, 25, 0, 0, scalar) != u64::MAX, valid);
    }
    assert_eq!(step(80, 25, 0, 0, u32::MAX), u64::MAX);
}

// ====== deferred_wrap_and_scroll ======

#[test]
fn deferred_wrap_and_scroll() {
    assert_eq!(step(3, 2, 2, 0, 65), 3 | (1_u64 << 32));
    assert_eq!(step(3, 2, 3, 0, 66), 1 | (1_u64 << 16) | (1_u64 << 32));
    assert_eq!(step(3, 2, 3, 1, 67), 1 | (1_u64 << 16) | (5_u64 << 32));
    assert_eq!(step(3, 2, 3, 1, 10), (1_u64 << 16) | (4_u64 << 32));
    assert_eq!(step(1, 1, 1, 0, 65), 1 | (5_u64 << 32));
}

// ====== backspace_and_return ======

#[test]
fn backspace_and_return() {
    assert_eq!(step(3, 2, 0, 0, 8), 0);
    assert_eq!(step(3, 2, 3, 1, 8), 2 | (1_u64 << 16) | (2_u64 << 32));
    assert_eq!(step(3, 2, 0, 1, 8), 2 | (2_u64 << 32));
    assert_eq!(step(3, 2, 3, 1, 13), 1_u64 << 16);
}
