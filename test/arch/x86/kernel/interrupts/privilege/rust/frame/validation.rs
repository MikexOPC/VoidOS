#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_op_in_unsafe_fn)]

// ====== x86_k5_frame_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_k5_frame_valid(
    mode: u64,
    vector: u64,
    error: u64,
    cs: u64,
    flags: u64,
    rsp: u64,
    ss: u64,
) -> u32 {
    // Intel SDM 3A §§2.3, 5.7, 7.14.2: CF/PF/AF/ZF/SF/OF and saved RF may vary.
    const STATUS_AND_RF: u64 = 0x108d5;
    let (expected, base_flags) = match mode {
        1 => ((13, 0), 2),
        2 => ((14, 5), 2),
        3 => ((14, 21), 2),
        4 => ((6, 0), 2),
        5 => ((6, 0), 0x202),
        _ => return 0,
    };
    u32::from(
        (vector, error) == expected
            && cs == 0x33
            && ss == 0x2b
            && flags & !STATUS_AND_RF == base_flags
            && rsp == 0x403000,
    )
}

// ====== x86_k5_irq_frame_valid ======

#[unsafe(no_mangle)]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn x86_k5_irq_frame_valid(
    rip: u64,
    cs: u64,
    flags: u64,
    rsp: u64,
    ss: u64,
    entry: u64,
    first: u64,
    last: u64,
) -> u32 {
    // Intel SDM 3A §§7.14.2, 7.14.4: SS, RSP, RFLAGS, CS, RIP pushed on the RSP0 stack.
    const STATUS_AND_RF: u64 = 0x108d5;
    const RSP0: u64 = 0x409000;
    u32::from(
        first < last
            && rip >= first
            && rip < last
            && cs == 0x33
            && ss == 0x2b
            && flags & !STATUS_AND_RF == 0x202
            && rsp == 0x403000
            && entry == RSP0 - 40,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== four_faults_and_corrupted_frames ======

    #[test]
    fn four_faults_and_corrupted_frames() {
        for (mode, vector, error) in [(1, 13, 0), (2, 14, 5), (3, 14, 21), (4, 6, 0)] {
            for flags in [2, 0x46, 0x10046, 0x108d7] {
                assert_eq!(
                    x86_k5_frame_valid(mode, vector, error, 0x33, flags, 0x403000, 0x2b),
                    1
                );
            }
            for (bad_vector, bad_error, cs, flags, rsp, ss) in [
                (vector ^ 1, error, 0x33, 2, 0x403000, 0x2b),
                (vector, error ^ 1, 0x33, 2, 0x403000, 0x2b),
                (vector, error, 8, 2, 0x403000, 0x2b),
                (vector, error, 0x33, 0x3002, 0x403000, 0x2b),
                (vector, error, 0x33, 2, 0x402ff8, 0x2b),
                (vector, error, 0x33, 2, 0x403000, 0x10),
            ] {
                assert_eq!(
                    x86_k5_frame_valid(mode, bad_vector, bad_error, cs, flags, rsp, ss),
                    0
                );
            }
        }
        assert_eq!(x86_k5_frame_valid(5, 6, 0, 0x33, 2, 0x403000, 0x2b), 0);
        assert_eq!(x86_k5_frame_valid(5, 6, 0, 0x33, 0x202, 0x403000, 0x2b), 1);
        assert_eq!(x86_k5_frame_valid(5, 6, 0, 0x33, 0x10246, 0x403000, 0x2b), 1);
        assert_eq!(x86_k5_frame_valid(6, 6, 0, 0x33, 0x202, 0x403000, 0x2b), 0);
    }

    // ====== irq_frame_from_ring3 ======

    #[test]
    fn irq_frame_from_ring3() {
        let check = |rip, cs, flags, rsp, ss, entry| {
            x86_k5_irq_frame_valid(rip, cs, flags, rsp, ss, entry, 0x400000, 0x400100)
        };
        for flags in [0x202, 0x246, 0xad7, 0x10202] {
            assert_eq!(check(0x400000, 0x33, flags, 0x403000, 0x2b, 0x408fd8), 1);
            assert_eq!(check(0x4000ff, 0x33, flags, 0x403000, 0x2b, 0x408fd8), 1);
        }
        for (rip, cs, flags, rsp, ss, entry) in [
            (0x400100, 0x33, 0x202, 0x403000, 0x2b, 0x408fd8),
            (0x3fffff, 0x33, 0x202, 0x403000, 0x2b, 0x408fd8),
            (0x400010, 0x08, 0x202, 0x403000, 0x2b, 0x408fd8),
            (0x400010, 0x33, 0x002, 0x403000, 0x2b, 0x408fd8),
            (0x400010, 0x33, 0x3202, 0x403000, 0x2b, 0x408fd8),
            (0x400010, 0x33, 0x202, 0x402ff8, 0x2b, 0x408fd8),
            (0x400010, 0x33, 0x202, 0x403000, 0x10, 0x408fd8),
            (0x400010, 0x33, 0x202, 0x403000, 0x2b, 0x408fd0),
            (0x400010, 0x33, 0x202, 0x403000, 0x2b, u64::MAX),
        ] {
            assert_eq!(check(rip, cs, flags, rsp, ss, entry), 0);
        }
        assert_eq!(
            x86_k5_irq_frame_valid(0x400000, 0x33, 0x202, 0x403000, 0x2b, 0x408fd8, 5, 5),
            0
        );
        assert_eq!(
            x86_k5_irq_frame_valid(u64::MAX, 0x33, 0x202, 0x403000, 0x2b, 0x408fd8, 0, u64::MAX),
            0
        );
    }
}
