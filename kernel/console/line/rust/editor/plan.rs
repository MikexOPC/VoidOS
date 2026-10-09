#![forbid(unsafe_code)]
pub const INVALID: u32 = u32::MAX;
pub const APPEND: u32 = 1;
pub const ERASE: u32 = 2;
pub const SUBMIT: u32 = 3;
pub const NONE: u32 = 4;
pub const FULL: u32 = 5;

// ====== last_length ======

// Unicode17 §3.9 Table3-7. The preceding prefix is caller-owned and valid
// by construction; only the final scalar is validated from the integer tail.
fn last_length(length: u32, tail: u32) -> Option<u32> {
    let count = length.min(4);
    if count == 0 { return (tail == 0).then_some(0); }
    if count < 4 && tail >> (count * 8) != 0 { return None; }
    let mut start = count - 1;
    while start > 0 && (tail >> (start * 8)) & 0xc0 == 0x80 { start -= 1; }
    let first = (tail >> (start * 8)) & 255;
    let width = count - start;
    let mut scalar = match (width, first) {
        (1, 0..=0x7f) => first,
        (2, 0xc2..=0xdf) => first & 31,
        (3, 0xe0..=0xef) => first & 15,
        (4, 0xf0..=0xf4) => first & 7,
        _ => return None,
    };
    for index in start + 1..count {
        let byte = (tail >> (index * 8)) & 255;
        if byte & 0xc0 != 0x80 { return None; }
        scalar = (scalar << 6) | (byte & 63);
    }
    let character = char::from_u32(scalar)?;
    if character.is_control() || character.len_utf8() as u32 != width { return None; }
    Some(width)
}

// ====== step ======

pub fn step(length: u32, tail: u32, input: u32) -> u64 {
    if length > 256 { return u64::from(INVALID); }
    let Some(last) = last_length(length, tail) else { return u64::from(INVALID); };
    let (next, status) = match input {
        0 | 0x110002 => (length, NONE),
        0x110001 => (length - last, ERASE),
        0x110003 => (length, SUBMIT), // Retain until synchronous delivery succeeds.
        _ => {
            let Some(character) = char::from_u32(input) else { return u64::from(INVALID); };
            if character.is_control() { return u64::from(INVALID); }
            let added = character.len_utf8() as u32;
            if added > 256 - length { (length, FULL) }
            else { (length + added, APPEND) }
        }
    };
    (u64::from(next) << 32) | u64::from(status)
}

#[cfg(test)]
#[path = "../../test/editor/plan_test.rs"]
mod tests;
