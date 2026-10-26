#![cfg_attr(not(test), no_std)]

const LIMIT: u64 = 1 << 47;
// Intel SDM Vol. 3A 3.3.7.1: start of the upper canonical half.
const UPPER: u64 = 0xFFFF_8000_0000_0000;

// K1: same rule as X86_LAYOUT_RANGE in arch/x86/configs/layout.h (one canonical half, not page 0).
fn range(address: u64, bytes: u64) -> bool {
    bytes != 0 && ((address >= PAGE && address < LIMIT && bytes <= LIMIT - address)
        || (address >= UPPER && bytes - 1 <= !address))
}
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
    if (580..640).contains(&offset) { return 0xcc; }
    if offset >= 640 { return activation_byte(offset - 640); }
    match offset - STOP {
        0 => 0xfa, 1 => 0xf4, 2 => 0xeb, 3 => 0xfd,
        _ => 256,
    }
}

// ====== controlli del profilo CPU supportato prima di ExitBootServices ====== //

#[unsafe(no_mangle)]
pub extern "C" fn boot_activation_check_state(cr4: u64, root: u64, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) || cr4 & 0x0182_1000 != 0
        || root < 4096 || root & 4095 != 0 || root >= (1u64 << bits) { return 0; }
    1
}

// ====== intervalli del tratto finale con aritmetica controllata ====== //

#[unsafe(no_mangle)]
pub extern "C" fn boot_capsule_check_addresses(
    code: u64, entry: u64, stack_top: u64, contract: u64, stack_bytes: u64,
) -> u32 {
    if !(PAGE..=LIMIT - PAGE).contains(&code) || code & (PAGE - 1) != 0
        // K1: entry in either half; the last byte is refused because the overlaps use entry + 1.
        || !range(entry, 1) || entry == u64::MAX
        || !range(contract, PAGE) || contract & (PAGE - 1) != 0
        || stack_top & 15 != 0
        || stack_bytes == 0 || stack_bytes > 65536 || stack_bytes & (PAGE - 1) != 0 {
        return 0;
    }
    let Some(stack_base) = stack_top.checked_sub(stack_bytes) else { return 0; };
    // The former check kept stack_top strictly below the half: one byte of margin.
    if !range(stack_base, stack_bytes + 1) || stack_base & (PAGE - 1) != 0 { return 0; }
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

// ====== profilo macchina del tratto di attivazione verificato con disassembly ====== //

fn activation_byte(offset: u32) -> u32 {
    const CODE: [u8; 191] = [
        0xfa, 0xfc, 0x4c, 0x8b, 0xe1, 0x0f, 0x20, 0xe0, 0xa9, 0x00, 0x10, 0x82,
        0x01, 0x0f, 0x85, 0xa3, 0x00, 0x00, 0x00, 0xb9, 0x80, 0x00, 0x00, 0xc0,
        0x0f, 0x32, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x0f, 0x30, 0x0f, 0x20, 0xc0,
        0x48, 0x0d, 0x00, 0x00, 0x01, 0x00, 0x0f, 0x22, 0xc0, 0x49, 0x8b, 0x44,
        0x24, 0x28, 0x0f, 0x01, 0x10, 0x66, 0xb8, 0x18, 0x00, 0x0f, 0x00, 0xd8,
        0x49, 0x8b, 0x44, 0x24, 0x30, 0x0f, 0x01, 0x18, 0x6a, 0x08, 0x48, 0x8d,
        0x05, 0x03, 0x00, 0x00, 0x00, 0x50, 0x48, 0xcb, 0x66, 0xb8, 0x10, 0x00,
        0x66, 0x8e, 0xd8, 0x66, 0x8e, 0xc0, 0x66, 0x8e, 0xd0, 0x0f, 0x20, 0xc0,
        0xa9, 0x00, 0x00, 0x01, 0x00, 0x74, 0x4f, 0xb9, 0x80, 0x00, 0x00, 0xc0,
        0x0f, 0x32, 0xa9, 0x00, 0x08, 0x00, 0x00, 0x74, 0x41, 0x49, 0x8b, 0x44,
        0x24, 0x18, 0x48, 0xc7, 0x40, 0x18, 0x03, 0x00, 0x00, 0x00, 0x0f, 0x20,
        0xe0, 0x48, 0x25, 0x7f, 0xff, 0xff, 0xff, 0x0f, 0x22, 0xe0, 0x49, 0x8b,
        0x04, 0x24, 0x0f, 0x22, 0xd8, 0x49, 0x8b, 0x64, 0x24, 0x10, 0x0f, 0x20,
        0xd8, 0x49, 0x3b, 0x04, 0x24, 0x75, 0x13, 0x48, 0x83, 0xec, 0x20, 0x49,
        0x8b, 0x4c, 0x24, 0x20, 0x41, 0xff, 0x54, 0x24, 0x08, 0xe9, 0x05, 0x00,
        0x00, 0x00, 0xb8, 0xff, 0xff, 0x07, 0xb0, 0xfa, 0xf4, 0xeb, 0xfd,
    ];
    if offset < CODE.len() as u32 { u32::from(CODE[offset as usize]) } else { 256 }
}
