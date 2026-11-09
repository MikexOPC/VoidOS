use crate::plan::{IMAGE_END, USER_END};
use crate::parser::USER_BASE;

pub const MAX_COPY: u64 = 256;
pub const SYS_EXIT: u64 = 0;
pub const SYS_WRITE: u64 = 1;

pub const RANGE_OK: u32 = 1;
pub const RANGE_BAD: u32 = 2;
pub const LENGTH_BAD: u32 = 3;

pub const STATE_VALIDATED: u32 = 1;
pub const STATE_MAPPED: u32 = 2;
pub const STATE_RUNNING: u32 = 3;
pub const STATE_TERMINATED: u32 = 4;
pub const STATE_REAPED: u32 = 5;

const LOWER_LIMIT: u64 = 1 << 47;
const AC: u64 = 1 << 18;

// ====== copy_range ======

pub fn copy_range(pointer: u64, length: u64) -> u32 {
    if length > MAX_COPY {
        return LENGTH_BAD;
    }
    match pointer.checked_add(length) {
        Some(end) if pointer >= USER_BASE && end <= USER_END => RANGE_OK,
        _ => RANGE_BAD,
    }
}

// ====== return_valid ======

pub fn return_valid(rip: u64, cs: u64, flags: u64, rsp: u64, ss: u64) -> bool {
    // Intel SDM 3A §2.3: CF, PF, AF, ZF, SF, TF, IF, DF and OF may return to CPL3.
    const USER_FLAGS: u64 = 0xfd5;
    (USER_BASE..IMAGE_END).contains(&rip)
        && cs == 0x33
        && ss == 0x2b
        && flags & !USER_FLAGS == 2
        && flags & 0x200 != 0
        && rsp < LOWER_LIMIT
}

// ====== irq_frame_valid ======

pub fn irq_frame_valid(rip: u64, cs: u64, flags: u64, rsp: u64, ss: u64) -> bool {
    // Intel SDM 3A §§2.3, 7.14.2: an IRQ image may also carry RF.
    return_valid(rip, cs, flags & !(1 << 16), rsp, ss)
}

// ====== fault_recoverable ======

#[allow(clippy::too_many_arguments)]
pub fn fault_recoverable(
    rip: u64,
    cs: u64,
    flags: u64,
    error: u64,
    cr2: u64,
    window: (u64, u64),
    range: (u64, u64),
    write: bool,
) -> bool {
    // Intel SDM 3A §7.15, Interrupt 14: P 0, W/R 1, U/S 2, RSVD 3, I/D 4, PK 5.
    let expected = if write { 2 } else { 0 };
    let Some(end) = range.0.checked_add(range.1) else {
        return false;
    };
    cs == 8
        && (window.0..window.1).contains(&rip)
        && flags & AC != 0
        && error & !1 == expected
        && (range.0..end).contains(&cr2)
}

// ====== nmi_valid ======

pub fn nmi_valid(window: u64, user_root: bool, kernel_gs: bool, ac: bool, cs: u64) -> bool {
    let expected = match window {
        1 => (true, false, false),
        2 => (true, true, false),
        3 | 4 => (false, true, false),
        5 => (true, true, false),
        6 => (true, true, true),
        _ => return false,
    };
    cs == 8 && (user_root, kernel_gs, ac) == expected
}

// ====== filter_console ======

pub fn filter_console(bytes: &mut [u8]) {
    let last = bytes.len().saturating_sub(1);
    for (index, byte) in bytes.iter_mut().enumerate() {
        let trailing_newline = index == last && *byte == b'\n';
        if !trailing_newline && !(0x20..0x7f).contains(byte) {
            *byte = b'?';
        }
    }
}

// ====== state_next ======

