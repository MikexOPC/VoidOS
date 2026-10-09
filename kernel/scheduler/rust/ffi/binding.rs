use super::{PENDING, kernel_scheduler_binding_context_valid};
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};
#[path = "../irq/bindings.rs"]
mod registry;
#[path = "../payload/channel.rs"]
pub(super) mod channel;

pub(super) struct State {
    pub registry: registry::Bindings,
    pub channels: [channel::Channel; 8],
}
pub(super) struct Storage(pub UnsafeCell<State>);
// SAFETY: exclusive ENTERED acquisition precedes every borrow; no reference escapes.
unsafe impl Sync for Storage {}
pub(super) static STORAGE: Storage = Storage(UnsafeCell::new(State {
    registry: registry::Bindings::new(), channels: [const { channel::Channel::new() }; 8],
}));
static ENTERED: AtomicBool = AtomicBool::new(false);
pub(super) struct Guard;

impl Drop for Guard {
    // ====== binding_guard_drop ======

    fn drop(&mut self) { ENTERED.store(false, Ordering::Release); }
}

// ====== binding_enter ======

pub(super) fn enter() -> Option<Guard> {
    ENTERED.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
        .ok().map(|_| Guard)
}

// ====== kernel_scheduler_irq_bind ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_irq_bind(event: u32, source: u64) -> u64 {
    let Some(_guard) = enter() else { return 0; };
    // SAFETY: backend verifies BSP/IF0/DF0/main stack and no RUNNING task.
    // Callback occurs before borrowing registry storage; reentry returns failure.
    if unsafe { kernel_scheduler_binding_context_valid() } != 1 { return 0; }
    // SAFETY: ENTERED exclusively owns storage, no callbacks during this borrow.
    unsafe { &mut *STORAGE.0.get() }.registry.attach(event, source, PENDING.contains(event))
}

// ====== kernel_scheduler_irq_unbind ======

// Private backend must first verify hardware quiescence; Rust does not stop hardware.
#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_irq_unbind(event: u32, source: u64, generation: u64) -> u32 {
    let Some(_guard) = enter() else { return 0; };
    // SAFETY: same trusted callback contract as bind, before registry borrow.
    if unsafe { kernel_scheduler_binding_context_valid() } != 1 { return 0; }
    // SAFETY: exclusive guard and no escaping reference or callback.
    let state = unsafe { &mut *STORAGE.0.get() };
    if !state.registry.matches(event, source, generation) || state.channels[event as usize].enabled() { return 0; }
    u32::from(state.registry.detach(event, source, generation, PENDING.contains(event)))
}

// ====== kernel_scheduler_irq_post_bound ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_irq_post_bound(event: u32, source: u64, generation: u64) -> u32 {
    let Some(_guard) = enter() else { return 0; };
    // SAFETY: exclusive guard. Trusted BSP/IF0 ISR only, no callbacks/Owner/heap.
    let state = unsafe { &*STORAGE.0.get() };
    if !state.registry.matches(event, source, generation) || state.channels[event as usize].enabled() { return 0; }
    u32::from(PENDING.post(event))
}
