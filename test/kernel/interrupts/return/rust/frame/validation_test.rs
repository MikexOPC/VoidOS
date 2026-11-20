#[path = "../../../../../../arch/x86/kernel/interrupts/return/rust/frame/validation.rs"]
mod validation;

// ====== limiti IDTR completi senza wrap o indirizzi non canonici ===== //

#[test]
fn idtr_bounds() {
    assert_eq!(validation::x86_irq_idtr(4096, 4095), 1);
    assert_eq!(validation::x86_irq_idtr((1u64 << 47) - 4096, 4095), 1);
    for (base, limit) in [(0,4095),(4097,4095),(4096,511),((1u64<<47)-4080,4095),(u64::MAX,4095)] {
        assert_eq!(validation::x86_irq_idtr(base,limit), 0);
    }
}

// ====== frame ring zero e sorgenti senza EOI ===== //

#[test]
fn return_frame() {
    for vector in 0..256 {
        assert_eq!(validation::x86_irq_frame(vector,8,2,16), u32::from(vector == 0xf0 || vector == 0xff));
        assert_eq!(validation::x86_irq_frame(vector,8,0x202,16),u32::from((32..=255).contains(&vector) && vector != 0xf0));
    }
    for (cs, flags, ss) in [(0,2,16),(8,0,16),(8,2,0),(8,0x202,16)] {
        assert_eq!(validation::x86_irq_frame(0xf0,cs,flags,ss),0);
    }
    assert_eq!(validation::x86_irq_frame(0xff,8,0x202,16),1);
    assert_eq!(validation::x86_irq_frame(0xf0,8,0x402,16),1);
}

// ====== saved_area_bounds ======

#[test]
fn saved_area_bounds() {
    let floor = 0xffff_f400_0006_1000;
    let top = floor + 65536;
    for address in [floor + 664, floor + 672, top - 48] {
        assert_eq!(validation::x86_irq_saved_area(address, floor, top), 1);
    }
    for address in [0, floor + 656, floor + 665, top - 40, u64::MAX - 7] {
        assert_eq!(validation::x86_irq_saved_area(address, floor, top), 0);
    }
    assert_eq!(validation::x86_irq_saved_area(top - 48, top, floor), 0);
    assert_eq!(validation::x86_irq_saved_area(1 << 47, (1 << 47) - 4096, (1 << 47) + 4096), 0);
}
