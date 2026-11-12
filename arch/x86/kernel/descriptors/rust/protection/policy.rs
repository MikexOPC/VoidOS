#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_op_in_unsafe_fn)]

const CR4_UMIP: u64 = 1 << 11;
const CR4_SMEP: u64 = 1 << 20;
const CR4_SMAP: u64 = 1 << 21;
const CPUID_SMEP: u32 = 1 << 7;
const CPUID_SMAP: u32 = 1 << 20;
const CPUID_UMIP: u32 = 1 << 2;
const REQUIRED: u64 = CR4_SMEP | CR4_SMAP;

// ====== x86_user_protection_bits ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_user_protection_bits(max_leaf: u32, leaf7_ebx: u32, leaf7_ecx: u32) -> u64 {
    // Intel SDM 3A §2.5: CR4.UMIP bit 11, CR4.SMEP bit 20, CR4.SMAP bit 21.
    // Intel SDM 2A, CPUID: CPUID.07H.00H:EBX SMEP bit 7, SMAP bit 20; ECX UMIP bit 2.
    if max_leaf < 7 {
        return 0;
    }
    let smep = if leaf7_ebx & CPUID_SMEP != 0 { CR4_SMEP } else { 0 };
    let smap = if leaf7_ebx & CPUID_SMAP != 0 { CR4_SMAP } else { 0 };
    let umip = if leaf7_ecx & CPUID_UMIP != 0 { CR4_UMIP } else { 0 };
    smep | smap | umip
}

// ====== x86_user_mode_permitted ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_user_mode_permitted(bits: u64) -> u32 {
    u32::from(bits & !CR4_UMIP == REQUIRED)
}

#[cfg(test)]
mod tests {
    use super::*;

    // ====== bits_follow_cpuid ======

    #[test]
    fn bits_follow_cpuid() {
        let both = CPUID_SMEP | CPUID_SMAP;
        assert_eq!(x86_user_protection_bits(7, both, 0), REQUIRED);
        assert_eq!(x86_user_protection_bits(7, both, CPUID_UMIP), REQUIRED | CR4_UMIP);
        assert_eq!(x86_user_protection_bits(0x20, CPUID_SMEP, 0), CR4_SMEP);
        assert_eq!(x86_user_protection_bits(7, CPUID_SMAP, 0), CR4_SMAP);
        assert_eq!(x86_user_protection_bits(7, 0, CPUID_UMIP), CR4_UMIP);
        assert_eq!(x86_user_protection_bits(7, 0, 0), 0);
        assert_eq!(x86_user_protection_bits(7, !both, !CPUID_UMIP), 0);
        assert_eq!(x86_user_protection_bits(6, u32::MAX, u32::MAX), 0);
        assert_eq!(x86_user_protection_bits(0, u32::MAX, u32::MAX), 0);
    }

    // ====== smep_and_smap_are_required ======

    #[test]
    fn smep_and_smap_are_required() {
        assert_eq!(x86_user_mode_permitted(REQUIRED), 1);
        assert_eq!(x86_user_mode_permitted(REQUIRED | CR4_UMIP), 1);
        for bits in [0, CR4_UMIP, CR4_SMEP, CR4_SMAP, CR4_SMEP | CR4_UMIP, REQUIRED | 1, u64::MAX] {
            assert_eq!(x86_user_mode_permitted(bits), 0);
        }
    }
}
