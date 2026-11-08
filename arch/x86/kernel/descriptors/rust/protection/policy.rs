#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_op_in_unsafe_fn)]

const CR4_SMEP: u64 = 1 << 20;
const CR4_SMAP: u64 = 1 << 21;
const CPUID_SMEP: u32 = 1 << 7;
const CPUID_SMAP: u32 = 1 << 20;

// ====== x86_user_protection_bits ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_user_protection_bits(max_leaf: u32, leaf7_ebx: u32) -> u64 {
    // Intel SDM 3A §2.5: CPUID.07H.00H:EBX SMEP bit 7, SMAP bit 20; CR4 bits 20 and 21.
    if max_leaf < 7 {
        return 0;
    }
    let smep = if leaf7_ebx & CPUID_SMEP != 0 { CR4_SMEP } else { 0 };
    let smap = if leaf7_ebx & CPUID_SMAP != 0 { CR4_SMAP } else { 0 };
    smep | smap
}

// ====== x86_user_mode_permitted ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_user_mode_permitted(bits: u64) -> u32 {
    u32::from(bits == CR4_SMEP | CR4_SMAP)
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== bits_follow_cpuid ======

    #[test]
    fn bits_follow_cpuid() {
        assert_eq!(x86_user_protection_bits(7, CPUID_SMEP | CPUID_SMAP), CR4_SMEP | CR4_SMAP);
        assert_eq!(x86_user_protection_bits(0x20, CPUID_SMEP), CR4_SMEP);
        assert_eq!(x86_user_protection_bits(7, CPUID_SMAP), CR4_SMAP);
        assert_eq!(x86_user_protection_bits(7, 0), 0);
        assert_eq!(x86_user_protection_bits(7, !(CPUID_SMEP | CPUID_SMAP)), 0);
        assert_eq!(x86_user_protection_bits(6, u32::MAX), 0);
        assert_eq!(x86_user_protection_bits(0, u32::MAX), 0);
    }

    // ====== both_are_required ======

    #[test]
    fn both_are_required() {
        assert_eq!(x86_user_mode_permitted(CR4_SMEP | CR4_SMAP), 1);
        for bits in [0, CR4_SMEP, CR4_SMAP, CR4_SMEP | CR4_SMAP | 1, u64::MAX] {
            assert_eq!(x86_user_mode_permitted(bits), 0);
        }
    }
}
