#![forbid(unsafe_code)]

// ====== kernel_vgpu_pci_bar ======

pub fn bar(low: u32, high: u32, size: u64, bits: u32) -> u64 {
    let kind = low & 7;
    if !matches!(kind, 0 | 4) || !(32..=52).contains(&bits) ||
        size < 16 || !size.is_power_of_two() || (kind == 0 && high != 0) {
        return u64::MAX;
    }
    let base = u64::from(low & !15) | (u64::from(high) << 32);
    let limit = if kind == 0 { 1u64 << 32 } else { 1u64 << bits };
    if base == 0 || base & (size-1) != 0 || size > limit || base > limit-size {
        return u64::MAX;
    }
    base
}

// ====== kernel_vgpu_pci_region ======

// Virtio1.2 4.1.4.3-.6 and 5.7.4: common56, notify2, ISR1, GPU16;
// notify multiplier is zero or a power of two divisible by two.
pub fn region(kind: u32, offset: u32, length: u32,
    multiplier: u32, base: u64, size: u64) -> u64 {
    let (minimum, alignment) = match kind {
        1 => (56, 4), 2 => (2, 2), 3 => (1, 1), 4 => (16, 4), _ => return u64::MAX,
    };
    if length < minimum || offset & (alignment-1) != 0 ||
        (kind != 2 && multiplier != 0) ||
        (kind == 2 && multiplier != 0 && (multiplier < 2 || !multiplier.is_power_of_two())) {
        return u64::MAX;
    }
    let offset = u64::from(offset);
    let length = u64::from(length);
    if base == 0 || length > size || offset > size-length ||
        base.checked_add(size).is_none() {
        return u64::MAX;
    }
    base.saturating_add(offset)
}

#[cfg(test)]
#[path = "../../../../../../../test/kernel/drivers/video/virtio/discovery/validation_test.rs"]
mod tests;
