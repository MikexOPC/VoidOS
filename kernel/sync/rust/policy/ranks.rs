#![forbid(unsafe_code)]

// ====== ks_rank_enter ======

pub fn rank_enter(held: u32, next: u32, depth: u32) -> u32 {
    u32::from(depth < 8 && (depth == 0) == (held == 0) && next > held && next <= u16::MAX.into())
}

// ====== ks_ticket_available ======

pub fn ticket_available(next: u64) -> u32 {
    u32::from(next < u64::MAX)
}
