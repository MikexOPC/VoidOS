#![forbid(unsafe_code)]

// ====== features ======

// VirtIO 1.2 6: VERSION_1 bit 32 is required; ACCESS_PLATFORM bit 33 is accepted only when
// platform = 1 (the owned IOMMU domain translates this device) and is then also required,
// because a device without it would bypass the IOMMU. Without translation the bit is never
// accepted; a device that needs it fails FEATURES_OK on its own.
pub fn features(offered: u64, platform: u32) -> u64 {
    const VERSION: u64 = 1u64 << 32;
    const ACCESS: u64 = 1u64 << 33;
    if platform > 1 || offered & VERSION == 0 { return 0; }
    if platform == 0 { return VERSION; }
    if offered & ACCESS == 0 { 0 } else { VERSION | ACCESS }
}

// ====== register_valid ======

pub fn register_valid(offset: u32, width: u32, write: u32) -> u32 {
    if write > 1 { return 0; }
    u32::from(match (offset, width) {
        (0 | 8 | 12, 4) | (20, 1) => true,
        (4, 4) => write == 0,
        _ => false,
    })
}

#[cfg(test)]
#[path="../../../../../../../test/kernel/drivers/video/virtio/initialization/rust/policy/policy_test.rs"]
mod tests;
