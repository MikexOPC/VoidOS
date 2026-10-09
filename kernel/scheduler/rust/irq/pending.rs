use core::sync::atomic::{AtomicU32, Ordering};

// Notification only, not an IRQ counter or payload queue. Static driver binding
// and quiescence belong to the backend; atomics do not certify that lifetime.
pub struct Pending { bits: AtomicU32 }

impl Default for Pending {
    // ====== pending_default ======

    fn default() -> Self { Self::new() }
}

impl Pending {
    // ====== pending_new ======

    pub const fn new() -> Self { Self { bits: AtomicU32::new(0) } }

    // ====== pending_post ======

    pub fn post(&self, event: u32) -> bool {
        if event >= 8 { return false; }
        self.bits.fetch_or(1 << event, Ordering::Release);
        true
    }

    // ====== pending_take ======

    pub fn take(&self) -> u32 { self.bits.swap(0, Ordering::AcqRel) }

    // ====== pending_restore ======

    pub fn restore(&self, mask: u32) -> bool {
        if mask & !0xFF != 0 { return false; }
        self.bits.fetch_or(mask, Ordering::Release);
        true
    }

    // ====== pending_empty ======

    pub fn empty(&self) -> bool { self.bits.load(Ordering::Acquire) == 0 }

    // ====== pending_contains ======

    pub fn contains(&self, event: u32) -> bool {
        event < 8 && self.bits.load(Ordering::Acquire) & (1 << event) != 0
    }
}

#[cfg(test)]
#[path = "../../test/irq/pending_test.rs"]
mod pending_test;
