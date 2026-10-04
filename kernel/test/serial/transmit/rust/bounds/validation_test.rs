#[path = "../../../../../../arch/x86/kernel/serial/transmit/rust/bounds/validation.rs"]
mod validation;

// ====== tutte le transizioni bounded senza aritmetica che puo traboccare ===== //

#[test]
fn states() {
    for length in 0..=65 {
        for next in 0..=66 {
            for done in 0..=2 {
                let valid = (1..=64).contains(&length) && next <= length &&
                    (done == 0 || (done == 1 && next == length));
                assert_eq!(validation::x86_uart_tx_bounds(length,next,done),u32::from(valid));
            }
        }
    }
    assert_eq!(validation::x86_uart_tx_bounds(u32::MAX,0,0),0);
    assert_eq!(validation::x86_uart_tx_bounds(64,u32::MAX,0),0);
}
