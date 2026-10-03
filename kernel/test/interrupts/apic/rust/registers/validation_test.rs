#[path = "../../../../../../arch/x86/kernel/interrupts/apic/rust/registers/validation.rs"]
mod validation;
use validation::*;

// ====== versioni LAPIC e LVT compatibili col profilo ===== //

#[test]
fn lapic_versions() {
    for byte in 0..=255 { assert_eq!(x86_apic_lapic_version(0x0005_0000 | byte),u32::from((0x10..=0x1f).contains(&byte))); }
    for lvt in 0..=255 { assert_eq!(x86_apic_lapic_version(lvt<<16 | 0x14),u32::from((3..=6).contains(&lvt))); }
}

// ====== limite del selettore a 8 bit e conteggi hardware ===== //

#[test]
fn io_counts() {
    for max in 0..=255 {
        for version in [0x11,0x20] { assert_eq!(x86_apic_io_inputs(max<<16 | version),if max<120 {max+1} else {0}); }
    }
    assert_eq!(x86_apic_io_inputs(u32::MAX),0);
    assert_eq!(x86_apic_io_inputs(0x0017_0021),0);
}

// ====== fine GSI esclusiva senza overflow ===== //

#[test]
fn gsi_bounds() {
    assert_eq!(x86_apic_gsi_end(0,24),24);
    assert_eq!(x86_apic_gsi_end(u32::MAX,1),1u64<<32);
    assert_eq!(x86_apic_gsi_end(u32::MAX,2),0);
    for inputs in [0,121,u32::MAX] { assert_eq!(x86_apic_gsi_end(0,inputs),0); }
}
