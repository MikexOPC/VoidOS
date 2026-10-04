#![cfg_attr(not(test), no_std)]

// ====== foglia dispositivo UC supervisor RW NX con limite fisico CPUID ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_lapic_pte(physical: u64, cache: u64, bits: u32) -> u64 {
    if !(32..=52).contains(&bits) || physical == 0 || physical & 4095 != 0
        || physical >= (1u64 << bits) || cache & !0x98 != 0 { return 0; }
    physical | cache | (1u64 << 63) | 3
}
