#![cfg_attr(not(test), no_std)]

const LIMIT: u64 = 1 << 47;
const PAGE: u64 = 4096;
const VECTOR_BASE: u32 = 64;
const STOP: u32 = 576;

// ====== oracolo del profilo macchina senza accesso a puntatori ====== //

#[unsafe(no_mangle)]
pub extern "C" fn boot_capsule_expected_byte(offset: u32) -> u32 {
    const TAIL: [u8; 27] = [
        0xfa, 0xfc, 0x48, 0x8b, 0xc1, 0x48, 0x8b, 0xe2,
        0x48, 0x83, 0xec, 0x28, 0x48, 0xc7, 0x04, 0x24,
        0, 0, 0, 0, 0x49, 0x8b, 0xc8, 0x33, 0xed, 0xff, 0xe0,
    ];
    if offset < TAIL.len() as u32 {
        return u32::from(TAIL[offset as usize]);
    }
    if offset < VECTOR_BASE { return 0xcc; }
    if offset < STOP {
        let vector = (offset - VECTOR_BASE) / 16;
        let byte = (offset - VECTOR_BASE) % 16;
        let displacement = STOP - (VECTOR_BASE + vector * 16 + 10);
        return match byte {
            0 => 0xb8,
            1..=4 => (vector >> ((byte - 1) * 8)) & 255,
            5 => 0xe9,
            6..=9 => (displacement >> ((byte - 6) * 8)) & 255,
            _ => 0xcc,
        };
    }
    match offset - STOP {
        0 => 0xfa, 1 => 0xf4, 2 => 0xeb, 3 => 0xfd,
        _ => 256,
    }
}

// ====== intervalli del tratto finale con aritmetica controllata ====== //

#[unsafe(no_mangle)]
pub extern "C" fn boot_capsule_check_addresses(
    code: u64, entry: u64, stack_top: u64, contract: u64, stack_bytes: u64,
) -> u32 {
    if !(PAGE..=LIMIT - PAGE).contains(&code) || code & (PAGE - 1) != 0
        || !(PAGE..LIMIT).contains(&entry)
        || !(PAGE..=LIMIT - PAGE).contains(&contract) || contract & (PAGE - 1) != 0
        || stack_top >= LIMIT || stack_top & 15 != 0
        || stack_bytes == 0 || stack_bytes > 65536 || stack_bytes & (PAGE - 1) != 0 {
        return 0;
    }
    let Some(stack_base) = stack_top.checked_sub(stack_bytes) else { return 0; };
    if stack_base < PAGE || stack_base & (PAGE - 1) != 0 { return 0; }
    let overlaps = |base: u64, end: u64, other: u64, other_end: u64| base < other_end && other < end;
    if overlaps(code, code + PAGE, stack_base, stack_top)
        || overlaps(contract, contract + PAGE, stack_base, stack_top)
        || overlaps(code, code + PAGE, contract, contract + PAGE)
        || overlaps(entry, entry + 1, code, code + PAGE)
        || overlaps(entry, entry + 1, contract, contract + PAGE)
        || overlaps(entry, entry + 1, stack_base, stack_top) {
        return 0;
    }
    1
}
