use super::{PENDING, binding, kernel_scheduler_binding_context_valid, kernel_scheduler_payload_context_valid};

// ====== kernel_scheduler_payload_open ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_payload_open(event: u32, source: u64, token: u64, consumer: u32) -> u32 {
    let Some(_guard) = binding::enter() else { return 0; };
    // SAFETY: trusted BSP/IF0/backend callback, before any registry/channel borrow.
    if unsafe { kernel_scheduler_binding_context_valid() } != 1 { return 0; }
    // SAFETY: same exclusive guard as bind/unbind/post; no reference escapes.
    let state = unsafe { &mut *binding::STORAGE.0.get() };
    if !state.registry.matches(event, source, token) || PENDING.contains(event) { return 0; }
    u32::from(state.channels[event as usize].open(consumer))
}

// ====== kernel_scheduler_payload_push ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_payload_push(event: u32, source: u64, token: u64, byte: u32) -> u32 {
    let Some(_guard) = binding::enter() else { return 0; };
    // SAFETY: trusted BSP/IF0 ISR, shared guard, no Owner/heap/callback/log.
    let state = unsafe { &mut *binding::STORAGE.0.get() };
    if !state.registry.matches(event, source, token) { return 0; }
    let result = state.channels[event as usize].push(byte);
    // Both accepted byte and ordered loss are conditions that require a wakeup.
    if result != 0 && !PENDING.post(event) { return 0; }
    result
}

// ====== kernel_scheduler_payload_pop ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_payload_pop(event: u32, source: u64, token: u64,
    consumer: u32, generation: u64) -> u64 {
    let Some(_guard) = binding::enter() else { return 8; };
    // SAFETY: native callback authenticates actual RUNNING task, BSP/IF0/DF0.
    if unsafe { kernel_scheduler_payload_context_valid(consumer, generation) } != 1 { return 4; }
    // SAFETY: guard before borrow, callback already returned, no reference escapes.
    let state = unsafe { &mut *binding::STORAGE.0.get() };
    if !state.registry.matches(event, source, token) { return 4; }
    state.channels[event as usize].take(consumer, generation)
}

// ====== kernel_scheduler_payload_close ======

// Caller first proves hardware stop/EOI/quiescence; this does not discard data.
#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_payload_close(event: u32, source: u64, token: u64) -> u32 {
    let Some(_guard) = binding::enter() else { return 0; };
    // SAFETY: native dispatcher/final-session predicate before any borrow.
    if unsafe { kernel_scheduler_binding_context_valid() } != 1 { return 0; }
    // SAFETY: same guard as all channel/binding operations.
    let state = unsafe { &mut *binding::STORAGE.0.get() };
    if !state.registry.matches(event, source, token) || PENDING.contains(event) { return 0; }
    u32::from(state.channels[event as usize].close())
}
