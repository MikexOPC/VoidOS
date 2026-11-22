#![cfg_attr(not(test), no_std)]

// ====== virtio_blk_pci_and_protocol_policy ======

// OASIS VirtIO 1.2 §4.1.2, §4.1.4, §5.2: PCI discovery, feature negotiation,
// config validation and split virtqueue completion checks for virtio-blk.

pub const KVBLK_PCI_ABSENT: u32 = 0;
pub const KVBLK_PCI_READY: u32 = 1;
pub const KVBLK_PCI_REJECTED: u32 = 2;
pub const KVBLK_PCI_UNSUPPORTED: u32 = 3;

pub const VIRTIO_VENDOR_ID: u16 = 0x1AF4;
pub const VIRTIO_BLK_DEVICE_TRANSITIONAL: u16 = 0x1001;
pub const VIRTIO_BLK_DEVICE_MODERN: u16 = 0x1042;

pub const VIRTIO_BLK_F_RO: u32 = 1 << 5;
pub const VIRTIO_BLK_F_BLK_SIZE: u32 = 1 << 6;
pub const VIRTIO_BLK_F_FLUSH: u32 = 1 << 9;
pub const VIRTIO_F_VERSION_1_HIGH: u32 = 1 << 0;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VirtioBlkPciRegion {
    pub address: u64,
    pub length: u64,
    pub bar: u32,
    pub offset: u32,
    pub multiplier: u32,
    pub reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VirtioBlkPciLayout {
    pub regions: [VirtioBlkPciRegion; 4],
    pub capabilities: u32,
    pub revision: u32,
    pub class_code: u32,
    pub device_id: u32,
}

// ====== byte_at ======

#[inline(always)]
fn byte_at(bytes: &[u8], offset: usize) -> u8 {
    match bytes.get(offset) {
        Some(&b) => b,
        None => 0,
    }
}

// ====== read_le16 ======

fn read_le16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([byte_at(bytes, offset), byte_at(bytes, offset + 1)])
}

// ====== read_le32 ======

fn read_le32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        byte_at(bytes, offset),
        byte_at(bytes, offset + 1),
        byte_at(bytes, offset + 2),
        byte_at(bytes, offset + 3),
    ])
}

// ====== parse_bars ======

fn parse_bars(config: &[u8], phys_bits: u32) -> [u64; 6] {
    let mut bases = [u64::MAX; 6];
    let limit = 1u64 << phys_bits;
    let mut index = 0usize;

    while index < 6 {
        let low = read_le32(config, 0x10 + index * 4);
        if (low & 1) != 0 {
            index += 1;
            continue;
        }
        let kind = (low >> 1) & 3;
        if kind == 0 {
            let base = (low & !0xFu32) as u64;
            if let Some(slot) = bases.get_mut(index) {
                if base >= 0x1000 && base < limit {
                    *slot = base;
                }
            }
            index += 1;
        } else if kind == 2 {
            if index == 5 {
                break;
            }
            let high = read_le32(config, 0x14 + index * 4) as u64;
            let base = (high << 32) | ((low & !0xFu32) as u64);
            if let Some(slot) = bases.get_mut(index) {
                if base >= 0x1000 && base < limit {
                    *slot = base;
                }
            }
            index += 2;
        } else {
            index += 1;
        }
    }
    bases
}

// ====== check_region_bounds ======

fn check_region_bounds(kind: u32, address: u64, length: u64, multiplier: u32, phys_bits: u32) -> bool {
    if address < 0x1000 || phys_bits < 32 || phys_bits > 52 {
        return false;
    }
    let limit = 1u64 << phys_bits;
    let Some(end) = address.checked_add(length) else {
        return false;
    };
    if end > limit {
        return false;
    }
    match kind {
        1 => length >= 56 && (address & 3) == 0,
        2 => {
            length >= 2
                && (address & 1) == 0
                && (multiplier & 1) == 0
                && (multiplier as u64) <= length.saturating_sub(2)
        }
        3 => length >= 1,
        4 => length >= 8 && (address & 3) == 0,
        _ => false,
    }
}

// ====== ranges_overlap ======

