#![cfg_attr(not(test), no_std)]

// Intel SDM Vol. 3A 3.3.7.1: le due metà canoniche.
const LOWER_LIMIT: u64 = 1 << 47;
const UPPER_BASE: u64 = 0xFFFF_8000_0000_0000;

// K1: stessa regola di X86_LAYOUT_RANGE in layout.h (una sola metà canonica, non la pagina 0).
fn range(address: u64, bytes: u64) -> bool {
    bytes != 0 && ((address >= 4096 && address < LOWER_LIMIT && bytes <= LOWER_LIMIT - address)
        || (address >= UPPER_BASE && bytes - 1 <= !address))
}

// ====== tabella completa ring zero nel sottointervallo canonico ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_idtr(base: u64, limit: u64) -> u32 {
    u32::from(limit == 4095 && base & 15 == 0 && range(base, 4096))
}

// ====== frame dei soli ingressi con ritorno definiti dal profilo iniziale ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_frame(vector: u64, cs: u64, flags: u64, ss: u64) -> u32 {
    u32::from(matches!(vector,0x40..=0x43 | 0xd1 | 0xf0 | 0xff) && cs == 8 && ss == 16 && flags & 2 != 0
        && (vector != 0xf0 || flags & 0x200 == 0) && (!matches!(vector,0x40..=0x43 | 0xd1) || flags & 0x200 != 0))
}

// ====== x86_irq_saved_area ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_saved_area(frame: u64, floor: u64, top: u64) -> u32 {
    const GPR_BYTES: u64 = 15 * 8;
    const SAVE_BYTES: u64 = 32 + 512;
    const FRAME_BYTES: u64 = 6 * 8;

    let Some(stack_bytes) = top.checked_sub(floor) else { return 0; };
    let Some(gprs) = frame.checked_sub(GPR_BYTES) else { return 0; };
    let Some(saved) = (gprs & !15).checked_sub(SAVE_BYTES) else { return 0; };
    u32::from(frame & 7 == 0 && range(floor, stack_bytes) && saved >= floor &&
        frame.checked_add(FRAME_BYTES).is_some_and(|end| end <= top))
}
