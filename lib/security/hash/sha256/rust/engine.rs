#[allow(dead_code)]
#[path = "../../../memory/rust/wipe.rs"]
pub(crate) mod wipe;

use wipe::wipe;

// ====== costanti FIPS 180-4 ======

pub const DIGEST_BYTES          : usize = 32;
pub const BLOCK_BYTES           : usize = 64;
pub const MAXIMUM_MESSAGE_BYTES : u64   = (1 << 61) - 1;

pub const ROUND_CONSTANTS: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

pub const INITIAL_HASH: [u32; 8] = [
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Sha256Error {
    MessageTooLong,
}

#[derive(Clone)]
pub struct Sha256 {
    state    : [u32; 8],
    buffer   : [u8; BLOCK_BYTES],
    buffered : usize,
    length   : u64,
}

// ====== funzioni logiche ======

fn choose(x: u32, y: u32, z: u32) -> u32 {
    (x & y) ^ (!x & z)
}

fn majority(x: u32, y: u32, z: u32) -> u32 {
    (x & y) ^ (x & z) ^ (y & z)
}

fn big_sigma0(x: u32) -> u32 {
    x.rotate_right(2) ^ x.rotate_right(13) ^ x.rotate_right(22)
}

fn big_sigma1(x: u32) -> u32 {
    x.rotate_right(6) ^ x.rotate_right(11) ^ x.rotate_right(25)
}

fn small_sigma0(x: u32) -> u32 {
    x.rotate_right(7) ^ x.rotate_right(18) ^ (x >> 3)
}

fn small_sigma1(x: u32) -> u32 {
    x.rotate_right(17) ^ x.rotate_right(19) ^ (x >> 10)
}

// ====== compress ======

fn compress(state: &mut [u32; 8], block: &[u8; BLOCK_BYTES]) {
    let mut schedule = [0u32; 64];
    for (word, bytes) in schedule.iter_mut().zip(block.as_chunks::<4>().0) {
        *word = u32::from_be_bytes(*bytes);
    }
    for t in 16..64 {
        schedule[t] = small_sigma1(schedule[t - 2])
            .wrapping_add(schedule[t - 7])
            .wrapping_add(small_sigma0(schedule[t - 15]))
            .wrapping_add(schedule[t - 16]);
    }
    let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = *state;
    for (constant, word) in ROUND_CONSTANTS.iter().zip(schedule.iter()) {
        let first = h
            .wrapping_add(big_sigma1(e))
            .wrapping_add(choose(e, f, g))
            .wrapping_add(*constant)
            .wrapping_add(*word);
        let second = big_sigma0(a).wrapping_add(majority(a, b, c));
        h = g;
        g = f;
        f = e;
        e = d.wrapping_add(first);
        d = c;
        c = b;
        b = a;
        a = first.wrapping_add(second);
    }
    for (value, working) in state.iter_mut().zip([a, b, c, d, e, f, g, h]) {
        *value = value.wrapping_add(working);
    }
    wipe(&mut schedule);
}

// ====== Sha256 ======

impl Default for Sha256 {
    fn default() -> Self {
        Self::new()
    }
}

impl Sha256 {
    pub const fn new() -> Self {
        Self {
            state    : INITIAL_HASH,
            buffer   : [0; BLOCK_BYTES],
            buffered : 0,
            length   : 0,
        }
    }

    #[cfg(test)]
    pub fn with_length(length: u64) -> Self {
        let mut hasher = Self::new();
        hasher.length = length;
        hasher
    }

// ====== Sha256::update ======

    pub fn update(&mut self, data: &[u8]) -> Result<(), Sha256Error> {
        let added = u64::try_from(data.len()).map_err(|_| Sha256Error::MessageTooLong)?;
        let total = self.length.checked_add(added).ok_or(Sha256Error::MessageTooLong)?;
        if total > MAXIMUM_MESSAGE_BYTES {
            return Err(Sha256Error::MessageTooLong);
        }
        self.length = total;
        let mut rest = data;
        if self.buffered != 0 {
            let take = rest.len().min(BLOCK_BYTES - self.buffered);
            self.buffer[self.buffered..self.buffered + take].copy_from_slice(&rest[..take]);
            self.buffered += take;
            rest = &rest[take..];
            if self.buffered < BLOCK_BYTES {
                return Ok(());
            }
            compress(&mut self.state, &self.buffer);
            self.buffered = 0;
        }
        let (blocks, tail) = rest.as_chunks::<BLOCK_BYTES>();
        for block in blocks {
            compress(&mut self.state, block);
        }
        self.buffer[..tail.len()].copy_from_slice(tail);
        self.buffered = tail.len();
        Ok(())
    }

// ====== Sha256::finalize ======

    pub fn finalize(mut self) -> [u8; DIGEST_BYTES] {
        let bits = self.length << 3;
        self.buffer[self.buffered] = 0x80;
        self.buffer[self.buffered + 1..].fill(0);
        if self.buffered >= BLOCK_BYTES - 8 {
            compress(&mut self.state, &self.buffer);
            self.buffer.fill(0);
        }
        self.buffer[BLOCK_BYTES - 8..].copy_from_slice(&bits.to_be_bytes());
        compress(&mut self.state, &self.buffer);
        let mut digest = [0u8; DIGEST_BYTES];
        for (bytes, word) in digest.as_chunks_mut::<4>().0.iter_mut().zip(self.state) {
            *bytes = word.to_be_bytes();
        }
        digest
    }
}

// ====== Sha256::drop ======

impl Drop for Sha256 {
    fn drop(&mut self) {
        wipe(&mut self.state);
        wipe(&mut self.buffer);
        wipe(&mut self.buffered);
        wipe(&mut self.length);
    }
}

// ====== digest ======

pub fn digest(data: &[u8]) -> Result<[u8; DIGEST_BYTES], Sha256Error> {
    let mut hasher = Sha256::new();
    hasher.update(data)?;
    Ok(hasher.finalize())
}
