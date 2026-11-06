#![no_std]

#[allow(dead_code)]
#[path = "../../../../../lib/security/memory/rust/wipe.rs"]
mod wipe;

use core::hint::black_box;

// ====== fill ======

fn fill(secret: &mut [u64; 8], seed: u64) -> u64 {
    for (index, word) in secret.iter_mut().enumerate() {
        *word = black_box(seed ^ index as u64);
    }
    secret.iter().fold(0, |total, word| total.wrapping_add(*word))
}

// ====== probe_volatile ======

#[unsafe(no_mangle)]
pub extern "C" fn probe_volatile(seed: u64) -> u64 {
    let mut secret = [0u64; 8];
    let total = fill(&mut secret, seed);
    wipe::wipe(&mut secret);
    total
}

// ====== probe_plain ======

#[unsafe(no_mangle)]
pub extern "C" fn probe_plain(seed: u64) -> u64 {
    let mut secret = [0u64; 8];
    let total = fill(&mut secret, seed);
    secret.fill(0);
    total
}
