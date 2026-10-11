pub const CAPACITY: usize = 8;
pub const NONE: u32 = u32::MAX;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Queue {
    entries: [u32; CAPACITY],
    head: usize,
    count: usize,
}

// ====== queue_new ======

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
}

#[cfg(test)]
#[path = "../../../../test/kernel/scheduler/state/queue_test.rs"]
mod queue_test;
