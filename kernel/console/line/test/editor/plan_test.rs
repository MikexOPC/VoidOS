use super::*;

// ====== tail ======

fn tail(text: &str) -> u32 {
    let bytes = text.as_bytes();
    let mut value = 0;
    for (index, byte) in bytes[bytes.len().saturating_sub(4)..].iter().enumerate() {
        value |= u32::from(*byte) << (index * 8);
    }
    value
}

// ====== append_erase_submit_and_full ======

#[test]
fn append_erase_submit_and_full() {
    assert_eq!(step(0,0,0xe8),(2u64 << 32) | u64::from(APPEND));
    assert_eq!(step(2,tail("è"),0x110001),u64::from(ERASE));
    assert_eq!(step(0,0,0x110001),u64::from(ERASE));
    assert_eq!(step(2,tail("è"),0x110003),(2u64 << 32) | u64::from(SUBMIT));
    assert_eq!(step(0,0,0x110003),u64::from(SUBMIT));
    assert_eq!(step(255,tail("aaaa"),0xe8),(255u64 << 32) | u64::from(FULL));
    assert_eq!(step(256,tail("aaaa"),0x61),(256u64 << 32) | u64::from(FULL));
    assert_eq!(step(254,tail("aaaa"),0xe8),(256u64 << 32) | u64::from(APPEND));
    assert_eq!(step(4,tail("😀"),0x110002),(4u64 << 32) | u64::from(NONE));
}

// ====== invalid_state_and_input ======

#[test]
fn invalid_state_and_input() {
    for (length, bytes) in [(257,0),(u32::MAX,0),(0,1),(1,0x6161),
        (2,0xa8),(2,0xafc0),(3,0x80a0ed),(4,0x808090f4),(1,0x1b),(1,0)] {
        assert_eq!(step(length,bytes,0x61),u64::from(INVALID));
    }
    for scalar in [1,9,10,13,27,127,0x85,0xd800,0xdfff,0x110000,u32::MAX] {
        assert_eq!(step(0,0,scalar),u64::from(INVALID));
    }
}

// ====== all_unicode_final_boundaries ======

#[test]
fn all_unicode_final_boundaries() {
    for scalar in 1..=0x110000 {
        let Some(character) = char::from_u32(scalar) else {
            assert_eq!(step(0,0,scalar),u64::from(INVALID)); continue;
        };
        if character.is_control() {
            assert_eq!(step(0,0,scalar),u64::from(INVALID)); continue;
        }
        let mut bytes = [0;4];
        let text = character.encode_utf8(&mut bytes);
        let count = text.len() as u32;
        assert_eq!(step(0,0,scalar),(u64::from(count) << 32) | u64::from(APPEND));
        assert_eq!(step(count,tail(text),0x110001),u64::from(ERASE));
        let mut prefixed = String::from("abc"); prefixed.push(character);
        assert_eq!(step(3 + count,tail(&prefixed),0x110001),(3u64 << 32) | u64::from(ERASE));
        assert_eq!(step(257 - count,tail("aaaa"),scalar),
            (u64::from(257 - count) << 32) | u64::from(FULL));
    }
}
