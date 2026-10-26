#[path = "../../../../../arch/x86/kernel/exceptions/rust/frame/validation.rs"]
mod frame;
use frame::*;

// ====== indirizzi canonici e confini degli ingressi ===== //

// K1: X86_KERNEL_IMAGE_BASE (arch/x86/configs/layout.h), slot 511.
const IMAGE: u64 = 0xFFFF_FFFF_8000_0000;

#[test]
fn targets() {
    assert_eq!(x86_exception_target(IMAGE + 0x10, IMAGE, IMAGE + 0x100), 1);
    for address in [0, 0xffffffff, IMAGE - 1, IMAGE + 0x100, 1 << 47, u64::MAX] {
        assert_eq!(x86_exception_target(address, IMAGE, IMAGE + 0x100), 0);
    }
    assert_eq!(x86_exception_target(IMAGE + 0x10, IMAGE + 0x100, IMAGE), 0);
    // The terminal code must lie in the image: a lower-half range is refused.
    assert_eq!(x86_exception_target(0x100000010, 0x100000000, 0x100000100), 0);
}

// ====== codici CPU distinti dai segnaposto degli ingressi assembly ===== //

#[test]
fn error_codes() {
    let cpu_errors = [8, 10, 11, 12, 13, 14, 17, 21, 29, 30];
    for vector in 0..32 {
        assert_eq!(x86_exception_error(vector), u32::from(cpu_errors.contains(&vector)));
        assert_eq!(x86_exception_frame(vector, 0, 8, 2, 16), 1);
        assert_eq!(x86_exception_frame(vector, 1, 8, 2, 16), u32::from(vector != 8 && cpu_errors.contains(&vector)));
    }
    assert_eq!(x86_exception_frame(32, 0, 8, 2, 16), 0);
    assert_eq!(x86_exception_error(u64::MAX), 0);
}

// ====== frame ring zero e bit fisso RFLAGS ===== //

#[test]
fn invalid_frames() {
    assert_eq!(x86_exception_frame(6, 0, 0, 2, 16), 0);
    assert_eq!(x86_exception_frame(6, 0, 8, 0, 16), 0);
    assert_eq!(x86_exception_frame(6, 0, 8, 2, 0), 0);
    assert_eq!(x86_exception_frame(8, 1, 8, 2, 16), 0);
}

// ====== operando IDTR interamente canonico e allineato ===== //

#[test]
fn descriptor_bounds() {
    assert_eq!(x86_exception_idtr(4096, 511), 1);
    assert_eq!(x86_exception_idtr((1 << 47) - 512, 511), 1);
    for base in [0, 4095, 4097, (1 << 47) - 16, 1 << 47, u64::MAX] {
        assert_eq!(x86_exception_idtr(base, 511), 0);
    }
    for limit in [0, 510, 512, 65535, u64::MAX] {
        assert_eq!(x86_exception_idtr(4096, limit), 0);
    }
}
