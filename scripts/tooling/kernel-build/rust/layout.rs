use std::fs;
use std::path::Path;

const LAYOUT: &str = include_str!("../../../../arch/x86/configs/layout.h");
pub const COPIES: [&str; 4] = [
    "arch/x86/kernel/exceptions/rust/frame/validation.rs",
    "arch/x86/kernel/descriptors/rust/layout/validation.rs",
    "arch/x86/mm/regions/rust/layout/validation.rs",
    "arch/x86/mm/paging/rust/validation/entries.rs",
];

// ====== define_value ======

fn define_value<'a>(layout: &'a str, name: &str) -> Result<&'a str, String> {
    layout
        .lines()
        .find_map(|line| {
            let mut fields = line.split_whitespace();
            match (fields.next(), fields.next(), fields.next()) {
                (Some("#define"), Some(found), Some(value)) if found == name => Some(value),
                _ => None,
            }
        })
        .ok_or_else(|| format!("layout.h: {name} missing"))
}

// ====== hex_define ======

fn hex_define(layout: &str, name: &str) -> Result<u64, String> {
    let value = define_value(layout, name)?;
    value
        .strip_prefix("0x")
        .and_then(|value| value.strip_suffix("ULL"))
        .and_then(|value| u64::from_str_radix(value, 16).ok())
        .ok_or_else(|| format!("layout.h: {name} missing"))
}

// ====== slot_define ======

fn slot_define(layout: &str, name: &str) -> Result<u64, String> {
    define_value(layout, name)?
        .strip_suffix('U')
        .and_then(|value| value.parse().ok())
        .ok_or_else(|| format!("layout.h: {name} missing"))
}

// ====== slot_address ======

// Intel SDM Vol. 3A 3.3.7.1: slots 256-511 are the upper half, bits 63:48 copy bit 47.
fn slot_address(slot: u64) -> u64 {
    let address = slot << 39;
    if slot >= 256 { address | 0xFFFF_0000_0000_0000 } else { address }
}

// ====== constant ======

fn constant(source: &str, name: &str) -> Option<u64> {
    let marker = format!("const {name}: u64 = ");
    let start = source.find(&marker)? + marker.len();
    let rest = &source[start..];
    let value = &rest[..rest.find(';')?];
    match value.strip_prefix("0x") {
        Some(hex) => u64::from_str_radix(&hex.replace('_', ""), 16).ok(),
        None => value.parse().ok(),
    }
}

// ====== expected_copies ======

pub fn expected_copies(layout: &str) -> Result<Vec<(&'static str, &'static str, u64)>, String> {
    let windows = slot_address(slot_define(layout, "X86_LAYOUT_WINDOWS_SLOT")?);
    let framebuffer = slot_address(slot_define(layout, "X86_LAYOUT_FB_SLOT")?);
    Ok(vec![
        (COPIES[3], "RECURSIVE", slot_define(layout, "X86_LAYOUT_RECURSIVE_SLOT")?),
        (COPIES[0], "KERNEL_IMAGE_BASE", hex_define(layout, "X86_KERNEL_IMAGE_BASE")?),
        (COPIES[1], "WINDOWS", windows),
        (COPIES[2], "WINDOWS", windows),
        (COPIES[3], "LAPIC_VIRTUAL", windows + 0x12_0000),
        (COPIES[3], "VIDEO", framebuffer + 4096),
    ])
}

// ====== check_source ======

pub fn check_source(path: &str, name: &str, value: u64, source: &str) -> Result<(), String> {
    if constant(source, name) != Some(value) {
        return Err(format!("{path}: {name} differs from layout.h"));
    }
    Ok(())
}

// ====== check_copies ======

pub fn check_copies(repo: &str) -> Result<(), String> {
    for (path, name, value) in expected_copies(LAYOUT)? {
        let full = Path::new(repo).join(path);
        let source = fs::read_to_string(&full).map_err(|error| format!("{}: {error}", full.display()))?;
        check_source(path, name, value, &source)?;
    }
    Ok(())
}
