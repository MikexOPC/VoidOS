#![cfg_attr(not(test), no_std)]
// K1: base of the layout.h windows slot (X86_LAYOUT_WINDOWS_VIRTUAL), checked by Assert-LayoutCopies.
const WINDOWS: u64 = 0xFFFF_F400_0000_0000;
// X86_KERNEL_IST_GUARD and the main stack (X86_KERNEL_MAIN_BASE .. X86_KERNEL_MAIN_TOP).
const GUARD: u64 = WINDOWS + 0x4_0000;
const MAIN_BASE: u64 = WINDOWS + 0x6_1000;
const MAIN_TOP: u64 = WINDOWS + 0x7_1000;
// X86_KERNEL_SYSCALL_BASE .. X86_KERNEL_SYSCALL_TOP: four pages after the guard at WINDOWS + 0x7_4000.
const SYSCALL_BASE: u64 = WINDOWS + 0x7_5000;
const SYSCALL_TOP: u64 = WINDOWS + 0x7_9000;

// ====== tre stack con una guardia prima e dopo ogni intervallo ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_ist_base(stack: u32) -> u64 {
    if !(1..=3).contains(&stack) { return 0; }
    GUARD + (u64::from(stack - 1) * 5 + 1) * 4096
}

// ====== puntatore iniziale un byte oltre l'intervallo scrivibile ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_ist_top(stack: u32) -> u64 {
    let base = x86_ist_base(stack);
    if base == 0 { 0 } else { base + 16384 }
}

// ====== guardie assenti e dodici pagine fisiche senza buchi ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_ist_page(slot: u32) -> u32 {
    if slot >= 16 || slot % 5 == 0 { return 0; }
    slot / 5 * 4 + slot % 5
}

// ====== confini fisici prima di pubblicare una PTE ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_ist_frame(physical: u64, bits: u32) -> u32 {
    x86_stack_frame(1, physical, bits)
}

// ====== profilo IST, stack principale o stack syscall con guardie escluse ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_stack_page(profile: u32, slot: u32) -> u32 {
    match profile {
        1 => x86_ist_page(slot),
        2 if (1..=16).contains(&slot) => slot,
        3 if (1..=4).contains(&slot) => slot,
        _ => 0,
    }
}

// ====== dimensione fisica scelta prima di checked_add ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_stack_frame(profile: u32, physical: u64, bits: u32) -> u32 {
    let pages = match profile { 1 => 12, 2 => 16, 3 => 4, _ => return 0 };
    if !(32..=52).contains(&bits) || physical < 4096 || physical & 4095 != 0 { return 0; }
    u32::from(physical.checked_add(pages * 4096).is_some_and(|end| end <= (1u64 << bits)))
}

// ====== RSP appartenente al nuovo stack senza assumere un indirizzo mappato ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_main_pointer(pointer: u64) -> u32 {
    u32::from((MAIN_BASE..MAIN_TOP).contains(&pointer) && pointer & 7 == 0)
}

// ====== RSP appartenente allo stack delle syscall, cima compresa ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_syscall_pointer(pointer: u64) -> u32 {
    u32::from((SYSCALL_BASE..=SYSCALL_TOP).contains(&pointer) && pointer & 7 == 0)
}
