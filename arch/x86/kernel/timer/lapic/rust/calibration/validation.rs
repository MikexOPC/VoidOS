#![cfg_attr(not(test), no_std)]

// ====== stati della source con backpressure prima del prossimo riarmo ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_timer_event_fields(count: u32, consumed: u32, phase: u32, active: u32) -> u32 {
    u32::from(match phase {
        0 => count == 0 && consumed == 0 && active == 0,
        1 | 4 => count == consumed && active == 0,
        2 => count == consumed && active == 1 && count < u32::MAX,
        3 => consumed.checked_add(1) == Some(count) && active == 1,
        _ => false,
    })
}

// ====== consegna esatta e numero di risvegli entro il budget dichiarato ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_timer_wait_result(result: u32, budget: u32, count: u32, error: u32, goal: u32) -> u32 {
    u32::from((1..=1024).contains(&budget) && result > 0 && result <= budget+1 &&
        (1..=3).contains(&goal) && count == goal && error == 0)
}

// ====== timer oneshot mascherato e scaduto senza interpretare il reload storico ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_timer_previous_valid(lvt: u32, divide: u32, current: u32) -> u32 {
    u32::from(lvt & !0xff == 0x10000 && (32..255).contains(&(lvt & 255)) &&
        divide & !0xb == 0 && current == 0)
}

// ====== profilo iniziale System IO dword senza porte legacy UART ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_pm_clock_port(port: u64, flags: u32, length: u32) -> u32 {
    u32::from((0x100..=0xfffc).contains(&port) && port & 3 == 0 &&
        !(0x3f8..0x400).contains(&port) && flags & (1 << 20) == 0 && length == 4)
}

// ====== differenza modulo 24 oppure 32 bit senza accettare intervalli ambigui ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_pm_elapsed(start: u32, end: u32, mask: u32) -> u32 {
    if !matches!(mask, 0xffffff | 0xffffffff) || start & !mask != 0 || end & !mask != 0 { return 0; }
    let delta = end.wrapping_sub(start) & mask;
    if delta > mask / 2 { return 0; }
    delta
}

// ====== reload approssimato per 35795 tick PM con prodotto limitato a u64 ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_timer_reload(start: u32, end: u32, elapsed: u32) -> u32 {
    if end == 0 || start <= end || !(35795..=143180).contains(&elapsed) { return 0; }
    let result = (u64::from(start - end) * 35795).div_ceil(u64::from(elapsed));
    u32::try_from(result).unwrap_or(0)
}
