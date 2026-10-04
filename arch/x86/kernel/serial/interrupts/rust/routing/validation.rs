#![cfg_attr(not(test), no_std)]

// ====== rotta ISA fixed supervisor con trigger e polarita MADT ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_uart_route_bits(flags: u32) -> u32 {
    if flags > 15 || flags & 3 == 2 || (flags >> 2) & 3 == 2 { return 0; }
    0x40 | if flags & 3 == 3 { 0x2000 } else { 0 }
        | if (flags >> 2) & 3 == 3 { 0x8000 } else { 0 }
}

// ====== THRE con FIFO disabilitata oppure 16550A funzionante ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_uart_thre(iir: u32) -> u32 {
    u32::from(matches!(iir, 0x02 | 0xc2))
}
