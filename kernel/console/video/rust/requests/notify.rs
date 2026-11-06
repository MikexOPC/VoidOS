#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU64, Ordering};

const MODE_MASK: u64 = 7;
const LIVE: u64 = 1;
const SEALED: u64 = 2;
const CLOSED: u64 = 3;
const FAILED: u64 = 4;
const REF_UNIT: u64 = 8;
const PRODUCERS: u32 = 2;
const CAS_LIMIT: u32 = 32;

pub struct Notify {
    token: AtomicU64,
    control: AtomicU64,
    published: AtomicU64,
    observed: AtomicU64,
    pending: AtomicU64,
    slots: [AtomicU64; 2],
}

impl Default for Notify {
    // ====== default ======

    fn default() -> Self { Self::new() }
}

impl Notify {
    // ====== new ======

    pub const fn new() -> Self {
        Self { token: AtomicU64::new(0), control: AtomicU64::new(LIVE),
            published: AtomicU64::new(0), observed: AtomicU64::new(0), pending: AtomicU64::new(0),
            slots: [AtomicU64::new(0), AtomicU64::new(0)] }
    }

    // ====== matches ======

    fn matches(&self, token: u64) -> bool {
        token != 0 && self.token.load(Ordering::Acquire) == token
    }

    // ====== publish ======

    pub fn publish(&self, cpu: u32, token: u64) -> bool {
        cpu == 0 && token != 0 && self.token.compare_exchange(0, token,
            Ordering::Release, Ordering::Relaxed).is_ok()
    }

    // ====== begin ======

    pub fn begin(&self, cpu: u32, token: u64, producer: u32) -> u64 {
        if cpu != 1 || producer >= PRODUCERS || !self.matches(token) { return 0; }
        let mut control = self.control.load(Ordering::Acquire);
        let mut acquired = false;
        for _ in 0..CAS_LIMIT {
            if control & MODE_MASK != LIVE || control / REF_UNIT >= u64::from(PRODUCERS) { return 0; }
            match self.control.compare_exchange(control, control + REF_UNIT, Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => { acquired = true; break; }
                Err(current) => control = current,
            }
        }
        if !acquired { return 0; }
        let slot = &self.slots[producer as usize];
        if slot.compare_exchange(0, 1, Ordering::AcqRel, Ordering::Acquire).is_err() {
            self.control.fetch_sub(REF_UNIT, Ordering::Release);
            return 0;
        }
        let mut previous = self.published.load(Ordering::Acquire);
        for _ in 0..CAS_LIMIT {
            if previous >= u64::MAX / 2 { break; }
            match self.published.compare_exchange(previous, previous + 1, Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => {
                    let send = u64::from(self.pending.swap(1, Ordering::AcqRel) == 0);
                    let notice = ((previous + 1) << 1) | send;
                    slot.store(notice, Ordering::Release);
                    return notice;
                }
                Err(current) => previous = current,
            }
        }
        self.control.fetch_or(FAILED, Ordering::AcqRel);
        slot.store(0, Ordering::Release);
        self.control.fetch_sub(REF_UNIT, Ordering::Release);
        0
    }

    // ====== finish ======

    pub fn finish(&self, cpu: u32, token: u64, producer: u32, notice: u64) -> bool {
        if cpu != 1 || producer >= PRODUCERS || notice < 2 || !self.matches(token) ||
            !matches!(self.control.load(Ordering::Acquire) & MODE_MASK, LIVE | SEALED) { return false; }
        if self.slots[producer as usize].compare_exchange(notice, 0,
            Ordering::AcqRel, Ordering::Acquire).is_err() { return false; }
        self.control.fetch_sub(REF_UNIT, Ordering::Release);
        true
    }

    // ====== take ======

    pub fn take(&self, cpu: u32, token: u64) -> u64 {
        if cpu != 0 || !self.matches(token) ||
            !matches!(self.control.load(Ordering::Acquire) & MODE_MASK, LIVE | SEALED) { return u64::MAX; }
        self.pending.swap(0, Ordering::AcqRel);
        self.published.load(Ordering::Acquire)
    }

    // ====== acknowledge ======

    pub fn acknowledge(&self, cpu: u32, token: u64, sequence: u64) -> bool {
        if cpu != 0 || !self.matches(token) ||
            !matches!(self.control.load(Ordering::Acquire) & MODE_MASK, LIVE | SEALED) ||
            sequence > self.published.load(Ordering::Acquire) { return false; }
        self.observed.fetch_max(sequence, Ordering::AcqRel);
        true
    }

    // ====== seal ======

    pub fn seal(&self, cpu: u32, token: u64) -> bool {
        if cpu != 0 || !self.matches(token) { return false; }
        let mut previous = self.control.load(Ordering::Acquire);
        for _ in 0..CAS_LIMIT {
            if previous & MODE_MASK != LIVE { return false; }
            match self.control.compare_exchange(previous, (previous & !MODE_MASK) | SEALED,
                Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => return true,
                Err(current) => previous = current,
            }
        }
        false
    }

    // ====== close ======

    pub fn close(&self, cpu: u32, token: u64, hardware_quiet: bool, queue_empty: bool) -> bool {
        if cpu != 0 || !self.matches(token) || !hardware_quiet || !queue_empty ||
            self.control.load(Ordering::Acquire) != SEALED ||
            self.pending.load(Ordering::Acquire) != 0 ||
            self.observed.load(Ordering::Acquire) != self.published.load(Ordering::Acquire) ||
            self.slots.iter().any(|slot| slot.load(Ordering::Acquire) != 0) { return false; }
        self.control.compare_exchange(SEALED, CLOSED, Ordering::AcqRel, Ordering::Acquire).is_ok()
    }

    // ====== query ======

    pub fn query(&self, token: u64, field: u32) -> u64 {
        if !self.matches(token) { return u64::MAX; }
        match field {
            0 => self.control.load(Ordering::Acquire),
            1 => self.published.load(Ordering::Acquire),
            2 => self.observed.load(Ordering::Acquire),
            3 => self.pending.load(Ordering::Acquire),
            4 | 5 => self.slots[(field - 4) as usize].load(Ordering::Acquire),
            _ => u64::MAX,
        }
    }
}

#[cfg(test)]
#[path = "../../../../../test/kernel/console/video/rust/requests/notify_test.rs"]
mod tests;
