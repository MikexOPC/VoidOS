#![cfg_attr(not(test), no_std)]

// ====== margine e allineamento con aritmetica controllata ====== //

#[unsafe(no_mangle)]
pub extern "C" fn os_buffer_capacity(
    needed: u64,
    entry_bytes: u64,
    extra_entries: u64,
    alignment: u64,
    maximum: u64,
) -> u64 {
    if needed == 0 || entry_bytes == 0 || !alignment.is_power_of_two() {
        return 0;
    }
    let Some(margin) = entry_bytes.checked_mul(extra_entries) else {
        return 0;
    };
    let Some(total) = needed.checked_add(margin) else {
        return 0;
    };
    let remainder = total & (alignment - 1);
    let padding = if remainder == 0 { 0 } else { alignment - remainder };
    match total.checked_add(padding) {
        Some(capacity) if capacity <= maximum => capacity,
        _ => 0,
    }
}

// ====== intervallo del buffer rappresentabile con estremo esclusivo ====== //

#[unsafe(no_mangle)]
pub extern "C" fn os_buffer_extent_is_valid(base: u64, capacity: u64, maximum: u64) -> u32 {
    u32::from(base != 0 && capacity != 0 && capacity <= maximum && base.checked_add(capacity).is_some())
}
