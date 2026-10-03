#![cfg_attr(not(test), no_std)]

// ====== header MADT con profilo esplicito e limite massimo di lettura ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_madt_header_fields(signature: u32, length: u32, revision: u32) -> u32 {
    u32::from(signature == 0x4349_5041 && (44..=65536).contains(&length) && (1..=6).contains(&revision))
}

// ====== struttura del record verificata prima di qualunque campo ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_madt_record_span(kind: u32, length: u32, remaining: u32) -> u32 {
    if kind > 255 || !(2..=255).contains(&length) || length > remaining { return 0; }
    let expected = match kind { 0 | 3 => 8, 1 | 5 | 10 => 12, 2 => 10, 4 => 6, 9 => 16, _ => return 1 };
    u32::from(length == expected)
}

// ====== solo i campi definiti di polarita e trigger sono interpretati ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_madt_interrupt_flags(flags: u32) -> u32 {
    u32::from(flags & 3 != 2 && (flags >> 2) & 3 != 2)
}

// ====== CPU normalizzate senza confondere disponibilita e hot enable ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_madt_cpu_fields(uid: u32, id: u32, flags: u32, kind: u32) -> u32 {
    u32::from(matches!(flags, 1 | 2) && (kind == 9 || (kind == 0 && uid <= 255 && id <= 255)))
}
