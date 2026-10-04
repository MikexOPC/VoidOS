#[path = "../../../../../../arch/x86/kernel/serial/queue/rust/state/validation.rs"]
mod validation;

// ====== stati bounded e arithmetic overflow per i receipt monotoni ===== //

#[test]
fn states() {
    for head in 0..=3 {
        for count in 0..=4 {
            for completed in [0,3,u64::MAX-4,u64::MAX] {
                let next = completed.checked_add(u64::from(count)+1).unwrap_or(0);
                let valid = head < 3 && count <= 3 && next != 0;
                assert_eq!(validation::x86_uart_queue_state(head,count,next,completed),u32::from(valid));
            }
        }
    }
}

// ====== tre indici di bordo e nessuna lettura oltre la coda ===== //

#[test]
fn slots() {
    for head in 0..3 {
        for ordinal in 0..3 {
            assert_eq!(validation::x86_uart_queue_slot(head,ordinal),(head+ordinal)%3);
        }
    }
    assert_eq!(validation::x86_uart_queue_slot(3,0),u32::MAX);
    assert_eq!(validation::x86_uart_queue_slot(0,u32::MAX),u32::MAX);
}
