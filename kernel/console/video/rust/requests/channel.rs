#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU64, Ordering};

pub struct Channel {
    token: AtomicU64,
    flags: AtomicU64,
}

impl Channel {
    // ====== new ======

    pub const fn new() -> Self {
        Self { token: AtomicU64::new(0), flags: AtomicU64::new(0) }
    }

    // ====== publish ======

    pub fn publish(&self, cpu: u32, token: u64) -> bool {
        cpu == 0 && token != 0 && self.token.compare_exchange(0, token,
            Ordering::Release, Ordering::Relaxed).is_ok()
    }

    // ====== enter ======

    pub fn enter(&self, cpu: u32, token: u64, producer: u32) -> bool {
        cpu == 1 && producer < 2 && token != 0 && self.token.load(Ordering::Acquire) == token &&
            self.flags.load(Ordering::Acquire) & (4 | (1 << producer)) == 0
    }

    // ====== complete ======

    pub fn complete(&self, cpu: u32, token: u64, producer: u32) -> bool {
        if !self.enter(cpu, token, producer) { return false; }
        let bit = 1 << producer;
        let mut flags = self.flags.load(Ordering::Acquire);
        for _ in 0..8 {
            if flags & (4 | bit) != 0 { return false; }
            match self.flags.compare_exchange(flags, flags | bit, Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => return true,
                Err(current) => flags = current,
            }
        }
        false
    }

    // ====== mask ======

    pub fn mask(&self, token: u64) -> u64 {
        if token == 0 || self.token.load(Ordering::Acquire) != token { return u64::MAX; }
        self.flags.load(Ordering::Acquire)
    }

    // ====== close ======

    pub fn close(&self, cpu: u32, token: u64) -> bool {
        cpu == 0 && token != 0 && self.token.load(Ordering::Acquire) == token &&
            self.flags.compare_exchange(3, 7, Ordering::AcqRel, Ordering::Acquire).is_ok()
    }
}

impl Default for Channel {
    // ====== default ======

    fn default() -> Self { Self::new() }
}
