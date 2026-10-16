#![cfg_attr(not(test), no_std)]

// ====== soli vettori hardware autorizzati e identita monotone senza wrap ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_irq_subscription_fields(vector: u32, fault: u32, id: u64, next: u64) -> u32 {
    u32::from(matches!(vector, 0x40..=0x42) && fault <= 1 && id != 0 && id < next)
}
