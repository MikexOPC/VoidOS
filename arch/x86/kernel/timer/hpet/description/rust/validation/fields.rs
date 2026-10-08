#![cfg_attr(not(test), no_std)]

// ====== x86_hpet_timer_valid ======

// Intel HPET 1.0a section 2.3.8: reserved 31:16, 7, 0; capability
// dependencies for periodic (3/4), size (5/8), value-set (6/3), FSB (14/15).
#[unsafe(no_mangle)]
pub extern "C" fn x86_hpet_timer_valid(id: u32, config: u32) -> u32 {
    u32::from(config & 0xffff_0081 == 0
        && (config & (1 << 3) == 0 || config & (1 << 4) != 0)
        && (config & (1 << 5) == 0 || id & (1 << 13) != 0)
        && (config & (1 << 8) == 0 || config & (1 << 5) != 0)
        && (config & (1 << 6) == 0 || config & (1 << 3) != 0)
        && (config & (1 << 14) == 0 || config & (1 << 15) != 0))
}

// ====== x86_hpet_timer_candidate ======

// Observation only, not ownership. Sections 2.3.5/2.3.6/2.3.8:
// skip legacy timers 0/1, enabled, periodic, FSB-enabled or pending timers.
// A route mask describes IOAPIC inputs, not validated/available system GSIs.
#[unsafe(no_mangle)]
pub extern "C" fn x86_hpet_timer_candidate(
    id: u32, global: u32, pending: u32, index: u32, config: u32, routes: u32,
) -> u32 {
    if index > ((id >> 8) & 31) || x86_hpet_timer_valid(id, config) == 0 {
        return 0;
    }
    u32::from(!(global & 2 != 0 && index < 2)
        && config & ((1 << 2) | (1 << 3) | (1 << 6) | (1 << 14)) == 0
        && pending & (1u32 << index) == 0
        && (routes != 0 || config & (1 << 15) != 0))
}

// ====== x86_hpet_capabilities_valid ======

// Intel HPET 1.0a section 2.3.4: identity must match ACPI; revision != 0,
// period is nonzero and <= 0x05f5e100 femtoseconds. Read-only probe does not
// infer free comparators, ownership, IRQ routing or a live main counter.
#[unsafe(no_mangle)]
pub extern "C" fn x86_hpet_capabilities_valid(expected_id: u32, id: u32, period: u32) -> u32 {
    u32::from(id == expected_id && id & 255 != 0 && id & (1 << 14) == 0
        && (1..=100_000_000).contains(&period))
}

// ====== x86_hpet_table_span ======

// Table-copy spans may be unaligned. Checked extent is numerical only;
// the C reader owns the mapping/permissions/lifetime decision.
#[unsafe(no_mangle)]
pub extern "C" fn x86_hpet_table_span(physical: u64, bytes: u32, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) || bytes == 0 || bytes > 56 || physical < 4096 {
        return 0;
    }
    let Some(end) = physical.checked_add(u64::from(bytes)) else { return 0; };
    u32::from(end <= (1u64 << bits))
}

// ====== x86_hpet_description_fields ======

// Intel HPET 1.0a section 3.2.4 Table 3: block ID bit 14 reserved;
// hardware revision bits 7:0. GAS identifies memory, not a pointer to use.
// Profile: GAS width 0 or 64, offset/reserved zero; 8-byte aligned block,
// physical width 32..52; exclude page zero. These are supported-profile
// restrictions, not claims that every other encoding violates HPET.
#[unsafe(no_mangle)]
pub extern "C" fn x86_hpet_description_fields(
    block_id: u32, gas: u32, physical: u64, physical_bits: u32, attributes: u32,
) -> u32 {
    if !(32..=52).contains(&physical_bits)
        || block_id & 0xff == 0 || block_id & (1 << 14) != 0
        || (gas != 0 && gas != (64 << 8))
        || attributes > 255 || attributes & 15 > 2
        || physical < 4096 || physical & 7 != 0
    {
        return 0;
    }
    let Some(end) = physical.checked_add(1024) else { return 0; };
    u32::from(end <= (1u64 << physical_bits))
}
