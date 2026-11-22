#![cfg_attr(not(test), no_std)]

// ====== ahci_pci_and_fis_protocol_policy ======

// Intel Serial ATA AHCI 1.3.1 §2.1, §3.1, §3.3, §4.2 & ATA8-ACS §7.14, §7.16, §7.25, §7.63.

pub const KAHCI_PCI_ABSENT: u32 = 0;
pub const KAHCI_PCI_READY: u32 = 1;
pub const KAHCI_PCI_REJECTED: u32 = 2;
pub const KAHCI_PCI_UNSUPPORTED: u32 = 3;

pub const AHCI_CLASS_SATA: u32 = 0x010601;
pub const SATA_SIG_ATA: u32 = 0x0000_0101;

pub const ATA_CMD_READ_DMA_EXT: u8 = 0x25;
pub const ATA_CMD_WRITE_DMA_EXT: u8 = 0x35;
pub const ATA_CMD_FLUSH_CACHE_EXT: u8 = 0xEA;
pub const ATA_CMD_IDENTIFY: u8 = 0xEC;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AhciPciLayout {
    pub abar_address: u64,
    pub vendor_id: u16,
    pub device_id: u16,
    pub revision: u32,
    pub class_code: u32,
    pub reserved: u32,
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

// ====== read_le64 ======

fn read_le64(bytes: &[u8], offset: usize) -> u64 {
    let lo = read_le32(bytes, offset) as u64;
    let hi = read_le32(bytes, offset + 4) as u64;
    (hi << 32) | lo
}

// ====== parse_pci_slice ======

pub fn parse_pci_slice(config: &[u8], phys_bits: u32) -> Result<AhciPciLayout, u32> {
    if config.len() != 256 || !(32..=52).contains(&phys_bits) {
        return Err(KAHCI_PCI_REJECTED);
    }
    let vendor_id = read_le16(config, 0x00);
    let device_id = read_le16(config, 0x02);
    if vendor_id == 0xFFFF || vendor_id == 0 {
        return Err(KAHCI_PCI_ABSENT);
    }

    let class_code = (byte_at(config, 0x09) as u32)
        | ((byte_at(config, 0x0a) as u32) << 8)
        | ((byte_at(config, 0x0b) as u32) << 16);
    if class_code != AHCI_CLASS_SATA {
        return Err(KAHCI_PCI_ABSENT);
    }

    let header_type = byte_at(config, 0x0e) & 0x7f;
    if header_type != 0 {
        return Err(KAHCI_PCI_UNSUPPORTED);
    }

    // Intel AHCI 1.3.1 §2.1.11: ABAR is BAR5 at offset 0x24 (32-bit memory BAR).
    let bar5 = read_le32(config, 0x24);
    if (bar5 & 1) != 0 || ((bar5 >> 1) & 3) != 0 {
        return Err(KAHCI_PCI_REJECTED);
    }
    let abar = (bar5 & !0xFu32) as u64;
    let limit = 1u64 << phys_bits;
    if abar < 0x1000 || (abar & 0xFFF) != 0 || abar.saturating_add(0x2000) > limit {
        return Err(KAHCI_PCI_REJECTED);
    }

    Ok(AhciPciLayout {
        abar_address: abar,
        vendor_id,
        device_id,
        revision: byte_at(config, 0x08) as u32,
        class_code,
        reserved: 0,
    })
}

// ====== kahci_pci_probe ======

#[unsafe(no_mangle)]
pub unsafe extern "C" fn kahci_pci_probe(
    config: *const u8,
    bytes: u32,
    physical_bits: u32,
    output: *mut AhciPciLayout,
) -> u32 {
    if config.is_null() || output.is_null() || bytes != 256 || (output as usize & 7) != 0 {
        return KAHCI_PCI_REJECTED;
    }
    let slice = unsafe { core::slice::from_raw_parts(config, 256) };
    match parse_pci_slice(slice, physical_bits) {
        Ok(layout) => {
            unsafe {
                *output = layout;
            }
            KAHCI_PCI_READY
        }
        Err(code) => code,
    }
}

// ====== kahci_port_usable ======

// Intel AHCI 1.3.1 §3.3.10 (PxSSTS): DET bits 3:0 == 3, IPM bits 11:8 == 1; §3.3.9 PxSIG == 0x101.
#[unsafe(no_mangle)]
pub extern "C" fn kahci_port_usable(ssts: u32, sig: u32) -> u32 {
    let det = ssts & 0x0F;
    let ipm = (ssts >> 8) & 0x0F;
    if det == 3 && ipm == 1 && sig == SATA_SIG_ATA {
        1
    } else {
        0
    }
}

// ====== kahci_build_h2d_fis ======

// Serial ATA Rev 3.0 §10.3.4: 20-byte Register Host-to-Device FIS (type 0x27, C=1).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn kahci_build_h2d_fis(
    command: u8,
    lba: u64,
    sector_count: u16,
    fis_out: *mut u8,
) -> u32 {
    if fis_out.is_null() || (lba >> 48) != 0 {
        return 0;
    }
    if (command == ATA_CMD_READ_DMA_EXT || command == ATA_CMD_WRITE_DMA_EXT) && sector_count == 0 {
        return 0;
    }
    let fis = unsafe { core::slice::from_raw_parts_mut(fis_out, 20) };
    let mut i = 0usize;
    while i < 20 {
        if let Some(b) = fis.get_mut(i) {
            *b = 0;
        }
        i += 1;
    }

    let device = if command == ATA_CMD_IDENTIFY {
        0x00u8
    } else {
        0x40u8
    };

    let values: [(usize, u8); 11] = [
        (0, 0x27),
        (1, 0x80),
        (2, command),
        (4, (lba & 0xFF) as u8),
        (5, ((lba >> 8) & 0xFF) as u8),
        (6, ((lba >> 16) & 0xFF) as u8),
        (7, device),
        (8, ((lba >> 24) & 0xFF) as u8),
        (9, ((lba >> 32) & 0xFF) as u8),
        (10, ((lba >> 40) & 0xFF) as u8),
        (12, (sector_count & 0xFF) as u8),
    ];
    let mut k = 0usize;
    while k < values.len() {
        if let Some(&(idx, val)) = values.get(k) {
            if let Some(slot) = fis.get_mut(idx) {
                *slot = val;
            }
        }
        k += 1;
    }
    if let Some(slot) = fis.get_mut(13) {
        *slot = ((sector_count >> 8) & 0xFF) as u8;
    }
    1
}

