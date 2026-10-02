#![cfg_attr(not(test), no_std)]

const VALID: u32 = 0;
const UNSUPPORTED_PROFILE: u32 = 3;
const BAD_SEGMENT: u32 = 5;
const VIRTUAL_LIMIT: u64 = 1 << 47;

// ====== limiti e permessi dei segmenti ELF senza accesso a puntatori ====== //

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
    if !(4096..VIRTUAL_LIMIT).contains(&virtual_address) {
        return BAD_SEGMENT;
    }
    let Some(remaining_virtual) = VIRTUAL_LIMIT.checked_sub(virtual_address) else {
        return BAD_SEGMENT;
    };
    if memory_bytes > remaining_virtual {
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
