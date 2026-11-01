#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

// ====== x86_ap_low_geometry ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn x86_ap_low_geometry(base: u64, pages: u64) -> u32 {
    u32::from(pages == 1 && (0x1000..=0xff000).contains(&base) && base & 0xfff == 0)
}