// ====== kahci_identify_sectors ======

// ATA8-ACS §7.16.7: word 83 bit 10 indicates LBA48; words 100..103 hold LBA48 total sectors;
// words 60..61 hold LBA28 total sectors.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn kahci_identify_sectors(identify_512: *const u8, bytes: u32) -> u64 {
    if identify_512.is_null() || bytes != 512 {
        return 0;
    }
    let data = unsafe { core::slice::from_raw_parts(identify_512, 512) };
    let word83 = read_le16(data, 83 * 2);
    let lba48 = read_le64(data, 100 * 2);
    let lba28 = read_le32(data, 60 * 2) as u64;

    let sectors = if (word83 & (1 << 10)) != 0 && lba48 != 0 {
        lba48
    } else {
        lba28
    };
    if sectors == 0 || sectors > (1u64 << 40) {
        0
    } else {
        sectors
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_valid_ahci_pci_config() {
        let mut cfg = [0u8; 256];
        cfg[0..2].copy_from_slice(&0x8086u16.to_le_bytes());
        cfg[2..4].copy_from_slice(&0x2922u16.to_le_bytes());
        cfg[8] = 2;
        cfg[9] = 0x01;
        cfg[10] = 0x06;
        cfg[11] = 0x01;
        cfg[0x24..0x28].copy_from_slice(&0xFEBF_1000u32.to_le_bytes());

        let layout = parse_pci_slice(&cfg, 39).expect("valid AHCI config");
        assert_eq!(layout.vendor_id, 0x8086);
        assert_eq!(layout.device_id, 0x2922);
        assert_eq!(layout.abar_address, 0xFEBF_1000);

        // Wrong class
        cfg[10] = 0x01;
        assert_eq!(parse_pci_slice(&cfg, 39), Err(KAHCI_PCI_ABSENT));
    }

    #[test]
    fn builds_h2d_fis_and_parses_identify() {
        let mut fis = [0u8; 20];
        assert_eq!(
            unsafe {
                kahci_build_h2d_fis(
                    ATA_CMD_READ_DMA_EXT,
                    0x1122_3344_5566,
                    8,
                    fis.as_mut_ptr(),
                )
            },
            1
        );
        assert_eq!(fis[0], 0x27);
        assert_eq!(fis[1], 0x80);
        assert_eq!(fis[2], ATA_CMD_READ_DMA_EXT);
        assert_eq!(fis[4], 0x66);
        assert_eq!(fis[5], 0x55);
        assert_eq!(fis[6], 0x44);
        assert_eq!(fis[7], 0x40);
        assert_eq!(fis[8], 0x33);
        assert_eq!(fis[9], 0x22);
        assert_eq!(fis[10], 0x11);
        assert_eq!(fis[12], 8);

        let mut id = [0u8; 512];
        id[83 * 2..83 * 2 + 2].copy_from_slice(&(1u16 << 10).to_le_bytes());
        id[100 * 2..100 * 2 + 8].copy_from_slice(&1_032_192u64.to_le_bytes());
        assert_eq!(unsafe { kahci_identify_sectors(id.as_ptr(), 512) }, 1_032_192);
    }
}
