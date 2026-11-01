#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU32, AtomicU64, Ordering::{Acquire, AcqRel}};

pub const PREPARED: u64 = 1;
pub const COMMITTED: u64 = 2;
pub const ARMED: u64 = 3;
pub const ENTERED: u64 = 4;
pub const PRIVATE: u64 = 5;
pub const PARKED: u64 = 6;
pub const QUARANTINED: u64 = 7;
const GENERATION_MAX: u64 = u32::MAX as u64;

pub struct Startup {
    count: AtomicU32,
    state: AtomicU64,
}

impl Startup {
    // ====== new ======

    pub const fn new() -> Self {
        Self { count: AtomicU32::new(0), state: AtomicU64::new(0) }
    }

    // ====== configure ======

    pub fn configure(&self, count: u32) -> u32 {
        u32::from((1..=64).contains(&count)
            && self.count.compare_exchange(0, count, AcqRel, Acquire).is_ok())
    }

    // ====== snapshot ======

    pub fn snapshot(&self) -> u64 { self.state.load(Acquire) }

    // ====== reserve ======

    pub fn reserve(&self, index: u32) -> u64 {
        let count = self.count.load(Acquire);
        if index == 0 || index >= count { return 0; }
        let old = self.snapshot();
        if !matches!(old & 7, 0 | PARKED) || ((old >> 3) & 63) + 1 != u64::from(index) { return 0; }
        let generation = old >> 9;
        if generation >= GENERATION_MAX { return 0; }
        let token = ((generation + 1) << 9) | (u64::from(index) << 3);
        if self.state.compare_exchange(old, token | PREPARED, AcqRel, Acquire).is_ok() { token } else { 0 }
    }

    // ====== advance ======

    pub fn advance(&self, token: u64, from: u64) -> u32 {
        if !Self::token_valid(token) || !matches!(from, PREPARED | COMMITTED | ARMED | ENTERED | PRIVATE) { return 0; }
        u32::from(self.state.compare_exchange(token | from, token | (from + 1), AcqRel, Acquire).is_ok())
    }

    // ====== cancel ======

    pub fn cancel(&self, token: u64) -> u32 {
        if !Self::token_valid(token) { return 0; }
        let previous = (token & !511) | ((((token >> 3) & 63) - 1) << 3);
        u32::from(self.state.compare_exchange(token | PREPARED, previous, AcqRel, Acquire).is_ok())
    }

    // ====== quarantine ======

    pub fn quarantine(&self, token: u64) -> u32 {
        if !Self::token_valid(token) { return 0; }
        // At most four forward phase changes can race this terminal transition.
        for _ in 0..5 {
            let old = self.snapshot();
            if old & !7 != token || !(COMMITTED..=PRIVATE).contains(&(old & 7)) { return 0; }
            if self.state.compare_exchange(old, token | QUARANTINED, AcqRel, Acquire).is_ok() { return 1; }
        }
        0
    }

    // ====== token_valid ======

    fn token_valid(token: u64) -> bool {
        token & 7 == 0 && (1..=GENERATION_MAX).contains(&(token >> 9)) && (token >> 3) & 63 != 0
    }
}
