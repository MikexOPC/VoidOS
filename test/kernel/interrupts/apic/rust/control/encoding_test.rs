#[path = "../../../../../../arch/x86/kernel/interrupts/apic/control/rust/encoding/validation.rs"]
mod validation;

// ====== registri LVT presenti secondo MaxLVT e rotte mascherate ===== //

#[test]
fn controller_encoding() {
    for max in 3..=6 {
        for index in 0..=max {
            assert_ne!(validation::x86_irq_lvt(max,index),0);
        }
        assert_eq!(validation::x86_irq_lvt(max,max+1),0);
    }
    assert_eq!(validation::x86_irq_lvt(7,0),0);
    assert_eq!(validation::x86_irq_lvt(3,3),0x370);
    assert_eq!(validation::x86_irq_lvt(6,6),0x2f0);
}

// ====== default ISA edge e polarita esplicita senza encoding riservati ===== //

#[test]
fn nmi_encoding() {
    for lint in 0..=1 {
        for flags in 0..=255 {
            let valid = flags <= 7 && flags & 3 != 2;
            assert_eq!(validation::x86_irq_nmi_lint(lint,flags) != 0,valid);
        }
    }
    assert_eq!(validation::x86_irq_nmi_lint(2,0),0);
    assert_eq!(validation::x86_irq_nmi_lint(1,3),0x2400);
}
