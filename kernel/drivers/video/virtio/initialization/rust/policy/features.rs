#![forbid(unsafe_code)]

// ====== features ======

pub fn features(offered: u64) -> u64 {
    offered & (1u64 << 32)
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
