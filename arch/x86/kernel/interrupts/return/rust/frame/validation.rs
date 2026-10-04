#![cfg_attr(not(test), no_std)]

// ====== tabella completa ring zero nel sottointervallo canonico ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_idtr(base: u64, limit: u64) -> u32 {
    u32::from(limit == 4095 && base >= 4096 && base & 15 == 0 && base <= (1u64 << 47)-4096)
}

// ====== frame dei soli ingressi con ritorno definiti dal profilo iniziale ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_frame(vector: u64, cs: u64, flags: u64, ss: u64) -> u32 {
    u32::from(matches!(vector,0x40 | 0xf0 | 0xff) && cs == 8 && ss == 16 && flags & 2 != 0
        && (vector != 0xf0 || flags & 0x200 == 0) && (vector != 0x40 || flags & 0x200 != 0))
}
