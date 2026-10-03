#[path = "../../../../../../arch/x86/kernel/interrupts/apic/rust/profile/validation.rs"]
mod validation;
use validation::*;

// ====== bit APIC BSP enable e modalita estesa ===== //

#[test]
fn apic_modes() {
    let edx = (1 << 5) | (1 << 9) | (1 << 16);
    assert_eq!(x86_apic_mode(0xfee0_0900, 0, edx, 36), 1);
    assert_eq!(x86_apic_mode(0xfee0_0d00, 1 << 21, edx, 36), 2);
    for (base, ecx) in [(0xfee0_0100,0),(0xfee0_0800,0),(0xfee0_0500,1<<21),(0xfee0_0d00,0)] {
        assert_eq!(x86_apic_mode(base, ecx, edx, 36), 0);
    }
    for feature in [5,9,16] { assert_eq!(x86_apic_mode(0xfee0_0900, 0, edx & !(1<<feature), 36),0); }
    for bits in [0,31,53,64,u32::MAX] { assert_eq!(x86_apic_mode(0xfee0_0900,0,edx,bits),0); }
}

// ====== ogni byte PAT usa soltanto tipi architetturali ===== //

#[test]
fn pat_entries() {
    for slot in 1..8 {
        for byte in 0..=255u64 {
            let pat = (0x0007_0406_0007_0406 & !(255 << (slot*8))) | (byte << (slot*8));
            if !matches!(byte,0|1|4|5|6|7) { assert_eq!(x86_apic_uc_slot(pat),0); }
        }
        let pat = 0x0606_0606_0606_0606 & !(255 << (slot*8));
        assert_eq!(x86_apic_uc_slot(pat),slot+1);
    }
    assert_eq!(x86_apic_uc_slot(0x0707_0707_0707_0706),0);
    assert_eq!(x86_apic_uc_slot(0x0007_0406_0007_0400),0);
}

// ====== indice PAT tradotto nei soli bit PAT PCD PWT della foglia 4 KiB ===== //

#[test]
fn leaf_cache_bits() {
    for (index, flags) in [0,8,16,24,128,136,144,152].into_iter().enumerate() {
        assert_eq!(x86_apic_cache_bits(index as u32),flags);
    }
    for index in [8,9,u32::MAX] { assert_eq!(x86_apic_cache_bits(index),u64::MAX); }
}
