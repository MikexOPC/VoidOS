mod bytes;
#[cfg(test)]
pub use bytes::{DATA, EMPTY, LOSS};
pub const DENIED: u64 = 4;

#[derive(Debug, PartialEq, Eq)]
pub struct Channel {
    queue: bytes::Bytes,
    consumer: u32,
    generation: u64,
    enabled: bool,
}

// Fixed per-event budget, including metadata/alignment; no heap or buffer pointer.
const _: () = assert!(core::mem::size_of::<Channel>() <= 288);

impl Default for Channel {
    // ====== channel_default ======

    fn default() -> Self { Self::new() }
}

impl Channel {
    // ====== channel_new ======

    pub const fn new() -> Self {
        Self { queue: bytes::Bytes::new(), consumer: u32::MAX, generation: 0, enabled: false }
    }

    // ====== channel_enabled ======

    pub fn enabled(&self) -> bool { self.enabled }

    // ====== channel_open ======

    pub fn open(&mut self, consumer: u32) -> bool {
        if consumer >= 8 || self.enabled { return false; }
        self.consumer = consumer;
        self.enabled = true;
        true
    }

    // ====== channel_push ======

    pub fn push(&mut self, byte: u32) -> u32 {
        if !self.enabled || byte > 255 { return 0; }
        if self.queue.push(byte as u8) { 1 } else { 2 }
    }

    // ====== channel_take ======

    // Native context predicate authenticates the first generation before this call.
    pub fn take(&mut self, consumer: u32, generation: u64) -> u64 {
        if !self.enabled || consumer != self.consumer || generation == 0 ||
            (self.generation != 0 && generation != self.generation) { return DENIED; }
        self.generation = generation;
        self.queue.take()
    }

    // ====== channel_close ======

    pub fn close(&mut self) -> bool {
        if !self.enabled || !self.queue.idle() { return false; }
        *self = Self::new();
        true
    }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/payload/channel_test.rs"]
mod tests;
