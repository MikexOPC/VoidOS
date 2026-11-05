#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU64, Ordering::{Acquire, Release, AcqRel, Relaxed}};

pub const INVALID: u64 = u64::MAX;
pub const OFFERED: u64 = 1;
pub const CLAIMED: u64 = 2;
pub const ADMITTED: u64 = 3;
pub const FAILED: u64 = 4;
const MAX_TOKEN: u64 = (1 << 61) - 1;

#[repr(align(64))]
struct Slot { value: AtomicU64 }
pub struct Admission {
    count: AtomicU64,
    group: AtomicU64,
    slots: [Slot; 64],
}

impl Admission {
    // ====== admission_new ======

    pub const fn new() -> Self {
        Self { count: AtomicU64::new(0), group: AtomicU64::new(0),
            slots: [const { Slot { value: AtomicU64::new(0) } }; 64] }
    }

    // ====== admission_configure ======

    pub fn configure(&self, count: u32, group: u64, caller: u32) -> u32 {
        if caller != 0 || !(2..=64).contains(&count) || group == 0 || group == INVALID ||
            self.count.compare_exchange(0,65,AcqRel,Acquire).is_err() { return 0; }
        self.group.store(group,Relaxed);
        self.count.store(u64::from(count),Release);
        1
    }

    // ====== admission_slot ======

    fn slot(&self, cpu: u32, group: u64) -> Option<&AtomicU64> {
        let count = self.count.load(Acquire);
        if !(2..=64).contains(&count) || cpu == 0 || u64::from(cpu) >= count ||
            group != self.group.load(Relaxed) { return None; }
        Some(&self.slots[cpu as usize].value)
    }

    // ====== admission_offer ======

    pub fn offer(&self, cpu: u32, caller: u32, token: u64, group: u64) -> u32 {
        let Some(slot) = self.slot(cpu,group) else { return 0; };
        if caller != 0 || token == 0 || token > MAX_TOKEN { return 0; }
        u32::from(slot.compare_exchange(0,(token << 3) | OFFERED,Release,Relaxed).is_ok())
    }

    // ====== admission_claim ======

    pub fn claim(&self, cpu: u32, caller: u32, group: u64) -> u64 {
        if caller != cpu || cpu == 0 || cpu >= 64 { return INVALID; }
        let Some(slot) = self.slot(cpu,group) else { return 0; };
        let value = slot.load(Acquire);
        if value & 7 != OFFERED { return 0; }
        if slot.compare_exchange(value,(value & !7) | CLAIMED,AcqRel,Acquire).is_ok() {
            value >> 3
        } else { 0 }
    }

    // ====== admission_admit ======

    pub fn admit(&self, cpu: u32, caller: u32, token: u64, group: u64) -> u32 {
        let Some(slot) = self.slot(cpu,group) else { return 0; };
        if caller != cpu || token == 0 || token > MAX_TOKEN { return 0; }
        u32::from(slot.compare_exchange((token << 3) | CLAIMED,(token << 3) | ADMITTED,
            Release,Relaxed).is_ok())
    }

    // ====== admission_fail ======

    pub fn fail(&self, cpu: u32, caller: u32, token: u64, group: u64) -> u32 {
        let Some(slot) = self.slot(cpu,group) else { return 0; };
        if (caller != 0 && caller != cpu) || token == 0 || token > MAX_TOKEN { return 0; }
        for _ in 0..4 {
            let value = slot.load(Acquire);
            if value >> 3 != token || !(OFFERED..=FAILED).contains(&(value & 7)) { return 0; }
            if value & 7 == FAILED { return 1; }
            if slot.compare_exchange(value,(token << 3) | FAILED,AcqRel,Acquire).is_ok() { return 1; }
        }
        0
    }

    // ====== admission_value ======

    pub fn value(&self, cpu: u32, group: u64) -> u64 {
        self.slot(cpu,group).map_or(INVALID,|slot| slot.load(Acquire))
    }
}

#[cfg(test)]
#[path = "../../../../../../../test/arch/x86/kernel/scheduler/admission/rust/state/owner_test.rs"]
mod tests;
