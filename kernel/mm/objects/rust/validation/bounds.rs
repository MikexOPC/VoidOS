#![cfg_attr(not(test), no_std)]
const MAX_BYTES: u64 = 65536;
const MAX_ALIGNMENT: u64 = 4096;

// ====== richiesta limitata e allineamento potenza di due ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_object_request(bytes: u64, alignment: u64) -> u32 {
    u32::from((1..=MAX_BYTES).contains(&bytes) && alignment <= MAX_ALIGNMENT && alignment.is_power_of_two())
}

// ====== offset codificato piu uno per distinguere il primo byte dall errore ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_object_position(start: u64, end: u64, bytes: u64, alignment: u64) -> u64 {
    if start > end || end > MAX_BYTES || kernel_object_request(bytes, alignment) == 0 { return 0; }
    let Some(sum) = start.checked_add(alignment - 1) else { return 0; };
    let aligned = sum & !(alignment - 1);
    match aligned.checked_add(bytes) {
        Some(limit) if limit <= end => aligned + 1,
        _ => 0,
    }
}

// ====== record completo entro il buffer esplicito ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_object_record(offset: u64, bytes: u64, alignment: u64, capacity: u64) -> u32 {
    if capacity == 0 || capacity > MAX_BYTES || kernel_object_request(bytes, alignment) == 0
        || offset & (alignment - 1) != 0 { return 0; }
    u32::from(offset.checked_add(bytes).is_some_and(|end| end <= capacity))
}
