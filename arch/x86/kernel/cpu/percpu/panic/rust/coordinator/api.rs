#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]
mod state;
static COORDINATOR: state::Coordinator = state::Coordinator::new();
#[cfg(not(target_has_atomic = "64"))]
compile_error!("Panic coordination requires native atomic64");

// ====== xp_panic_configure ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_configure(count: u32) -> u32 { COORDINATOR.configure(count) }

// ====== xp_panic_admit ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_admit(index: u32) -> u32 { COORDINATOR.admit(index) }

// ====== xp_panic_elect ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_elect(index: u32) -> u32 { COORDINATOR.elect(index) }

// ====== xp_panic_record_claim ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_record_claim(index: u32) -> u32 { COORDINATOR.record_claim(index) }

// ====== xp_panic_record_publish ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_record_publish(index: u32) -> u32 { COORDINATOR.record_publish(index) }

// ====== xp_panic_record_state ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_record_state(index: u32) -> u32 { COORDINATOR.record_state(index) }

// ====== xp_panic_acknowledge ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_acknowledge(index: u32) -> u32 { COORDINATOR.acknowledge(index) }

// ====== xp_panic_finish ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_finish(index: u32, outcome: u64) -> u32 { COORDINATOR.finish(index, outcome) }

// ====== xp_panic_value ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_value(field: u32) -> u64 { COORDINATOR.value(field) }

// ====== xp_panic_input_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_input_valid(kind: u64, vector: u64, error: u64, cs: u64, flags: u64, ss: u64) -> u32 {
    let has_error = matches!(vector, 8 | 10 | 11 | 12 | 13 | 14 | 17 | 21 | 29 | 30);
    u32::from(matches!(kind, 1..=3) && vector < 32 && cs == 8 && (ss == 0 || ss == 16) &&
        flags & 2 != 0 && (has_error || error == 0) && (vector != 8 || error == 0) &&
        (kind != 3 || vector == 2))
}

// ====== xp_panic_nmi_bits ======

// Intel SDM 078US 11.6.1, Figure 11-12: physical destination high[31:24], NMI low[10:8]=100b.
// Level bit 14 is asserted, trigger edge and shorthand zero; low write dispatches to exactly that ID.
#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_nmi_bits(id: u32) -> u64 {
    if id >= 255 { u64::MAX } else { (u64::from(id) << 56) | 0x4400 }
}

// ====== xp_panic_profile_valid ======

// Intel SDM 078US 11.4.4: IA32_APIC_BASE BSP bit 8, x2APIC bit 10, enable bit 11; initial 36-bit address profile.
#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_profile_valid(base: u64, physical: u64, pat: u64, expected_pat: u64,
    bits: u32, expected_bits: u32, bsp: u32) -> u32 {
    if !(32..=52).contains(&bits) || bits != expected_bits || bsp > 1 || pat != expected_pat { return 0; }
    let mask = ((1u64 << bits.min(36)) - 1) & !4095;
    u32::from(physical != 0 && physical & !mask == 0 && base & !(mask | 0xd00) == 0 &&
        base & mask == physical && base & 0xc00 == 0x800 && (base >> 8) & 1 == u64::from(bsp))
}

// ====== xp_panic_state_address ======

#[unsafe(no_mangle)]
pub extern "C" fn xp_panic_state_address() -> u64 { (&COORDINATOR as *const state::Coordinator) as u64 }
