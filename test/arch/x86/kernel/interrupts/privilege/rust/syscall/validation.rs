#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_op_in_unsafe_fn)]

const USER_CODE: u64 = 0x400000;
const USER_CODE_END: u64 = 0x401000;
const USER_STACK: u64 = 0x402000;
const USER_STACK_END: u64 = 0x403000;

// ====== x86_k5_sys_number ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_k5_sys_number(number: u64) -> u32 {
    match number {
        0 => 1,
        1 => 2,
        _ => 0,
    }
}

// ====== x86_k5_sys_frame_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_k5_sys_frame_valid(rip: u64, cs: u64, flags: u64, rsp: u64, ss: u64) -> u32 {
    // Intel SDM 3A §2.3: CF, PF, AF, ZF, SF, TF, IF, DF and OF may return to CPL3.
    const USER_FLAGS: u64 = 0xfd5;
    const FIXED_AND_IF: u64 = 0x202;
    u32::from(
        (USER_CODE..USER_CODE_END).contains(&rip)
            && cs == 0x33
            && ss == 0x2b
            && flags & !USER_FLAGS == 2
            && flags & FIXED_AND_IF == FIXED_AND_IF
            && (USER_STACK..=USER_STACK_END).contains(&rsp),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== numbers_and_boundaries ======

    #[test]
    fn numbers_and_boundaries() {
        assert_eq!(x86_k5_sys_number(0), 1);
        assert_eq!(x86_k5_sys_number(1), 2);
        for number in [2, 99, u32::MAX as u64 + 1, u64::MAX] {
            assert_eq!(x86_k5_sys_number(number), 0);
        }
    }

    // ====== accepted_frames ======

    #[test]
    fn accepted_frames() {
        for flags in [0x202, 0x246, 0x302, 0x602, 0xad7] {
            assert_eq!(x86_k5_sys_frame_valid(0x400000, 0x33, flags, 0x403000, 0x2b), 1);
            assert_eq!(x86_k5_sys_frame_valid(0x400fff, 0x33, flags, 0x402000, 0x2b), 1);
        }
    }

    // ====== rejected_frames ======

    #[test]
    fn rejected_frames() {
        let good = (0x400010, 0x33, 0x202, 0x403000, 0x2b);
        let cases = [
            (0x401000, good.1, good.2, good.3, good.4),
            (0x3fffff, good.1, good.2, good.3, good.4),
            (0x0000800000000000, good.1, good.2, good.3, good.4),
            (0xffffffff80000000, good.1, good.2, good.3, good.4),
            (good.0, 0x08, good.2, good.3, good.4),
            (good.0, 0x2b, good.2, good.3, good.4),
            (good.0, good.1, 0x002, good.3, good.4),
            (good.0, good.1, 0x3202, good.3, good.4),
            (good.0, good.1, 0x4202, good.3, good.4),
            (good.0, good.1, 0x10202, good.3, good.4),
            (good.0, good.1, 0x20202, good.3, good.4),
            (good.0, good.1, 0x40202, good.3, good.4),
            (good.0, good.1, 0x200, good.3, good.4),
            (good.0, good.1, good.2, 0x403001, good.4),
            (good.0, good.1, good.2, 0x401fff, good.4),
            (good.0, good.1, good.2, 0xffffffff80000000, good.4),
            (good.0, good.1, good.2, good.3, 0x10),
        ];
        for (rip, cs, flags, rsp, ss) in cases {
            assert_eq!(x86_k5_sys_frame_valid(rip, cs, flags, rsp, ss), 0);
        }
    }
}
