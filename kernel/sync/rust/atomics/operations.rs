#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]
use core::sync::atomic::{AtomicU64, Ordering};
#[path = "../policy/ranks.rs"]
mod policy;
const _: () = assert!(size_of::<AtomicU64>() == 8 && align_of::<AtomicU64>() == 8);
#[cfg(not(target_has_atomic = "64"))]
compile_error!("Kernel synchronization requires native atomic64 support");

// ====== ks_rank_enter ======

#[unsafe(no_mangle)]
pub extern "C" fn ks_rank_enter(held: u32, next: u32, depth: u32) -> u32 {
    policy::rank_enter(held, next, depth)
}

// ====== ks_ticket_available ======

#[unsafe(no_mangle)]
pub extern "C" fn ks_ticket_available(next: u64) -> u32 {
    policy::ticket_available(next)
}

// ====== ks_atomic_load_relaxed ======

/// # Safety
/// Value is live RW memory, aligned to 8, with concurrent accesses exclusively atomic64.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ks_atomic_load_relaxed(value: *const u64) -> u64 {
    // SAFETY: the C lock owner guarantees the pointer contract for this call.
    unsafe { AtomicU64::from_ptr(value.cast_mut()) }.load(Ordering::Relaxed)
}

// ====== ks_atomic_load_acquire ======

/// # Safety
/// Value is live RW memory, aligned to 8, with concurrent accesses exclusively atomic64.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ks_atomic_load_acquire(value: *const u64) -> u64 {
    // SAFETY: the C lock owner guarantees the pointer contract for this call.
    unsafe { AtomicU64::from_ptr(value.cast_mut()) }.load(Ordering::Acquire)
}

// ====== ks_atomic_store_relaxed ======

/// # Safety
/// Value is live RW memory, aligned to 8, with concurrent accesses exclusively atomic64.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ks_atomic_store_relaxed(value: *mut u64, next: u64) {
    // SAFETY: the C lock owner guarantees the pointer contract for this call.
    unsafe { AtomicU64::from_ptr(value) }.store(next, Ordering::Relaxed);
}

// ====== ks_atomic_store_release ======

/// # Safety
/// Value is live RW memory, aligned to 8, with concurrent accesses exclusively atomic64.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ks_atomic_store_release(value: *mut u64, next: u64) {
    // SAFETY: the C lock owner guarantees the pointer contract for this call.
    unsafe { AtomicU64::from_ptr(value) }.store(next, Ordering::Release);
}

// ====== ks_atomic_compare ======

/// # Safety
/// Value is live RW memory, aligned to 8, with concurrent accesses exclusively atomic64.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ks_atomic_compare(value: *mut u64, expected: u64, next: u64) -> u32 {
    // SAFETY: the C lock owner guarantees the pointer contract for this call.
    u32::from(unsafe { AtomicU64::from_ptr(value) }
        .compare_exchange(expected, next, Ordering::Relaxed, Ordering::Relaxed).is_ok())
}
