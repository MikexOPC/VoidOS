#![cfg_attr(not(test), no_std)]

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
