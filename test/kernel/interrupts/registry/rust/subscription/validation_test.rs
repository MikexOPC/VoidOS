#[path = "../../../../../../arch/x86/kernel/interrupts/registry/rust/subscription/validation.rs"]
mod validation;

// ====== nessun vettore eccezione self test o spurious puo essere registrato ===== //

#[test]
fn vectors_and_faults() {
    for vector in 0..256 {
        for fault in 0..4 {
            assert_eq!(validation::x86_irq_subscription_fields(vector,fault,1,2),
                u32::from(matches!(vector,0x40..=0x43) && fault <= 1));
        }
    }
}

// ====== identificatori correnti precedono sempre il prossimo senza wrap ===== //

#[test]
fn generations() {
    for id in [0,1,2,u64::MAX-1,u64::MAX] {
        for next in [0,1,2,u64::MAX-1,u64::MAX] {
            assert_eq!(validation::x86_irq_subscription_fields(0x40,0,id,next),u32::from(id != 0 && id < next));
        }
    }
}
