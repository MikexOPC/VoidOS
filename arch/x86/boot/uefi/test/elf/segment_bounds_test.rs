#[path = "../../elf/segment_bounds.rs"]
mod segment_bounds;

// ====== prove dei confini numerici e dei permessi ====== //

#[cfg(test)]
mod tests {
    use super::segment_bounds::elf_check_load_segment;
    const VALID: u32 = 0;
    const UNSUPPORTED_PROFILE: u32 = 3;
    const BAD_SEGMENT: u32 = 5;
    const VIRTUAL_LIMIT: u64 = 1 << 47;

    #[test]
    fn accepts_rx_and_rw_segments() {
        assert_eq!(
            elf_check_load_segment(5, 256, 0x100100, 16, 64, 256, 512),
            VALID
        );
        assert_eq!(
            elf_check_load_segment(6, 384, 0x200180, 8, 64, 128, 512),
            VALID
        );
    }

    #[test]
    fn rejects_overflow_truncation_and_invalid_addresses() {
        for (offset, address, file_bytes, memory_bytes) in [
            (u64::MAX, 0x100100, 16, 64),
            (256, u64::MAX, 16, 64),
            (256, 0x100100, 257, 300),
            (256, 0x100100, 65, 64),
            (256, 0x100100, 0, 0),
            (256, 0x100100, 16, u64::MAX),
            (0, VIRTUAL_LIMIT, 0, 1),
            (0, 0, 0, 1),
        ] {
            assert_eq!(
                elf_check_load_segment(5, offset, address, file_bytes, memory_bytes, 1, 512),
                BAD_SEGMENT
            );
        }
    }

    #[test]
    fn rejects_writable_code_and_alignment_mismatch() {
        for flags in [0, 1, 2, 3, 7, 8, u32::MAX] {
            assert_eq!(
                elf_check_load_segment(flags, 256, 0x100100, 16, 64, 1, 512),
                UNSUPPORTED_PROFILE
            );
        }
        for alignment in [3, 255, 0x200000, u64::MAX] {
            assert_eq!(
                elf_check_load_segment(5, 256, 0x100100, 16, 64, alignment, 512),
                BAD_SEGMENT
            );
        }
    }

    #[test]
    fn accepts_exact_exclusive_limits() {
        assert_eq!(
            elf_check_load_segment(4, 0, VIRTUAL_LIMIT - 4096, 4096, 4096, 4096, 4096),
            VALID
        );
        assert_eq!(
            elf_check_load_segment(4, 0, VIRTUAL_LIMIT - 4096, 4096, 4097, 4096, 4096),
            BAD_SEGMENT
        );
    }
}
