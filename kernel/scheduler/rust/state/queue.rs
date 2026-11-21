pub const CAPACITY: usize = 8;
pub const MIN_CAPACITY: usize = 8;
pub const MAX_CAPACITY: usize = 256;
pub const NONE: u32 = u32::MAX;

#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Queue {
    entries: [u32; CAPACITY],
    head: usize,
    count: usize,
}

// ====== queue_new ======

#[cfg(test)]
impl Queue {
    pub const fn new() -> Self {
        Self { entries: [NONE; CAPACITY], head: 0, count: 0 }
    }

    // ====== queue_len ======

    pub fn len(&self) -> usize { self.count }

    // ====== queue_contains ======

    pub fn contains(&self, index: u32) -> bool {
        if self.head >= CAPACITY || self.count > CAPACITY { return false; }
        (0..self.count).any(|i| self.entries[(self.head + i) % CAPACITY] == index)
    }

    // ====== queue_validate ======

    pub fn validate(&self) -> bool {
        if self.head >= CAPACITY || self.count > CAPACITY { return false; }
        for offset in 0..CAPACITY {
            let value = self.entries[(self.head + offset) % CAPACITY];
            if offset >= self.count {
                if value != NONE { return false; }
            } else {
                if value as usize >= CAPACITY { return false; }
                for previous in 0..offset {
                    if self.entries[(self.head + previous) % CAPACITY] == value {
                        return false;
                    }
                }
            }
        }
        true
    }

    // ====== queue_push ======

    pub fn push(&mut self, index: u32) -> bool {
        if !self.validate() || index as usize >= CAPACITY ||
            self.count == CAPACITY || self.contains(index) { return false; }
        self.entries[(self.head + self.count) % CAPACITY] = index;
        self.count += 1;
        true
    }

    // ====== queue_pop ======

    pub fn pop(&mut self) -> Option<u32> {
        if !self.validate() || self.count == 0 { return None; }
        let index = self.entries[self.head];
        self.entries[self.head] = NONE;
        self.head = (self.head + 1) % CAPACITY;
        self.count -= 1;
        Some(index)
    }

    // ====== queue_take_first ======

    pub fn take_first(&mut self, eligible: impl Fn(u32) -> bool) -> Option<u32> {
        if !self.validate() { return None; }
        let offset = (0..self.count).find(|&i| eligible(self.entries[(self.head + i) % CAPACITY]))?;
        let index = self.entries[(self.head + offset) % CAPACITY];
        for i in offset..self.count - 1 {
            self.entries[(self.head + i) % CAPACITY] = self.entries[(self.head + i + 1) % CAPACITY];
        }
        self.entries[(self.head + self.count - 1) % CAPACITY] = NONE;
        self.count -= 1;
        Some(index)
    }
}

// Circular queue over external storage of `cap` entries. The owner binds `entries` to one bank
// of its storage; head and count live in the ring itself.
#[derive(Clone, Copy)]
pub(super) struct Ring {
    pub(super) entries: *mut u32,
    pub(super) cap: usize,
    pub(super) head: usize,
    pub(super) count: usize,
}

// ====== ring_view ======

impl Ring {
    fn view(&self) -> &[u32] {
        // SAFETY: the owner binds `entries` to `cap` initialized u32 of its live storage.
        unsafe { core::slice::from_raw_parts(self.entries, self.cap) }
    }

    // ====== ring_view_mut ======

    fn view_mut(&mut self) -> &mut [u32] {
        // SAFETY: same binding as view; the ring is the only user of this bank region.
        unsafe { core::slice::from_raw_parts_mut(self.entries, self.cap) }
    }

    // ====== ring_len ======

    pub(super) fn len(&self) -> usize { self.count }

    // ====== ring_contains ======

    pub(super) fn contains(&self, index: u32) -> bool {
        if self.head >= self.cap || self.count > self.cap { return false; }
        let entries = self.view();
        (0..self.count).any(|i| entries[(self.head + i) % self.cap] == index)
    }

    // ====== ring_validate ======

    pub(super) fn validate(&self) -> bool {
        if self.cap == 0 || self.cap > MAX_CAPACITY || self.head >= self.cap || self.count > self.cap {
            return false;
        }
        let entries = self.view();
        let mut seen = [0u64; MAX_CAPACITY / 64];
        for offset in 0..self.cap {
            let value = entries[(self.head + offset) % self.cap];
            if offset >= self.count {
                if value != NONE { return false; }
            } else {
                if value as usize >= self.cap { return false; }
                let (word, bit) = (value as usize / 64, value % 64);
                if (seen[word] >> bit) & 1 != 0 { return false; }
                seen[word] |= 1 << bit;
            }
        }
        true
    }

    // ====== ring_push ======

    pub(super) fn push(&mut self, index: u32) -> bool {
        if !self.validate() || index as usize >= self.cap ||
            self.count == self.cap || self.contains(index) { return false; }
        let at = (self.head + self.count) % self.cap;
        self.view_mut()[at] = index;
        self.count += 1;
        true
    }

    // ====== ring_pop ======

    #[cfg(test)]
    pub(super) fn pop(&mut self) -> Option<u32> {
        if !self.validate() || self.count == 0 { return None; }
        let head = self.head;
        let index = self.view()[head];
        self.view_mut()[head] = NONE;
        self.head = (self.head + 1) % self.cap;
        self.count -= 1;
        Some(index)
    }

    // ====== ring_take_first ======

    pub(super) fn take_first(&mut self, eligible: impl Fn(u32) -> bool) -> Option<u32> {
        if !self.validate() { return None; }
        let (head, count, cap) = (self.head, self.count, self.cap);
        let entries = self.view_mut();
        let offset = (0..count).find(|&i| eligible(entries[(head + i) % cap]))?;
        let index = entries[(head + offset) % cap];
        for i in offset..count - 1 {
            entries[(head + i) % cap] = entries[(head + i + 1) % cap];
        }
        entries[(head + count - 1) % cap] = NONE;
        self.count -= 1;
        Some(index)
    }
}

// ====== ring_eq ======

impl PartialEq for Ring {
    fn eq(&self, other: &Self) -> bool {
        self.cap == other.cap && self.head == other.head && self.count == other.count &&
            self.view() == other.view()
    }
}

impl Eq for Ring {}

// ====== ring_debug ======

impl core::fmt::Debug for Ring {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Ring").field("head", &self.head).field("count", &self.count)
            .field("entries", &self.view()).finish()
    }
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/queue_test.rs"]
mod queue_test;
