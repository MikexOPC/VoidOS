#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU64, Ordering::SeqCst, Ordering::Acquire, Ordering::Release, Ordering::AcqRel};

#[repr(C, align(64))]
pub struct Coordinator {
    ready: AtomicU64,
    known: AtomicU64,
    online: AtomicU64,
    owner: AtomicU64,
    targets: AtomicU64,
    acknowledged: AtomicU64,
    missing: AtomicU64,
    phase: AtomicU64,
    outcome: AtomicU64,
    records: [AtomicU64; 64],
}

// ====== new ======

impl Coordinator {
    pub const fn new() -> Self {
        Self { ready: AtomicU64::new(0), known: AtomicU64::new(0), online: AtomicU64::new(0),
            owner: AtomicU64::new(0), targets: AtomicU64::new(0), acknowledged: AtomicU64::new(0),
            missing: AtomicU64::new(0), phase: AtomicU64::new(0), outcome: AtomicU64::new(0),
            records: [const { AtomicU64::new(0) }; 64] }
    }

    // ====== configure ======

    pub fn configure(&self, count: u32) -> u32 {
        if count == 0 || count > 64 || self.ready.compare_exchange(0, 1, AcqRel, Acquire).is_err() { return 0; }
        self.known.store(if count == 64 { u64::MAX } else { (1u64 << count) - 1 }, SeqCst);
        self.ready.store(2, Release);
        1
    }

    // ====== bit ======

    fn bit(&self, index: u32) -> Option<u64> {
        if index >= 64 || self.ready.load(Acquire) != 2 { return None; }
        let bit = 1u64 << index;
        if self.known.load(SeqCst) & bit == 0 { None } else { Some(bit) }
    }

    // ====== admit ======

    pub fn admit(&self, index: u32) -> u32 {
        let Some(bit) = self.bit(index) else { return 0; };
        if self.owner.load(SeqCst) != 0 { return 2; }
        if self.online.fetch_or(bit, SeqCst) & bit != 0 { return 0; }
        if self.owner.load(SeqCst) == 0 { 1 } else { 2 }
    }

    // ====== elect ======

    pub fn elect(&self, index: u32) -> u32 {
        let Some(bit) = self.bit(index) else { return 0; };
        let token = u64::from(index) + 1;
        let current = self.owner.load(SeqCst);
        if current != 0 { return if current == token { 3 } else { 2 }; }
        if self.online.load(SeqCst) & bit == 0 && self.record_state(index) == 0 { return 0; }
        match self.owner.compare_exchange(0, token, SeqCst, SeqCst) {
            Ok(_) => {
                self.targets.store(self.online.load(SeqCst) & !bit, Release);
                self.phase.store(1, Release);
                1
            }
            Err(winner) => if winner == token { 3 } else { 2 },
        }
    }

    // ====== record_claim ======

    pub fn record_claim(&self, index: u32) -> u32 {
        if self.bit(index).is_none() { return 0; }
        u32::from(self.records[index as usize].compare_exchange(0, 1, AcqRel, Acquire).is_ok())
    }

    // ====== record_publish ======

    pub fn record_publish(&self, index: u32) -> u32 {
        if self.bit(index).is_none() { return 0; }
        u32::from(self.records[index as usize].compare_exchange(1, 2, AcqRel, Acquire).is_ok())
    }

    // ====== record_state ======

    pub fn record_state(&self, index: u32) -> u32 {
        if self.bit(index).is_none() { return 3; }
        self.records[index as usize].load(Acquire) as u32
    }

    // ====== acknowledge ======

    pub fn acknowledge(&self, index: u32) -> u32 {
        let Some(bit) = self.bit(index) else { return 0; };
        let owner = self.owner.load(SeqCst);
        if owner == 0 || owner == u64::from(index) + 1 || self.record_state(index) != 2 { return 0; }
        u32::from(self.acknowledged.fetch_or(bit, AcqRel) & bit == 0)
    }

    // ====== finish ======

    pub fn finish(&self, index: u32, outcome: u64) -> u32 {
        if self.bit(index).is_none() || self.owner.load(SeqCst) != u64::from(index) + 1 ||
            outcome > 31 || self.phase.compare_exchange(1, 3, AcqRel, Acquire).is_err() { return 0; }
        let missing = self.targets.load(Acquire) & !self.acknowledged.load(Acquire);
        self.missing.store(missing, Release);
        self.outcome.store(outcome | u64::from(missing != 0), Release);
        self.phase.store(2, Release);
        1
    }

    // ====== value ======

    pub fn value(&self, field: u32) -> u64 {
        match field {
            0 => self.ready.load(Acquire), 1 => self.known.load(SeqCst), 2 => self.online.load(SeqCst),
            3 => self.owner.load(SeqCst), 4 => self.targets.load(Acquire), 5 => self.acknowledged.load(Acquire),
            6 => self.missing.load(Acquire), 7 => self.phase.load(Acquire), 8 => self.outcome.load(Acquire),
            _ => u64::MAX,
        }
    }
}

const _: () = assert!(core::mem::size_of::<Coordinator>() == 640 && core::mem::align_of::<Coordinator>() == 64);
const _: () = assert!(core::mem::offset_of!(Coordinator, records) == 72 && core::mem::offset_of!(Coordinator, owner) == 24);
