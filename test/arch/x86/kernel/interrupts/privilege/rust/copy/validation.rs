#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_op_in_unsafe_fn)]

const USER_BASE: u64 = 0x400000;
const USER_LIMIT: u64 = 0x407000;
const MAX_COPY: u64 = 256;

// ====== x86_k5_copy_range ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_k5_copy_range(pointer: u64, length: u64) -> u32 {
    if length > MAX_COPY {
        return 3;
    }
    match pointer.checked_add(length) {
        Some(end) if pointer >= USER_BASE && end <= USER_LIMIT => 1,
        _ => 2,
    }
}

// ====== x86_k5_copy_fault ======

#[unsafe(no_mangle)]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn x86_k5_copy_fault(
    rip: u64,
    cs: u64,
    flags: u64,
    error: u64,
    cr2: u64,
    first: u64,
    last: u64,
    pointer: u64,
    length: u64,
    write: u64,
) -> u32 {
    // Intel SDM 3A §7.15, Interrupt 14: P 0, W/R 1, U/S 2, RSVD 3, I/D 4, PK 5.
    const AC: u64 = 1 << 18;
    let expected_write = if write != 0 { 2 } else { 0 };
    let Some(end) = pointer.checked_add(length) else {
        return 0;
    };
    u32::from(
        cs == 8
            && (first..last).contains(&rip)
            && flags & AC != 0
            && error & !1 == expected_write
            && (pointer..end).contains(&cr2),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== ranges ======

    #[test]
    fn ranges() {
        for (pointer, length) in [(0x400000, 0), (0x407000, 0), (0x406f00, 256), (0x405ff8, 16)] {
            assert_eq!(x86_k5_copy_range(pointer, length), 1);
        }
        for (pointer, length) in [
            (0x3fffff, 1),
            (0x3fffff, 0),
            (0x406fff, 2),
            (0x407001, 0),
            (0x408000, 8),
            (0xffffffff80000000, 8),
            (0x0000800000000000, 8),
            (u64::MAX - 7, 16),
        ] {
            assert_eq!(x86_k5_copy_range(pointer, length), 2);
        }
        for length in [257, u64::MAX] {
            assert_eq!(x86_k5_copy_range(0x405000, length), 3);
        }
    }

    // ====== faults ======

    #[test]
    fn faults() {
        const AC: u64 = 0x40000;
        let check = |rip, cs, flags, error, cr2, write| {
            x86_k5_copy_fault(rip, cs, flags, error, cr2, 0x1000, 0x1002, 0x405ff0, 16, write)
        };
        assert_eq!(check(0x1000, 8, AC, 0, 0x405ff0, 0), 1);
        assert_eq!(check(0x1001, 8, AC, 1, 0x405fff, 0), 1);
        assert_eq!(check(0x1000, 8, AC, 3, 0x405ff8, 1), 1);
        assert_eq!(check(0x1000, 8, AC, 2, 0x405ff8, 1), 1);
        for (rip, cs, flags, error, cr2, write) in [
            (0x0fff, 8, AC, 0, 0x405ff0, 0),
            (0x1002, 8, AC, 0, 0x405ff0, 0),
            (0x1000, 0x33, AC, 0, 0x405ff0, 0),
            (0x1000, 8, 0, 0, 0x405ff0, 0),
            (0x1000, 8, AC, 2, 0x405ff0, 0),
            (0x1000, 8, AC, 0, 0x405ff0, 1),
            (0x1000, 8, AC, 4, 0x405ff0, 0),
            (0x1000, 8, AC, 8, 0x405ff0, 0),
            (0x1000, 8, AC, 0x10, 0x405ff0, 0),
            (0x1000, 8, AC, 0x20, 0x405ff0, 0),
            (0x1000, 8, AC, 0, 0x405fef, 0),
            (0x1000, 8, AC, 0, 0x406000, 0),
        ] {
            assert_eq!(check(rip, cs, flags, error, cr2, write), 0);
        }
        assert_eq!(x86_k5_copy_fault(0, 8, AC, 0, 0, 0, 1, u64::MAX, 2, 0), 0);
    }
}
