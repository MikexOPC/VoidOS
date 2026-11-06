#[allow(dead_code)]
#[path = "../../../hash/sha256/rust/engine.rs"]
mod sha256;

#[allow(dead_code)]
#[path = "../../../memory/rust/compare.rs"]
mod compare;

use compare::equal;
use sha256::wipe::Wiping;
use sha256::{BLOCK_BYTES, DIGEST_BYTES, Sha256};

pub use sha256::Sha256Error;

// ====== costanti FIPS 198-1 ======

pub const TAG_BYTES : usize = DIGEST_BYTES;
const INNER_PAD     : u8    = 0x36;
const OUTER_PAD     : u8    = 0x5c;

#[derive(Clone)]
pub struct HmacSha256 {
    inner : Sha256,
    outer : Sha256,
}

// ====== keyed_hash ======

fn keyed_hash(key_block: &[u8; BLOCK_BYTES], pad: u8) -> Result<Sha256, Sha256Error> {
    let mut padded = Wiping::new([0u8; BLOCK_BYTES]);
    for (byte, key) in padded.iter_mut().zip(key_block) {
        *byte = key ^ pad;
    }
    let mut hasher = Sha256::new();
    hasher.update(&*padded)?;
    Ok(hasher)
}

// ====== HmacSha256::new ======

impl HmacSha256 {
    pub fn new(key: &[u8]) -> Result<Self, Sha256Error> {
        let mut key_block = Wiping::new([0u8; BLOCK_BYTES]);
        if key.len() > BLOCK_BYTES {
            let hashed = Wiping::new(sha256::digest(key)?);
            key_block[..DIGEST_BYTES].copy_from_slice(&*hashed);
        } else {
            key_block[..key.len()].copy_from_slice(key);
        }
        Ok(Self {
            inner : keyed_hash(&key_block, INNER_PAD)?,
            outer : keyed_hash(&key_block, OUTER_PAD)?,
        })
    }

// ====== HmacSha256::update ======

    pub fn update(&mut self, data: &[u8]) -> Result<(), Sha256Error> {
        self.inner.update(data)
    }

// ====== HmacSha256::finalize ======

    pub fn finalize(self) -> [u8; TAG_BYTES] {
        let Self { inner, mut outer } = self;
        let inner_digest = Wiping::new(inner.finalize());
        outer.update(&*inner_digest).expect("un blocco piu 32 byte");
        outer.finalize()
    }

// ====== HmacSha256::verify ======

    pub fn verify(self, tag: &[u8]) -> bool {
        let computed = Wiping::new(self.finalize());
        equal(&*computed, tag)
    }
}

// ====== mac ======

pub fn mac(key: &[u8], data: &[u8]) -> Result<[u8; TAG_BYTES], Sha256Error> {
    let mut hmac = HmacSha256::new(key)?;
    hmac.update(data)?;
    Ok(hmac.finalize())
}