fn ranges_overlap(a_base: u64, a_len: u64, b_base: u64, b_len: u64) -> bool {
    if a_len == 0 || b_len == 0 {
        return false;
    }
    let a_end = a_base.saturating_add(a_len);
    let b_end = b_base.saturating_add(b_len);
    a_base < b_end && b_base < a_end
}

// ====== parse_pci_slice ======

pub fn parse_pci_slice(config: &[u8], phys_bits: u32) -> Result<VirtioBlkPciLayout, u32> {
    if config.len() != 256 || !(32..=52).contains(&phys_bits) {
        return Err(KVBLK_PCI_REJECTED);
    }
    let vendor = read_le16(config, 0x00);
    let device = read_le16(config, 0x02);
    if vendor != VIRTIO_VENDOR_ID
        || (device != VIRTIO_BLK_DEVICE_MODERN && device != VIRTIO_BLK_DEVICE_TRANSITIONAL)
    {
        return Err(KVBLK_PCI_ABSENT);
    }
    let status = read_le16(config, 0x06);
    let header_type = byte_at(config, 0x0e) & 0x7f;
    let base_class = byte_at(config, 0x0b);
    if header_type != 0 || base_class != 0x01 || (status & 0x10) == 0 {
        return Err(KVBLK_PCI_UNSUPPORTED);
    }

    let bases = parse_bars(config, phys_bits);
    let mut layout = VirtioBlkPciLayout {
        regions: [VirtioBlkPciRegion::default(); 4],
        capabilities: 0,
        revision: byte_at(config, 0x08) as u32,
        class_code: ((byte_at(config, 0x09) as u32)
            | ((byte_at(config, 0x0a) as u32) << 8)
            | ((byte_at(config, 0x0b) as u32) << 16)),
        device_id: device as u32,
    };

    let mut visited = 0u64;
    let mut occupied = [0u64; 4];
    let mut found = 0u32;
    let mut cursor = byte_at(config, 0x34) as usize;

    while cursor != 0 {
        if !(0x40..=0xfc).contains(&cursor) || (cursor & 3) != 0 {
            return Err(KVBLK_PCI_REJECTED);
        }
        let bit = 1u64 << (cursor / 4);
        if (visited & bit) != 0 {
            return Err(KVBLK_PCI_REJECTED);
        }
        visited |= bit;
        layout.capabilities += 1;
        if layout.capabilities > 48 {
            return Err(KVBLK_PCI_REJECTED);
        }

        let cap_id = byte_at(config, cursor);
        let next = byte_at(config, cursor + 1) as usize;
        let cap_len = if cap_id == 0x09 {
            let len = byte_at(config, cursor + 2) as usize;
            if len < 3 || cursor + len > 256 {
                return Err(KVBLK_PCI_REJECTED);
            }
            len
        } else {
            2usize
        };

        let mut pos = cursor;
        while pos < cursor + cap_len {
            let word = (pos >> 6) & 3;
            let mask = 1u64 << (pos & 63);
            if let Some(w) = occupied.get_mut(word) {
                if (*w & mask) != 0 {
                    return Err(KVBLK_PCI_REJECTED);
                }
                *w |= mask;
            }
            pos += 1;
        }

        if cap_id == 0x09 {
            if cap_len < 4 {
                return Err(KVBLK_PCI_REJECTED);
            }
            let kind = byte_at(config, cursor + 3) as u32;
            if (1..=4).contains(&kind) {
                let min_len = if kind == 2 { 20usize } else { 16usize };
                if cap_len < min_len {
                    return Err(KVBLK_PCI_REJECTED);
                }
                let bar = byte_at(config, cursor + 4) as usize;
                let Some(&bar_base) = bases.get(bar) else {
                    return Err(KVBLK_PCI_REJECTED);
                };
                if bar_base != u64::MAX {
                    let offset = read_le32(config, cursor + 8);
                    let length = read_le32(config, cursor + 12) as u64;
                    let multiplier = if kind == 2 {
                        read_le32(config, cursor + 16)
                    } else {
                        0
                    };
                    let Some(address) = bar_base.checked_add(offset as u64) else {
                        return Err(KVBLK_PCI_REJECTED);
                    };
                    if !check_region_bounds(kind, address, length, multiplier, phys_bits) {
                        return Err(KVBLK_PCI_REJECTED);
                    }
                    let idx = (kind - 1) as usize;
                    if (found & (1u32 << idx)) == 0 {
                        if let Some(reg) = layout.regions.get_mut(idx) {
                            *reg = VirtioBlkPciRegion {
                                address,
                                length,
                                bar: bar as u32,
                                offset,
                                multiplier,
                                reserved: 0,
                            };
                        }
                        found |= 1u32 << idx;
                    }
                }
            }
        }
        cursor = next;
    }

    if found != 0x0f {
        return Err(KVBLK_PCI_UNSUPPORTED);
    }

    let mut i = 0usize;
    while i < 4 {
        let mut j = i + 1;
        while j < 4 {
            if let (Some(ri), Some(rj)) = (layout.regions.get(i), layout.regions.get(j)) {
                if ranges_overlap(ri.address, ri.length, rj.address, rj.length) {
                    return Err(KVBLK_PCI_REJECTED);
                }
            }
            j += 1;
        }
        i += 1;
    }

    Ok(layout)
}

