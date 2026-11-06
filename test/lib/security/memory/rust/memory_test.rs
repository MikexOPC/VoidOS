#[path = "../../../../../lib/security/memory/rust/memory.rs"]
mod memory;

use memory::compare::equal;
use memory::wipe::{Wiping, wipe};
use std::mem::{MaybeUninit, size_of};

// ====== supporto ======

fn bytes_after_drop<T>(value: T) -> Vec<u8> {
    let mut storage = MaybeUninit::<T>::uninit();
    storage.write(value);
    unsafe {
        let pointer = storage.as_mut_ptr();
        std::ptr::drop_in_place(pointer);
        std::slice::from_raw_parts(pointer.cast::<u8>(), size_of::<T>()).to_vec()
    }
}

// ====== wipe ======

#[test]
fn wipe_clears_every_supported_type() {
    let mut bytes = [0xffu8; 97];
    wipe(&mut bytes);
    assert!(bytes.iter().all(|byte| *byte == 0));
    let mut words = [u32::MAX; 8];
    wipe(&mut words);
    assert_eq!(words, [0; 8]);
    let mut nested = [[u64::MAX; 3]; 4];
    wipe(&mut nested);
    assert_eq!(nested, [[0; 3]; 4]);
    let mut wide = u128::MAX;
    wipe(&mut wide);
    assert_eq!(wide, 0);
    let (mut small, mut size) = (u16::MAX, usize::MAX);
    wipe(&mut small);
    wipe(&mut size);
    assert_eq!((small, size), (0, 0));
}

#[test]
fn wipe_touches_only_the_given_slice() {
    let mut buffer = [0x5au8; 64];
    wipe(&mut buffer[16..48]);
    assert!(buffer[..16].iter().all(|byte| *byte == 0x5a));
    assert!(buffer[16..48].iter().all(|byte| *byte == 0));
    assert!(buffer[48..].iter().all(|byte| *byte == 0x5a));
    let mut empty: [u8; 0] = [];
    wipe(&mut empty);
    wipe(&mut buffer[64..]);
    assert!(buffer[48..].iter().all(|byte| *byte == 0x5a));
}

// ====== Wiping ======

#[test]
fn wiping_gives_access_and_clears_on_drop() {
    let mut secret = Wiping::new([0x11u8; 32]);
    secret[0] = 0x22;
    assert_eq!(secret[0], 0x22);
    assert_eq!(secret.len(), 32);
    assert!(bytes_after_drop(secret).iter().all(|byte| *byte == 0));
    let words = Wiping::new([u64::MAX; 5]);
    assert!(bytes_after_drop(words).iter().all(|byte| *byte == 0));
}

#[test]
fn plain_values_are_not_wiped_without_the_wrapper() {
    let control = bytes_after_drop([0x33u8; 32]);
    assert!(control.iter().all(|byte| *byte == 0x33));
}

// ====== equal ======

#[test]
fn equal_accepts_identical_slices() {
    assert!(equal(&[], &[]));
    assert!(equal(b"tag", b"tag"));
    let data: Vec<u8> = (0..=255).collect();
    assert!(equal(&data, &data.clone()));
}

#[test]
fn equal_rejects_any_single_bit_difference() {
    let base: Vec<u8> = (0..64u8).map(|value| value.wrapping_mul(37)).collect();
    for position in 0..base.len() {
        for bit in 0..8 {
            let mut changed = base.clone();
            changed[position] ^= 1 << bit;
            assert!(!equal(&base, &changed));
            assert!(!equal(&changed, &base));
        }
    }
}

#[test]
fn equal_rejects_different_lengths() {
    assert!(!equal(b"tag", b"ta"));
    assert!(!equal(b"", b"x"));
    assert!(!equal(&[0u8; 32], &[0u8; 33]));
}

#[test]
fn equal_matches_ordinary_comparison_on_generated_inputs() {
    let mut seed = 0x2026_1107_0000_0003u64;
    for _ in 0..20_000 {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        let length = (seed % 40) as usize;
        let left: Vec<u8> = (0..length).map(|index| (seed >> (index % 8 * 8)) as u8 & 3).collect();
        let right: Vec<u8> = (0..length).map(|index| (seed >> ((index + 3) % 8 * 8)) as u8 & 3).collect();
        assert_eq!(equal(&left, &right), left == right);
    }
}
