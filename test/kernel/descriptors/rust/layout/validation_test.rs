#[path = "../../../../../arch/x86/kernel/descriptors/rust/layout/validation.rs"]
mod layout;
use layout::*;
// K1: base of the windows slot (X86_LAYOUT_WINDOWS_VIRTUAL in arch/x86/configs/layout.h).
const WINDOWS: u64 = 0xFFFF_F400_0000_0000;

// ====== guardie e stack separati con estremi esatti ===== //

#[test]
fn virtual_layout() {
    for stack in 1..=3 {
        assert_eq!(x86_ist_top(stack) - x86_ist_base(stack), 16384);
        assert_eq!(x86_ist_base(stack) & 4095, 0);
        if stack < 3 { assert_eq!(x86_ist_base(stack + 1) - x86_ist_top(stack), 4096); }
    }
    for stack in [0, 4, u32::MAX] { assert_eq!(x86_ist_base(stack), 0); assert_eq!(x86_ist_top(stack), 0); }
}

// ====== ogni pagina fisica consumata una volta e guardie sempre assenti ===== //

#[test]
fn page_layout() {
    let mut seen = [false; 12];
    for slot in 0..16 {
        let page = x86_ist_page(slot);
        if [0, 5, 10, 15].contains(&slot) { assert_eq!(page, 0); }
        else { assert!(!seen[(page - 1) as usize]); seen[(page - 1) as usize] = true; }
    }
    assert!(seen.iter().all(|value| *value));
    assert_eq!(x86_ist_page(16), 0);
    assert_eq!(x86_ist_page(u32::MAX), 0);
}

// ====== overflow allineamento e limite CPUID ===== //

#[test]
fn physical_bounds() {
    for bits in 32..=52 {
        assert_eq!(x86_ist_frame((1u64 << bits) - 49152, bits), 1);
        assert_eq!(x86_ist_frame((1u64 << bits) - 45056, bits), 0);
    }
    for base in [0, 1, 4097, u64::MAX] { assert_eq!(x86_ist_frame(base, 52), 0); }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(x86_ist_frame(4096, bits), 0); }
}

// ====== sedici pagine contigue e due guardie sul profilo principale ===== //

#[test]
fn main_layout() {
    for slot in 0..=18 {
        assert_eq!(x86_stack_page(2, slot), if (1..=16).contains(&slot) { slot } else { 0 });
    }
    for profile in [0, 4, u32::MAX] {
        assert_eq!(x86_stack_page(profile, 1), 0);
        assert_eq!(x86_stack_frame(profile, 4096, 52), 0);
    }
    for bits in 32..=52 {
        assert_eq!(x86_stack_frame(2, (1u64 << bits) - 65536, bits), 1);
        assert_eq!(x86_stack_frame(2, (1u64 << bits) - 61440, bits), 0);
    }
    for pointer in [WINDOWS + 0x61000, WINDOWS + 0x70FF8] { assert_eq!(x86_main_pointer(pointer), 1); }
    for pointer in [0, WINDOWS + 0x60FF8, WINDOWS + 0x61001, WINDOWS + 0x71000, u64::MAX] {
        assert_eq!(x86_main_pointer(pointer), 0);
    }
}

// ====== quattro pagine contigue e due guardie sul profilo syscall ===== //

#[test]
fn syscall_layout() {
    for slot in 0..=8 {
        assert_eq!(x86_stack_page(3, slot), if (1..=4).contains(&slot) { slot } else { 0 });
    }
    for bits in 32..=52 {
        assert_eq!(x86_stack_frame(3, (1u64 << bits) - 16384, bits), 1);
        assert_eq!(x86_stack_frame(3, (1u64 << bits) - 12288, bits), 0);
    }
    for pointer in [WINDOWS + 0x75000, WINDOWS + 0x78FF8, WINDOWS + 0x79000] {
        assert_eq!(x86_syscall_pointer(pointer), 1);
    }
    for pointer in [0, WINDOWS + 0x74FF8, WINDOWS + 0x75001, WINDOWS + 0x79008, WINDOWS + 0x71000, u64::MAX] {
        assert_eq!(x86_syscall_pointer(pointer), 0);
    }
}
