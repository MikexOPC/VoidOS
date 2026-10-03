#![cfg_attr(not(test), no_std)]

// ====== profilo del LAPIC integrato senza interpretare bit riservati ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_apic_lapic_version(version: u32) -> u32 {
    u32::from((0x10..=0x1f).contains(&(version & 255)) && (3..=6).contains(&((version >> 16) & 255)))
}

// ====== massimo indice convertito in conteggio con budget iniziale esplicito ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_apic_io_inputs(version: u32) -> u32 {
    let inputs = ((version >> 16) & 255) + 1;
    // Massimo 120: registri redirection low/high entro il selettore a 8 bit.
    if matches!(version & 255, 0x11 | 0x20) && inputs <= 120 { inputs } else { 0 }
}

// ====== GSI finale esclusivo a 64 bit senza wrap a zero ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_apic_gsi_end(base: u32, inputs: u32) -> u64 {
    if !(1..=120).contains(&inputs) { return 0; }
    let end = u64::from(base) + u64::from(inputs);
    if end <= 1u64 << 32 { end } else { 0 }
}