// ====== kvblk_pci_probe ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kvblk_pci_probe(
    config: *const u8,
    bytes: u32,
    physical_bits: u32,
    output: *mut VirtioBlkPciLayout,
) -> u32 {
    if config.is_null() || output.is_null() || bytes != 256 || (output as usize & 7) != 0 {
        return KVBLK_PCI_REJECTED;
    }
    let slice = unsafe { core::slice::from_raw_parts(config, 256) };
    match parse_pci_slice(slice, physical_bits) {
        Ok(layout) => {
            unsafe {
                *output = layout;
            }
            KVBLK_PCI_READY
        }
        Err(code) => code,
    }
}

// ====== kvblk_features_select ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kvblk_features_select(
    device_low: u32,
    device_high: u32,
    driver_low_out: *mut u32,
    driver_high_out: *mut u32,
) -> u32 {
    if driver_low_out.is_null() || driver_high_out.is_null() {
        return 0;
    }
    if (device_high & VIRTIO_F_VERSION_1_HIGH) == 0 {
        return 0;
    }
    let accepted_low =
        device_low & (VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH);
    let accepted_high = VIRTIO_F_VERSION_1_HIGH;
    unsafe {
        *driver_low_out = accepted_low;
        *driver_high_out = accepted_high;
    }
    1
}

// ====== kvblk_queue_size_choose ======

#[unsafe(no_mangle)]
pub extern "C" fn kvblk_queue_size_choose(maximum: u32) -> u32 {
    if !(4..=32768).contains(&maximum) {
        return 0;
    }
    if maximum >= 8 {
        8
    } else {
        4
    }
}

// ====== kvblk_config_validate ======

