#![cfg_attr(not(test), no_std)]

#[allow(dead_code, unused_attributes)]
#[path = "../../../lib/security/entropy/rust/health.rs"]
mod health;

#[allow(dead_code)]
#[path = "../../../lib/security/random/drbg/rust/engine.rs"]
mod drbg;

use core::mem::{align_of, size_of};
use core::slice;
use drbg::HmacDrbg;
use health::{HealthConfig, HealthMonitor, HealthStatus};

// State storage passed from C is uint64_t[12] = 96 bytes, 8-byte aligned.
const STATE_BYTES: usize = 96;
const _: () = assert!(size_of::<HmacDrbg>() <= STATE_BYTES);
const _: () = assert!(align_of::<HmacDrbg>() <= align_of::<u64>());

// ====== ksec_health_check_u64 ======

/// # Safety
/// `samples` must point to `count` readable `u64` values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ksec_health_check_u64(samples: *const u64, count: u32) -> u32 {
    if samples.is_null() || count < health::STARTUP_SAMPLES {
        return 0;
    }
    // Conservative 8 bits/sample min-entropy estimate for 64-bit hardware RNG words:
    // RCT cutoff = ceil(20000 / 8000) + 1 = 4; APT cutoff = 64 over 512-sample window.
    let config = HealthConfig {
        min_entropy_millibits: 8_000,
        false_positive_exponent: 20,
        binary: false,
        proportion_cutoff: 64,
    };
    let Some(mut monitor) = HealthMonitor::new(config) else {
        return 0;
    };
    let data = unsafe { slice::from_raw_parts(samples, count as usize) };
    for &sample in data {
        match monitor.feed(sample) {
            HealthStatus::Startup | HealthStatus::Healthy => {}
            _ => return 0,
        }
    }
    u32::from(monitor.status() == HealthStatus::Healthy)
}

// ====== ksec_drbg_instantiate ======

/// # Safety
/// `state_words` must point to 12 writable `u64` words; `entropy` and `nonce` must be readable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ksec_drbg_instantiate(
    state_words: *mut u64,
    entropy: *const u8,
    entropy_len: u32,
    nonce: *const u8,
    nonce_len: u32,
) -> u32 {
    if state_words.is_null() || entropy.is_null() || nonce.is_null() {
        return 0;
    }
    let ent = unsafe { slice::from_raw_parts(entropy, entropy_len as usize) };
    let non = unsafe { slice::from_raw_parts(nonce, nonce_len as usize) };
    let Ok(instance) = HmacDrbg::instantiate(ent, non, b"VoidOS-kernel-csprng-v1") else {
        return 0;
    };
    unsafe {
        core::ptr::write(state_words.cast::<HmacDrbg>(), instance);
    }
    1
}

// ====== ksec_drbg_generate ======

/// # Safety
/// `state_words` must hold an instantiated `HmacDrbg`; `out` must be writable for `out_len` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ksec_drbg_generate(
    state_words: *mut u64,
    out: *mut u8,
    out_len: u32,
) -> u32 {
    if state_words.is_null() || (out.is_null() && out_len > 0) {
        return 0;
    }
    let instance = unsafe { &mut *state_words.cast::<HmacDrbg>() };
    let target = if out_len == 0 {
        &mut [][..]
    } else {
        unsafe { slice::from_raw_parts_mut(out, out_len as usize) }
    };
    u32::from(instance.generate(target, &[]).is_ok())
}

// ====== ksec_drbg_reseed ======

/// # Safety
/// `state_words` must hold an instantiated `HmacDrbg`; `entropy` must be readable for `entropy_len` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ksec_drbg_reseed(
    state_words: *mut u64,
    entropy: *const u8,
    entropy_len: u32,
) -> u32 {
    if state_words.is_null() || entropy.is_null() {
        return 0;
    }
    let instance = unsafe { &mut *state_words.cast::<HmacDrbg>() };
    let ent = unsafe { slice::from_raw_parts(entropy, entropy_len as usize) };
    u32::from(instance.reseed(ent, &[]).is_ok())
}

// ====== ksec_drbg_wipe ======

/// # Safety
/// `state_words` must point to 12 writable `u64` words.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ksec_drbg_wipe(state_words: *mut u64) {
    if state_words.is_null() {
        return;
    }
    let bytes = unsafe { slice::from_raw_parts_mut(state_words.cast::<u8>(), STATE_BYTES) };
    for slot in bytes {
        unsafe { core::ptr::write_volatile(slot, 0) };
    }
}

#[cfg(not(test))]
unsafe extern "C" {
    fn ks_fail_stop(reason: *const u8) -> !;
}

#[cfg(not(test))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { ks_fail_stop(b"CSPRNG_PANIC\0".as_ptr()) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn health_accepts_varied_and_rejects_stuck() {
        let mut varied = [0u64; 1024];
        for (i, slot) in varied.iter_mut().enumerate() {
            *slot = (i as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(1);
        }
        assert_eq!(unsafe { ksec_health_check_u64(varied.as_ptr(), 1024) }, 1);

        let stuck = [0xAAAA_AAAA_AAAA_AAAAu64; 1024];
        assert_eq!(unsafe { ksec_health_check_u64(stuck.as_ptr(), 1024) }, 0);

        let mut apt_fail = varied;
        for i in 0..100 {
            apt_fail[i * 2] = 0xDEAD_BEEF;
        }
        assert_eq!(unsafe { ksec_health_check_u64(apt_fail.as_ptr(), 1024) }, 0);
    }

    #[test]
    fn drbg_instantiate_generate_reseed_wipe() {
        let mut state = [0u64; 12];
        let entropy = [0x11u8; 32];
        let nonce = [0x22u8; 16];
        assert_eq!(unsafe { ksec_drbg_instantiate(state.as_mut_ptr(), entropy.as_ptr(), 32, nonce.as_ptr(), 16) }, 1);
        let mut a = [0u8; 32];
        let mut b = [0u8; 32];
        assert_eq!(unsafe { ksec_drbg_generate(state.as_mut_ptr(), a.as_mut_ptr(), 32) }, 1);
        assert_eq!(unsafe { ksec_drbg_generate(state.as_mut_ptr(), b.as_mut_ptr(), 32) }, 1);
        assert_ne!(a, b);
        let reseed_ent = [0x33u8; 32];
        assert_eq!(unsafe { ksec_drbg_reseed(state.as_mut_ptr(), reseed_ent.as_ptr(), 32) }, 1);
        let mut c = [0u8; 32];
        assert_eq!(unsafe { ksec_drbg_generate(state.as_mut_ptr(), c.as_mut_ptr(), 32) }, 1);
        assert_ne!(b, c);
        unsafe { ksec_drbg_wipe(state.as_mut_ptr()) };
        assert_eq!(state, [0u64; 12]);
    }
}
