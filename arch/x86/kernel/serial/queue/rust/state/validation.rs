#![cfg_attr(not(test), no_std)]

// ====== contatori monotoni verificati senza overflow ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_uart_queue_state(head: u32, count: u32, next: u64, completed: u64) -> u32 {
    u32::from(head < 3 && count <= 3 && next != 0 &&
        completed.checked_add(u64::from(count) + 1) == Some(next))
}

// ====== indice fisico solamente da coordinate bounded ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_uart_queue_slot(head: u32, ordinal: u32) -> u32 {
    if head >= 3 || ordinal >= 3 { return u32::MAX; }
    (head + ordinal) % 3
}
