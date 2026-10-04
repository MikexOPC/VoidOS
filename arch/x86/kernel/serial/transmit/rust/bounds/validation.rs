#![cfg_attr(not(test), no_std)]

// ====== indici scalari validati senza dereferenziare memoria C ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_uart_tx_bounds(length: u32, next: u32, done: u32) -> u32 {
    u32::from((1..=64).contains(&length) && next <= length &&
        (done == 0 || (done == 1 && next == length)))
}

// ====== contatore cumulativo bounded prima di aprire una nuova trasmissione ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_uart_tx_goal(base: u32, length: u32) -> u32 {
    if !(1..=64).contains(&length) { return 0; }
    base.checked_add(length+1).unwrap_or(0)
}
