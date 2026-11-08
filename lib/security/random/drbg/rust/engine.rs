#[allow(dead_code)]
#[path = "../../../mac/hmac/rust/engine.rs"]
mod hmac;

use hmac::sha256::wipe::wipe;
use hmac::{HmacSha256, TAG_BYTES};

// ====== limiti SP 800-90A Rev. 1, Table 2 (SHA-256) ======

pub const SECURITY_STRENGTH_BITS  : u32   = 256;
pub const MINIMUM_ENTROPY_BYTES   : usize = 32;
pub const MINIMUM_NONCE_BYTES     : usize = 16;
pub const MAXIMUM_INPUT_BYTES     : usize = 1 << 32;
pub const MAXIMUM_REQUEST_BYTES   : usize = 1 << 16;
pub const MAXIMUM_RESEED_INTERVAL : u64   = 1 << 48;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DrbgError {
    EntropyTooShort,
    NonceTooShort,
    InputTooLong,
    RequestTooLong,
    InvalidReseedInterval,
    ReseedRequired,
}

pub struct HmacDrbg {
    key             : [u8; TAG_BYTES],
    value           : [u8; TAG_BYTES],
    reseed_counter  : u64,
    reseed_interval : u64,
}

// ====== controlli degli input ======

pub fn input_length_allowed(length: usize) -> bool {
    length <= MAXIMUM_INPUT_BYTES
}

fn check_inputs(inputs: &[&[u8]]) -> Result<(), DrbgError> {
    if inputs.iter().all(|input| input_length_allowed(input.len())) {
        Ok(())
    } else {
        Err(DrbgError::InputTooLong)
    }
}

fn keyed(key: &[u8; TAG_BYTES], pieces: &[&[u8]]) -> [u8; TAG_BYTES] {
    let mut hmac = HmacSha256::new(key).expect("chiave di 32 byte");
    for piece in pieces {
        hmac.update(piece).expect("input entro 2^32 byte");
    }
    hmac.finalize()
}

// ====== HmacDrbg::update (10.1.2.2) ======

impl HmacDrbg {
    fn update(&mut self, provided: &[&[u8]]) {
        let null = provided.iter().all(|piece| piece.is_empty());
        for separator in [0x00u8, 0x01] {
            let mut pieces: [&[u8]; 6] = [&[]; 6];
            pieces[0] = &self.value;
            pieces[1] = core::slice::from_ref(&separator);
            for (slot, piece) in pieces[2..].iter_mut().zip(provided) {
                *slot = piece;
            }
            self.key = keyed(&self.key, &pieces[..2 + provided.len()]);
            self.value = keyed(&self.key, &[&self.value]);
            if null {
                return;
            }
        }
    }

// ====== HmacDrbg::instantiate (10.1.2.3) ======

    /// Checks lengths. For 256-bit strength the caller must supply at least
    /// 256 bits of assessed min-entropy and a separate compliant nonce.
    pub fn instantiate(entropy: &[u8], nonce: &[u8], personalization: &[u8]) -> Result<Self, DrbgError> {
        if entropy.len() < MINIMUM_ENTROPY_BYTES {
            return Err(DrbgError::EntropyTooShort);
        }
        if nonce.len() < MINIMUM_NONCE_BYTES {
            return Err(DrbgError::NonceTooShort);
        }
        check_inputs(&[entropy, nonce, personalization])?;
        let mut drbg = Self {
            key             : [0x00; TAG_BYTES],
            value           : [0x01; TAG_BYTES],
            reseed_counter  : 1,
            reseed_interval : MAXIMUM_RESEED_INTERVAL,
        };
        drbg.update(&[entropy, nonce, personalization]);
        Ok(drbg)
    }

// ====== HmacDrbg::with_reseed_interval ======

    pub fn with_reseed_interval(mut self, interval: u64) -> Result<Self, DrbgError> {
        if interval == 0 || interval > MAXIMUM_RESEED_INTERVAL {
            return Err(DrbgError::InvalidReseedInterval);
        }
        self.reseed_interval = interval;
        Ok(self)
    }

// ====== HmacDrbg::reseed (10.1.2.4) ======

    /// Checks lengths. For 256-bit strength the caller must supply at least
    /// 256 bits of fresh, assessed min-entropy.
    pub fn reseed(&mut self, entropy: &[u8], additional: &[u8]) -> Result<(), DrbgError> {
        if entropy.len() < MINIMUM_ENTROPY_BYTES {
            return Err(DrbgError::EntropyTooShort);
        }
        check_inputs(&[entropy, additional])?;
        self.update(&[entropy, additional]);
        self.reseed_counter = 1;
        Ok(())
    }

// ====== HmacDrbg::generate (10.1.2.5) ======

    pub fn generate(&mut self, output: &mut [u8], additional: &[u8]) -> Result<(), DrbgError> {
        if output.len() > MAXIMUM_REQUEST_BYTES {
            return Err(DrbgError::RequestTooLong);
        }
        check_inputs(&[additional])?;
        if self.reseed_counter > self.reseed_interval {
            return Err(DrbgError::ReseedRequired);
        }
        if !additional.is_empty() {
            self.update(&[additional]);
        }
        for chunk in output.chunks_mut(TAG_BYTES) {
            self.value = keyed(&self.key, &[&self.value]);
            chunk.copy_from_slice(&self.value[..chunk.len()]);
        }
        self.update(&[additional]);
        self.reseed_counter += 1;
        Ok(())
    }

// ====== HmacDrbg::reseed_counter ======

    pub fn reseed_counter(&self) -> u64 {
        self.reseed_counter
    }
}

// ====== HmacDrbg::drop ======

impl Drop for HmacDrbg {
    fn drop(&mut self) {
        wipe(&mut self.key);
        wipe(&mut self.value);
        wipe(&mut self.reseed_counter);
        wipe(&mut self.reseed_interval);
    }
}