pub fn state_next(state: u32, next: u32) -> bool {
    matches!(
        (state, next),
        (0, STATE_VALIDATED)
            | (STATE_VALIDATED, STATE_MAPPED)
            | (STATE_VALIDATED, STATE_REAPED)
            | (STATE_MAPPED, STATE_RUNNING)
            | (STATE_MAPPED, STATE_REAPED)
            | (STATE_RUNNING, STATE_TERMINATED)
            | (STATE_TERMINATED, STATE_REAPED)
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== ranges ======

    #[test]
    fn ranges() {
        for (pointer, length) in [(USER_BASE, 0), (USER_END, 0), (USER_END - 256, 256), (0x5fd000, 8)] {
            assert_eq!(copy_range(pointer, length), RANGE_OK);
        }
        for (pointer, length) in [
            (USER_BASE - 1, 1), (0, 0), (USER_END - 1, 2), (USER_END + 1, 0),
            (0xffffffff80000000, 8), (1 << 47, 8), (u64::MAX - 7, 16),
        ] {
            assert_eq!(copy_range(pointer, length), RANGE_BAD);
        }
        assert_eq!(copy_range(USER_BASE, 257), LENGTH_BAD);
        assert_eq!(copy_range(u64::MAX, u64::MAX), LENGTH_BAD);
    }

    // ====== return_frames ======

    #[test]
    fn return_frames() {
        assert!(return_valid(USER_BASE, 0x33, 0x202, 0x600000, 0x2b));
        assert!(return_valid(IMAGE_END - 1, 0x33, 0xfd7, 0, 0x2b));
        assert!(return_valid(USER_BASE, 0x33, 0x202, (1 << 47) - 1, 0x2b));
        for (rip, cs, flags, rsp, ss) in [
            (IMAGE_END, 0x33, 0x202, 0, 0x2b),
            (USER_BASE - 1, 0x33, 0x202, 0, 0x2b),
            (0xffffffff80000000, 0x33, 0x202, 0, 0x2b),
            (1 << 47, 0x33, 0x202, 0, 0x2b),
            (USER_BASE, 8, 0x202, 0, 0x2b),
            (USER_BASE, 0x33, 0x202, 0, 0x10),
            (USER_BASE, 0x33, 0x002, 0, 0x2b),
            (USER_BASE, 0x33, 0x3202, 0, 0x2b),
            (USER_BASE, 0x33, 0x4202, 0, 0x2b),
            (USER_BASE, 0x33, 0x10202, 0, 0x2b),
            (USER_BASE, 0x33, 0x40202, 0, 0x2b),
            (USER_BASE, 0x33, 0x202, 1 << 47, 0x2b),
            (USER_BASE, 0x33, 0x202, u64::MAX, 0x2b),
        ] {
            assert!(!return_valid(rip, cs, flags, rsp, ss));
        }
        assert!(irq_frame_valid(USER_BASE, 0x33, 0x10202, 0, 0x2b));
    }

    // ====== recoverable_faults ======

    #[test]
    fn recoverable_faults() {
        let check = |rip, cs, flags, error, cr2, write| {
            fault_recoverable(rip, cs, flags, error, cr2, (0x1000, 0x1002), (0x5fcff0, 32), write)
        };
        assert!(check(0x1000, 8, AC, 0, 0x5fd000, false));
        assert!(check(0x1001, 8, AC, 3, 0x5fd00f, true));
        for (rip, cs, flags, error, cr2, write) in [
            (0xfff, 8, AC, 0, 0x5fd000, false),
            (0x1002, 8, AC, 0, 0x5fd000, false),
            (0x1000, 0x33, AC, 0, 0x5fd000, false),
            (0x1000, 8, 0, 0, 0x5fd000, false),
            (0x1000, 8, AC, 2, 0x5fd000, false),
            (0x1000, 8, AC, 0, 0x5fd000, true),
            (0x1000, 8, AC, 4, 0x5fd000, false),
            (0x1000, 8, AC, 8, 0x5fd000, false),
            (0x1000, 8, AC, 0x10, 0x5fd000, false),
            (0x1000, 8, AC, 0x20, 0x5fd000, false),
            (0x1000, 8, AC, 0, 0x5fcfef, false),
            (0x1000, 8, AC, 0, 0x5fd010, false),
        ] {
            assert!(!check(rip, cs, flags, error, cr2, write));
        }
        assert!(!fault_recoverable(0, 8, AC, 0, 0, (0, 1), (u64::MAX, 2), false));
    }

    // ====== nmi_windows ======

    #[test]
    fn nmi_windows() {
        for (window, user, gs, ac) in [
            (1, true, false, false), (2, true, true, false), (3, false, true, false),
            (4, false, true, false), (5, true, true, false), (6, true, true, true),
        ] {
            assert!(nmi_valid(window, user, gs, ac, 8));
            assert!(!nmi_valid(window, !user, gs, ac, 8));
            assert!(!nmi_valid(window, user, !gs, ac, 8));
            assert!(!nmi_valid(window, user, gs, !ac, 8));
            assert!(!nmi_valid(window, user, gs, ac, 0x33));
        }
        assert!(!nmi_valid(0, true, false, false, 8));
        assert!(!nmi_valid(7, true, true, true, 8));
    }

    // ====== console ======

    #[test]
    fn console() {
        let mut text = *b"ok\x1b[3\0\n\n";
        filter_console(&mut text);
        assert_eq!(&text, b"ok?[3??\n");
        let mut empty = [0u8; 0];
        filter_console(&mut empty);
    }

    // ====== states ======

    #[test]
    fn states() {
        for path in [[0, 1], [1, 2], [2, 3], [3, 4], [4, 5], [1, 5], [2, 5]] {
            assert!(state_next(path[0], path[1]));
        }
        for path in [[0, 2], [0, 3], [1, 3], [2, 4], [3, 5], [3, 2], [4, 3], [5, 1], [5, 5]] {
            assert!(!state_next(path[0], path[1]));
        }
    }
}
