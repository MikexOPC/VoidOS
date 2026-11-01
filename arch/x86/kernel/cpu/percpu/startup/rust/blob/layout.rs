#![deny(unsafe_code)]

// ====== xs_blob_layout ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xs_blob_layout(bytes: u32, entry32: u32, entry64: u32, gdt: u32,
    nmi16: u32, fail16: u32, fail32: u32, fail64: u32) -> u32 {
    u32::from(bytes <= 4096 && gdt <= 4096 - 32 && gdt + 32 == bytes && gdt & 7 == 0
        && 32 < entry32 && entry32 < entry64 && entry64 < gdt
        && (32..entry32).contains(&nmi16) && (32..entry32).contains(&fail16)
        && nmi16 != fail16 && (entry32..entry64).contains(&fail32)
        && (entry64..gdt).contains(&fail64))
}
