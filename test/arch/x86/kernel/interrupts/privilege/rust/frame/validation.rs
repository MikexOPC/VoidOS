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
    let expected = match mode {
        1 => (13, 0),
        2 => (14, 5),
        3 => (14, 21),
        4 => (6, 0),
        _ => return 0,
    };
    u32::from(
        (vector, error) == expected
            && cs == 0x33
            && ss == 0x2b
            && flags & !STATUS_AND_RF == 2
            && rsp == 0x403000,
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
    }
}
