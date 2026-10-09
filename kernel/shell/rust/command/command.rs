#![forbid(unsafe_code)]
// ====== classify ======

pub fn classify(word: u64, length: u32, arguments: u32) -> u32 {
    if length > 256 || arguments > 1 { return u32::MAX; }
    if length == 0 { return if word == 0 && arguments == 0 { 0 } else { u32::MAX }; }
    if length > 8 { return 5; }
    if length < 8 && word >> (length * 8) != 0 { return u32::MAX; }
    let command = match (length, word) {
        (4, 0x706c6568) => 1, // help, little-endian token
        (4, 0x6f686365) => 2, // echo
        (6, 0x737574617473) => 3, // status
        (4, 0x74697865) => 4, // exit
        _ => 5,
    };
    if arguments != 0 && matches!(command, 1 | 3 | 4) { 6 } else { command }
}

#[cfg(test)]
#[path = "../../test/command/command_test.rs"]
mod tests;
