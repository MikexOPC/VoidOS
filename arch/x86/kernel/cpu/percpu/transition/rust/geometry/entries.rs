#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

// Intel SDM 253668-078US 4.5.4/4.6: address[51:12], P0, RW1, U/S2, XD63.
// Low code RO/X and separate data RW/NX; no user access, PAT0 verified WB.
const ADDRESS: u64 = 0x000f_ffff_ffff_f000;
const NX: u64 = 1 << 63;

// ====== xt_geometry ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xt_geometry(code: u64, data: u64, tables: u64) -> u32 {
    let low = |p: u64| (0x1000..=0xff000).contains(&p) && p & 4095 == 0;
    u32::from(low(code) && low(data) && code != data && tables >= 4096 && tables & 4095 == 0
        && tables <= 0x1_0000_0000 - 16384
        && !(tables..tables + 16384).contains(&code)
        && !(tables..tables + 16384).contains(&data))
}

// ====== xt_source_entry ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xt_source_entry(entry: u64, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) { return 0; }
    u32::from(entry == 0 || (entry & 1 != 0 && entry & !(ADDRESS | NX | 0xe7b) == 0
        && entry & ADDRESS != 0 && entry & ADDRESS < (1_u64 << bits)))
}

// ====== xt_entry ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xt_entry(table: u32, index: u32, tables: u64, code: u64,
    data: u64, source: u64, recursive: u32) -> u64 {
    if xt_geometry(code, data, tables) == 0 || table > 3 || index >= 512
        || !(256..512).contains(&recursive) { return u64::MAX; }
    match table {
        0 if index == 0 => (tables + 4096) | 3,
        0 if index >= 256 && index != recursive => source,
        1 if index == 0 => (tables + 8192) | 3,
        2 if index == 0 => (tables + 12288) | 3,
        3 if u64::from(index) == code >> 12 => code | 1,
        3 if u64::from(index) == data >> 12 => data | NX | 3,
        _ => 0,
    }
}
