#![forbid(unsafe_code)]
use core::sync::atomic::{AtomicU64, Ordering::{Acquire, Release, AcqRel}};

pub const INVALID: u64 = u64::MAX;

#[repr(C, align(64))]
pub struct Mailbox {
    phase: AtomicU64,
    request: AtomicU64,
    claimed: AtomicU64,
    ack: AtomicU64,
}

pub struct Service {
    count: AtomicU64,
    slots: [Mailbox; 64],
}

// ====== Mailbox_new ======

impl Mailbox {
    const fn new() -> Self {
        Self { phase: AtomicU64::new(0), request: AtomicU64::new(0),
            claimed: AtomicU64::new(0), ack: AtomicU64::new(0) }
    }
}

// ====== Service_new ======

impl Service {
    pub const fn new() -> Self {
        Self { count: AtomicU64::new(0), slots: [const { Mailbox::new() }; 64] }
    }

    // ====== configure ======

    pub fn configure(&self, count: u32) -> u32 {
        if !(1..=64).contains(&count) { return 0; }
        u32::from(self.count.compare_exchange(0, u64::from(count), AcqRel, Acquire).is_ok())
    }

    // ====== slot ======

    fn slot(&self, index: u32) -> Option<&Mailbox> {
        if index == 0 || u64::from(index) >= self.count.load(Acquire) { return None; }
        self.slots.get(index as usize)
    }

    // ====== ready ======

    pub fn ready(&self, index: u32, caller: u32) -> u32 {
        let Some(slot) = self.slot(index) else { return 0; };
        if caller != index { return 0; }
        u32::from(slot.phase.compare_exchange(0, 1, AcqRel, Acquire).is_ok())
    }

    // ====== publish ======

    pub fn publish(&self, index: u32, caller: u32) -> u64 {
        let Some(slot) = self.slot(index) else { return INVALID; };
        if caller != 0 || slot.phase.load(Acquire) != 1 { return INVALID; }
        let request = slot.request.load(Acquire);
        if slot.ack.load(Acquire) != request { return 0; }
        let Some(next) = request.checked_add(1).filter(|value| *value != INVALID) else { return INVALID; };
        if slot.request.compare_exchange(request, next, AcqRel, Acquire).is_err() { return 0; }
        if slot.phase.load(Acquire) == 1 { next } else { INVALID }
    }

    // ====== claim ======

    pub fn claim(&self, index: u32, caller: u32) -> u64 {
        let Some(slot) = self.slot(index) else { return INVALID; };
        if caller != index || slot.phase.load(Acquire) != 1 { return INVALID; }
        let request = slot.request.load(Acquire);
        let ack = slot.ack.load(Acquire);
        if request == ack { return 0; }
        if ack.checked_add(1) != Some(request) ||
            slot.claimed.compare_exchange(ack, request, AcqRel, Acquire).is_err() { return INVALID; }
        if slot.phase.load(Acquire) == 1 { request } else { INVALID }
    }

    // ====== complete ======

    pub fn complete(&self, index: u32, caller: u32, generation: u64) -> u32 {
        let Some(slot) = self.slot(index) else { return 0; };
        if caller != index || generation == 0 || generation == INVALID || slot.phase.load(Acquire) != 1 ||
            slot.request.load(Acquire) != generation || slot.claimed.load(Acquire) != generation { return 0; }
        u32::from(slot.ack.compare_exchange(generation - 1, generation, Release, Acquire).is_ok())
    }

    // ====== fail ======

    pub fn fail(&self, index: u32, caller: u32) -> u32 {
        let Some(slot) = self.slot(index) else { return 0; };
        if caller != 0 && caller != index { return 0; }
        let previous = slot.phase.swap(2, AcqRel);
        u32::from(previous != 2)
    }

    // ====== value ======

    pub fn value(&self, index: u32, field: u32) -> u64 {
        let Some(slot) = self.slot(index) else { return INVALID; };
        match field {
            0 => slot.phase.load(Acquire), 1 => slot.request.load(Acquire),
            2 => slot.claimed.load(Acquire), 3 => slot.ack.load(Acquire), _ => INVALID,
        }
    }
}

const _: () = assert!(core::mem::size_of::<Mailbox>() == 64 && core::mem::align_of::<Mailbox>() == 64);

#[cfg(test)]
#[path = "../../../../../../../../test/arch/x86/kernel/cpu/percpu/service/rust/mailbox/state_test.rs"]
mod tests;
