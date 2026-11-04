use core::sync::atomic::{AtomicBool, Ordering};

static ENTERED: AtomicBool = AtomicBool::new(false);

#[cfg(scheduler_core_lock)]
unsafe extern "C" {
    fn kernel_scheduler_lock_acquire() -> u32;
    fn kernel_scheduler_lock_release();
}

// ====== core_lock_acquire ======

#[cfg(scheduler_core_lock)]
fn lock_acquire() -> bool {
    unsafe { kernel_scheduler_lock_acquire() == 1 }
}

#[cfg(not(scheduler_core_lock))]
fn lock_acquire() -> bool { true }

// ====== core_lock_release ======

#[cfg(scheduler_core_lock)]
fn lock_release() {
    unsafe { kernel_scheduler_lock_release() }
}

#[cfg(not(scheduler_core_lock))]
fn lock_release() {}

pub(super) struct Guard(());

// ====== core_lock_enter ======

pub(super) fn enter() -> Option<Guard> {
    if !lock_acquire() { return None; }
    if ENTERED.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
        lock_release();
        return None;
    }
    Some(Guard(()))
}

// ====== core_lock_drop ======

impl Drop for Guard {
    fn drop(&mut self) {
        ENTERED.store(false, Ordering::Release);
        lock_release();
    }
}
