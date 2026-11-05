#![forbid(unsafe_code)]

// ====== xis_idle_return_valid ======

// Intel SDM 253668-078US, 6.14.2: RSP aligned to 16, five 8-byte CPU slots.
// interrupt.asm adds the 8-byte vector; idle.asm reserves 576 bytes below top.
pub fn idle_return_valid(frame: u64, top: u64, rsp: u64, cs: u64,
    flags: u64, ss: u64, diagnostic: u32) -> u32 {
    let Some(expected_rsp) = top.checked_sub(576) else { return 0; };
    let Some(expected_frame) = (rsp & !15).checked_sub(48) else { return 0; };
    let expected_flags = match diagnostic { 0 => 0x202, 1 => 0x602, _ => return 0 };
    u32::from(rsp == expected_rsp && frame == expected_frame && cs == 8 && ss == 16 &&
        flags & 0x7602 == expected_flags)
}

#[cfg(test)]
#[path = "../../../../../../../../test/arch/x86/kernel/cpu/percpu/service/rust/frame/validation_test.rs"]
mod tests;

// ====== task_return_valid ======

pub fn task_return_valid(frame: u64, rsp: u64, cs: u64, flags: u64, ss: u64) -> u32 {
    u32::from((rsp & !15).checked_sub(48) == Some(frame) && cs == 8 && ss == 16 &&
        flags & 0x7602 == 0x202)
}
