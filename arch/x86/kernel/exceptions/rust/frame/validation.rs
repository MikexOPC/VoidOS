#![cfg_attr(not(test), no_std)]

// ====== finestra dell'immagine del kernel ===== //

// K1: stesso valore di X86_KERNEL_IMAGE_BASE in arch/x86/configs/layout.h (unica fonte);
// kernel/scripts/build.ps1 confronta le due definizioni a ogni build (Assert-LayoutCopies).
const KERNEL_IMAGE_BASE: u64 = 0x0000_0001_0000_0000;
// Intel SDM Vol. 3A 3.3.7.1: fine della metà bassa canonica (limite architetturale, non di layout).
const LOWER_CANONICAL_LIMIT: u64 = 1 << 47;

// ====== ingressi interamente contenuti nel codice dei terminali ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_exception_target(address: u64, begin: u64, end: u64) -> u32 {
    u32::from(begin >= KERNEL_IMAGE_BASE && begin < end && end <= LOWER_CANONICAL_LIMIT
        && address >= begin && address < end)
}

// ====== vettori con error code CPU incluso profilo AMD riservato ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_exception_error(vector: u64) -> u32 {
    u32::from(matches!(vector, 8 | 10 | 11 | 12 | 13 | 14 | 17 | 21 | 29 | 30))
}

// ====== controllo del frame terminale senza dereferenziare puntatori C ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_exception_frame(vector: u64, error: u64, cs: u64, flags: u64, ss: u64) -> u32 {
    u32::from(vector < 32 && cs == 8 && ss == 16 && flags & 2 != 0
        && (x86_exception_error(vector) != 0 || error == 0)
        && (vector != 8 || error == 0))
}

// ====== la tabella completa deve restare nel sottointervallo canonico ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_exception_idtr(base: u64, limit: u64) -> u32 {
    u32::from(limit == 511 && base >= 4096 && base & 15 == 0
        && base <= (1 << 47) - 512)
}
