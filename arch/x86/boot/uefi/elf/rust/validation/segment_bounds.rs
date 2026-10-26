#![cfg_attr(not(test), no_std)]

const VALID: u32 = 0;
const UNSUPPORTED_PROFILE: u32 = 3;
const BAD_SEGMENT: u32 = 5;
const VIRTUAL_LIMIT: u64 = 1 << 47;
// Intel SDM Vol. 3A 3.3.7.1: start of the upper canonical half.
const UPPER_BASE: u64 = 0xFFFF_8000_0000_0000;

// K1: same rule as X86_LAYOUT_RANGE in arch/x86/configs/layout.h (one canonical half, not page 0).
fn virtual_range(address: u64, bytes: u64) -> bool {
    bytes != 0 && ((address >= 4096 && address < VIRTUAL_LIMIT && bytes <= VIRTUAL_LIMIT - address)
        || (address >= UPPER_BASE && bytes - 1 <= !address))
}

// ====== elf_check_load_segment ======

#[unsafe(no_mangle)]
pub extern "C" fn elf_check_load_segment(
    flags: u32,
    offset: u64,
    virtual_address: u64,
    file_bytes: u64,
    memory_bytes: u64,
    alignment: u64,
    file_size: u64,
) -> u32 {
    let Some(remaining_file) = file_size.checked_sub(offset) else {
        return BAD_SEGMENT;
    };
    if memory_bytes == 0 || file_bytes > memory_bytes || file_bytes > remaining_file {
        return BAD_SEGMENT;
    }
    // K1: the kernel image may be linked in either canonical half.
    if !virtual_range(virtual_address, memory_bytes) {
        return BAD_SEGMENT;
    }
    if flags & !7 != 0 || flags & 4 == 0 || flags & 3 == 3 {
        return UNSUPPORTED_PROFILE;
    }
    if virtual_address & 4095 != offset & 4095 {
        return BAD_SEGMENT;
    }
    if alignment > 1 {
        if !alignment.is_power_of_two() {
            return BAD_SEGMENT;
        }
        let mask = alignment - 1;
        if virtual_address & mask != offset & mask {
            return BAD_SEGMENT;
        }
    }
    VALID
}

// ====== elf_check_staging_range ======

// Intel SDM Vol. 3A, Section 4.5: lower 4-level canonical half ends at 2^47.
// UEFI 2.10, Section 7.2.1: allocation size is Pages * 4096 bytes.
#[unsafe(no_mangle)]
pub extern "C" fn elf_check_staging_range(address: u64, pages: u64) -> u32 {
    if !(4096..VIRTUAL_LIMIT).contains(&address) || address & 4095 != 0 || pages == 0 {
        return 0;
    }
    let Some(bytes) = pages.checked_mul(4096) else {
        return 0;
    };
    let Some(end) = address.checked_add(bytes) else {
        return 0;
    };
    u32::from(end <= VIRTUAL_LIMIT)
}
