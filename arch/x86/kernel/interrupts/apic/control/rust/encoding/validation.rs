#![cfg_attr(not(test), no_std)]

// ====== registri LVT opzionali dopo il massimo indice architetturale ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_lvt(max: u32, index: u32) -> u32 {
    if !(3..=6).contains(&max) || index > max { return 0; }
    match index { 0 => 0x320, 1 => 0x350, 2 => 0x360, 3 => 0x370,
        4 => 0x340, 5 => 0x330, 6 => 0x2f0, _ => 0 }
}

// ====== NMI locale con polarita MADT e profilo iniziale solo edge ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_nmi_lint(lint: u32, flags: u32) -> u32 {
    if lint > 1 || flags & !15 != 0 || flags & 3 == 2 || ((flags >> 2) & 3) > 1 { return 0; }
    0x400 | if flags & 3 == 3 { 0x2000 } else { 0 }
}