#[unsafe(no_mangle)]
pub extern "C" fn kvblk_config_validate(
    capacity_sectors: u64,
    blk_size: u32,
    has_blk_size: u32,
) -> u32 {
    if capacity_sectors == 0 || capacity_sectors > (1u64 << 40) {
        return 0;
    }
    if has_blk_size != 0 && blk_size != 512 && blk_size != 4096 {
        return 0;
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build_valid_config() -> [u8; 256] {
        let mut cfg = [0u8; 256];
        // Vendor 1AF4, Device 1042 (modern virtio-blk)
        cfg[0..2].copy_from_slice(&0x1AF4u16.to_le_bytes());
        cfg[2..4].copy_from_slice(&0x1042u16.to_le_bytes());
        // Status: Capabilities list bit 4
        cfg[6..8].copy_from_slice(&0x0010u16.to_le_bytes());
        // Revision 1, Class 01:80:00 (Mass Storage)
        cfg[8] = 1;
        cfg[9] = 0x00;
        cfg[10] = 0x80;
        cfg[11] = 0x01;
        // BAR4 (offset 0x20): 64-bit MMIO at 0xFE00_0000
        cfg[0x20..0x24].copy_from_slice(&(0xFE00_0000u32 | 0x0Cu32).to_le_bytes());
        cfg[0x24..0x28].copy_from_slice(&0u32.to_le_bytes());
        // Capabilities pointer at 0x40
        cfg[0x34] = 0x40;

        // Cap 1: COMMON_CFG at 0x40 (16 bytes), BAR 4, offset 0x0000, len 0x1000
        cfg[0x40] = 0x09;
        cfg[0x41] = 0x50;
        cfg[0x42] = 16;
        cfg[0x43] = 1;
        cfg[0x44] = 4;
        cfg[0x48..0x4c].copy_from_slice(&0x0000u32.to_le_bytes());
        cfg[0x4c..0x50].copy_from_slice(&0x1000u32.to_le_bytes());

        // Cap 2: NOTIFY_CFG at 0x50 (20 bytes), BAR 4, offset 0x3000, len 0x1000, mult 4
        cfg[0x50] = 0x09;
        cfg[0x51] = 0x64;
        cfg[0x52] = 20;
        cfg[0x53] = 2;
        cfg[0x54] = 4;
        cfg[0x58..0x5c].copy_from_slice(&0x3000u32.to_le_bytes());
        cfg[0x5c..0x60].copy_from_slice(&0x1000u32.to_le_bytes());
        cfg[0x60..0x64].copy_from_slice(&4u32.to_le_bytes());

        // Cap 3: ISR_CFG at 0x64 (16 bytes), BAR 4, offset 0x1000, len 0x1000
        cfg[0x64] = 0x09;
        cfg[0x65] = 0x74;
        cfg[0x66] = 16;
        cfg[0x67] = 3;
        cfg[0x68] = 4;
        cfg[0x6c..0x70].copy_from_slice(&0x1000u32.to_le_bytes());
        cfg[0x70..0x74].copy_from_slice(&0x1000u32.to_le_bytes());

        // Cap 4: DEVICE_CFG at 0x74 (16 bytes), BAR 4, offset 0x2000, len 0x1000
        cfg[0x74] = 0x09;
        cfg[0x75] = 0x00;
        cfg[0x76] = 16;
        cfg[0x77] = 4;
        cfg[0x78] = 4;
        cfg[0x7c..0x80].copy_from_slice(&0x2000u32.to_le_bytes());
        cfg[0x80..0x84].copy_from_slice(&0x1000u32.to_le_bytes());
        cfg
    }

    #[test]
    fn parses_valid_modern_virtio_blk_pci_config() {
        let cfg = build_valid_config();
        let layout = parse_pci_slice(&cfg, 39).expect("valid config");
        assert_eq!(layout.device_id, 0x1042);
        assert_eq!(layout.regions[0].address, 0xFE00_0000);
        assert_eq!(layout.regions[1].address, 0xFE00_3000);
        assert_eq!(layout.regions[1].multiplier, 4);
        assert_eq!(layout.regions[2].address, 0xFE00_1000);
        assert_eq!(layout.regions[3].address, 0xFE00_2000);
    }

    #[test]
    fn rejects_overlapping_or_looping_capabilities() {
        let mut cfg = build_valid_config();
        // Make DEVICE_CFG overlap COMMON_CFG at offset 0
        cfg[0x7c..0x80].copy_from_slice(&0x0000u32.to_le_bytes());
        assert_eq!(parse_pci_slice(&cfg, 39), Err(KVBLK_PCI_REJECTED));

        // Capability cycle
        let mut loop_cfg = build_valid_config();
        loop_cfg[0x41] = 0x40;
        assert_eq!(parse_pci_slice(&loop_cfg, 39), Err(KVBLK_PCI_REJECTED));
    }

    #[test]
    fn feature_and_config_validation() {
        let mut low = 0u32;
        let mut high = 0u32;
        assert_eq!(
            unsafe { kvblk_features_select(VIRTIO_BLK_F_FLUSH | 0xdead0000, 0, &mut low, &mut high) },
            0
        );
        assert_eq!(
            unsafe {
                kvblk_features_select(
                    VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_BLK_SIZE | 0x10000,
                    VIRTIO_F_VERSION_1_HIGH,
                    &mut low,
                    &mut high,
                )
            },
            1
        );
        assert_eq!(low, VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_BLK_SIZE);
        assert_eq!(high, VIRTIO_F_VERSION_1_HIGH);
        assert_eq!(kvblk_queue_size_choose(128), 8);
        assert_eq!(kvblk_queue_size_choose(2), 0);
        assert_eq!(kvblk_config_validate(128, 512, 1), 1);
        assert_eq!(kvblk_config_validate(0, 512, 1), 0);
        assert_eq!(kvblk_config_validate(128, 1024, 1), 0);
    }
}
