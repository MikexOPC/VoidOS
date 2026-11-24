#![forbid(unsafe_code)]
// ====== classify ======

pub fn classify(word: u64, length: u32, arguments: u32) -> u32 {
    if length > 256 || arguments > 1 { return u32::MAX; }
    if length == 0 { return if word == 0 && arguments == 0 { 0 } else { u32::MAX }; }
    if length > 8 { return 5; }
    if length < 8 && word >> (length * 8) != 0 { return u32::MAX; }
    let command = match (length, word) {
        (4, 0x706c6568) => 1, // help, little-endian token
        (5, 0x746e697270) => 2, // print
        (6, 0x737574617473) => 3, // status
        (4, 0x74697865) => 4, // exit
        (2, 0x7061) => 7,
        (6, 0x656764697262) => 8, // bridge
        (4, 0x636f7270) => 9, // proc
        (2, 0x6c66) => 10, // fl
        (2, 0x7072) => 11, // rp
        (5, 0x6574697277) => 12, // write
        (2, 0x6d72) => 13, // rm
        (4, 0x636e7973) => 14, // sync
        (3, 0x697567) => 15,
        _ => 5,
    };
    if (arguments != 0 && matches!(command, 1 | 3 | 4 | 9 | 14 | 15)) || (arguments == 0 && matches!(command, 7 | 8 | 11 | 12 | 13)) { 6 } else { command }
}

// ====== count ======

pub fn count(word: u64, length: u32) -> u64 {
    number(word,0,length)
}

// ====== number ======

pub fn number(low: u64, high: u64, length: u32) -> u64 {
    if length == 0 || length > 9 { return u64::MAX; }
    let word = u128::from(low) | (u128::from(high) << 64);
    if word >> (length * 8) != 0 { return u64::MAX; }
    let mut value = 0u64;
    for index in 0..length {
        let digit = (word >> (index * 8)) & 0xff;
        if !(0x30..=0x39).contains(&digit) { return u64::MAX; }
        value = value * 10 + digit as u64 - 0x30;
    }
    if value == 0 || value > 100_000_000 { u64::MAX } else { value }
}

#[cfg(test)]
#[path = "../../../../test/kernel/shell/command/command_test.rs"]
mod tests;
