pub const EMPTY: u64 = 1;
pub const LOSS: u64 = 2;
pub const DATA: u64 = 0x100;

#[derive(Debug, PartialEq, Eq)]
pub struct Bytes {
    data: [u8; 256],
    head: u16,
    count: u16,
    loss: bool,
    lost: u32,
}

impl Default for Bytes {
    // ====== bytes_default ======

    fn default() -> Self { Self::new() }
}

impl Bytes {
    // ====== bytes_new ======

    pub const fn new() -> Self {
        Self { data: [0; 256], head: 0, count: 0, loss: false, lost: 0 }
    }

    // ====== bytes_push ======

    pub fn push(&mut self, byte: u8) -> bool {
        if self.count == 256 || self.loss {
            self.loss = true;
            self.lost = self.lost.saturating_add(1);
            return false;
        }
        let tail = (usize::from(self.head) + usize::from(self.count)) & 255;
        self.data[tail] = byte;
        self.count += 1;
        true
    }

    // ====== bytes_take ======

    pub fn take(&mut self) -> u64 {
        if self.count != 0 {
            let byte = self.data[usize::from(self.head)];
            self.data[usize::from(self.head)] = 0;
            self.head = (self.head + 1) & 255;
            self.count -= 1;
            return DATA | u64::from(byte);
        }
        if self.loss {
            self.loss = false;
            return LOSS | (u64::from(self.lost) << 32);
        }
        EMPTY
    }

    // ====== bytes_idle ======

    pub fn idle(&self) -> bool { self.count == 0 && !self.loss }
}

#[cfg(test)]
#[path = "../../test/payload/bytes_test.rs"]
mod tests;
