#[path = "../../../../../arch/x86/kernel/serial/interrupts/rust/routing/validation.rs"]
mod validation;

// ====== IRQ ISA con encoding MADT valido e vettore riservato al driver ===== //

#[test]
fn route_flags() {
    for flags in 0..256 {
        let valid = flags < 16 && flags & 3 != 2 && ((flags >> 2) & 3) != 2;
        assert_eq!(validation::x86_uart_route_bits(flags) != 0, valid);
    }
    assert_eq!(validation::x86_uart_route_bits(0),0x40);
    assert_eq!(validation::x86_uart_route_bits(15),0xa040);
}

// ====== THRE soltanto senza scambiare FIFO status per interrupt ID ===== //

#[test]
fn thre_identity() {
    for iir in 0..256 {
        assert_eq!(validation::x86_uart_thre(iir),u32::from(matches!(iir,0x02 | 0xc2)));
    }
    assert_eq!(validation::x86_uart_thre(0x102),0);
}
