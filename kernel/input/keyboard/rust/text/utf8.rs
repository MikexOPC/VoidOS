#![forbid(unsafe_code)]

// ====== encode ======

// Unicode17.0 §3.9, Table3-7: shortest UTF-8, no surrogate or >10FFFF.
// The char validation precedes encode_utf8; four-byte local buffer only.
pub fn encode(scalar: u32) -> u64 {
    let Some(character) = char::from_u32(scalar) else { return 0; };
    let mut buffer = [0u8; 4];
    let text = character.encode_utf8(&mut buffer);
    let mut packed = 0u32;
    for (index, byte) in text.bytes().enumerate() {
        packed |= u32::from(byte) << (index * 8);
    }
    ((text.len() as u64) << 32) | u64::from(packed)
}

#[cfg(test)]
#[path = "../../test/text/utf8_test.rs"]
mod tests;
