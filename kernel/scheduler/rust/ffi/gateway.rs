#![no_std]

#[path = "../state/owner.rs"]
mod state;

#[path = "../irq/pending.rs"]
mod pending;
mod binding;
mod payload;
mod core_lock;
mod smp;
mod lot;
use core::cell::UnsafeCell;
use state::{Error, Handle, Owner, NONE};

#[repr(C)]
pub struct Reply {
    status: u32,
    index: u32,
    generation: u64,
    state: u32,
    ready: u32,
    events: u64,
}

const _: () = {
    assert!(core::mem::size_of::<Reply>() == 32);
    assert!(core::mem::align_of::<Reply>() == 8);
    assert!(core::mem::offset_of!(Reply, status) == 0);
    assert!(core::mem::offset_of!(Reply, index) == 4);
    assert!(core::mem::offset_of!(Reply, generation) == 8);
    assert!(core::mem::offset_of!(Reply, state) == 16);
    assert!(core::mem::offset_of!(Reply, ready) == 20);
    assert!(core::mem::offset_of!(Reply, events) == 24);
};

struct Storage(UnsafeCell<Option<Owner>>);

// SAFETY: every access to Storage is under the exclusive core_lock guard. No borrow
// escapes an entry point, including errors. IRQ/NMI entry is unsupported.
unsafe impl Sync for Storage {}
static STORAGE: Storage = Storage(UnsafeCell::new(None));
static PENDING: pending::Pending = pending::Pending::new();

// Owner storage of the legacy INIT command: the default capacity, until a backend supplies
// heap storage through kernel_scheduler_init_storage.
const FALLBACK_WORDS: usize = state::default_storage_bytes().div_ceil(8);
struct Fallback(UnsafeCell<[u64; FALLBACK_WORDS]>);

// SAFETY: the array is handed to the owner once, by the one-shot INIT under the core_lock guard.
unsafe impl Sync for Fallback {}
static FALLBACK: Fallback = Fallback(UnsafeCell::new([0; FALLBACK_WORDS]));

unsafe extern "C" {
    // Trusted backend must verify BSP/IF=0/DF=0 and dispatcher, outside IRQ/NMI.
    // Host tests mock this predicate; no CPU verification is claimed by Rust.
    #[cfg(not(scheduler_core_lock))]
    fn kernel_scheduler_context_valid() -> u32;
    fn kernel_scheduler_binding_context_valid() -> u32;
    fn kernel_scheduler_payload_context_valid(consumer: u32, generation: u64) -> u32;
    fn kernel_scheduler_fail_stop() -> !;
}

#[cfg(scheduler_core_lock)]
unsafe extern "C" {
    fn kernel_scheduler_context_valid_on(cpu: u32) -> u32;
}

// ====== gateway_context_valid_on ======

#[cfg(scheduler_core_lock)]
fn context_valid_on(cpu: u32) -> bool {
    unsafe { kernel_scheduler_context_valid_on(cpu) == 1 }
}

#[cfg(not(scheduler_core_lock))]
fn context_valid_on(_cpu: u32) -> bool {
    unsafe { kernel_scheduler_context_valid() == 1 }
}

#[repr(C)]
pub struct EventReply { reply: Reply, epoch: u64, outcome: u32, woken: u32 }
const _: () = {
    assert!(core::mem::size_of::<EventReply>() == 48);
    assert!(core::mem::align_of::<EventReply>() == 8);
    assert!(core::mem::offset_of!(EventReply, epoch) == 32);
    assert!(core::mem::offset_of!(EventReply, outcome) == 40);
    assert!(core::mem::offset_of!(EventReply, woken) == 44);
};

// ====== gateway_reply ======

fn reply(status: u32) -> Reply {
    Reply { status, index: NONE, generation: 0, state: NONE, ready: 0, events: 0 }
}

// ====== gateway_apply ======

// Private C ABI. Native resources must be verified before PUBLISH/ABORT/COLLECT.
// Struct return uses the target ABI; caller must provide valid return storage.
#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_apply(operation: u32, index: u32, generation: u64) -> Reply {
    apply_on(0, operation, index, generation, 9)
}

// ====== gateway_apply_on ======

// Same commands for the dispatcher of `cpu`; operation 10 is PREEMPT (timer-driven yield).
// `limit` keeps the CPU-0 entry point at its original command set.
fn apply_on(cpu: u32, operation: u32, index: u32, generation: u64, limit: u32) -> Reply {
    let Some(_guard) = core_lock::enter() else {
        return reply(9); // BUSY: no owner borrow, no backend callback.
    };
    // SAFETY: the linked backend has the stated C ABI and returns a normalized bit.
    // Callback occurs BEFORE creating any Rust reference to STORAGE.
    if !context_valid_on(cpu) { return reply(10); }
    if operation > limit { return reply(12); }
    if matches!(operation, 0 | 1 | 4 | 9) && (index != NONE || generation != 0) {
        return reply(12);
    }
    // SAFETY: the core_lock guard exclusively owns Storage; no callbacks during borrow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    if operation == 0 {
        if storage.is_some() { return reply(13); }
        let base = FALLBACK.0.get().cast::<u8>();
        // SAFETY: FALLBACK is 8-aligned, spans default_storage_bytes and is used only by this
        // one-shot under the core_lock guard.
        return match unsafe { Owner::from_storage(base, state::default_storage_bytes(), state::CAPACITY) } {
            Ok(owner) => { *storage = Some(owner); reply(0) }
            Err(error) => reply(error as u32),
        };
    }
    let Some(owner) = storage.as_mut() else { return reply(11); };
    let handle = Handle { index, generation };
    let result: Result<Handle, Error> = match operation {
        1 => owner.reserve_on(cpu),
        2 => owner.publish_on(cpu, handle).map(|()| handle),
        3 => owner.abort_on(cpu, handle).map(|()| handle),
        4 => owner.select_on(cpu),
        5 => owner.yield_on(cpu, handle).map(|()| handle),
        6 => owner.exit_on(cpu, handle).map(|()| handle),
        7 => owner.collect_on(cpu, handle).map(|()| handle),
        8 => owner.state(handle).map(|_| handle),
        9 => owner.current_on(cpu),
        10 => owner.preempt_on(cpu, handle).map(|()| handle),
        _ => return reply(12),
    };
    let mut output = match result {
        Ok(handle) => {
            let mut output = reply(0);
            output.index = handle.index;
            output.generation = handle.generation;
            output.state = match owner.state(handle) {
                Ok(state) => state as u32,
                Err(_) => return reply(1),
            };
            output
        }
        Err(error) => reply(error as u32),
    };
    if let Ok((ready, events)) = owner.snapshot() {
        output.ready = ready;
        output.events = events;
    }
    output
}

// ====== gateway_storage_bytes ======

// Bytes of storage that an owner of `capacity` tasks needs; 0 when the capacity is not supported.
#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_storage_bytes(capacity: u32) -> u64 {
    state::storage_bytes(capacity as usize).map_or(0, |bytes| bytes as u64)
}

// ====== gateway_init_storage ======

/// One-shot initialization of the owner on caller storage, like INIT with a chosen capacity.
///
/// # Safety
///
/// `buffer` must be valid for reads and writes of `bytes` bytes and stay valid and unused by
/// anything else for as long as the scheduler runs. Null, a buffer that is not 8-aligned, a short
/// buffer and a capacity outside the supported range are rejected without touching the buffer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn kernel_scheduler_init_storage(buffer: *mut u8, bytes: u64, capacity: u32) -> Reply {
    let Some(_guard) = core_lock::enter() else { return reply(9); };
    if !context_valid_on(0) { return reply(10); }
    // SAFETY: the core_lock guard owns Storage; no callbacks during this borrow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    if let Some(existing) = storage.as_ref()
        && (!existing.is_pristine() || (capacity as usize) <= existing.capacity()) {
        return reply(13);
    }
    let Ok(bytes) = usize::try_from(bytes) else { return reply(12); };
    // SAFETY: the caller contract above; from_storage validates before touching the buffer.
    match unsafe { Owner::from_storage(buffer, bytes, capacity as usize) } {
        Ok(owner) => { *storage = Some(owner); reply(0) }
        Err(error) => reply(error as u32),
    }
}

// ====== gateway_event_apply ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_event_apply(operation: u32, index: u32, generation: u64,
    event: u32, observed: u64) -> EventReply {
    event_apply_on(0, operation, index, generation, event, observed)
}

// ====== gateway_event_apply_on ======

fn event_apply_on(cpu: u32, operation: u32, index: u32, generation: u64,
    event: u32, observed: u64) -> EventReply {
    let mut output = EventReply { reply: reply(0), epoch: 0, outcome: u32::MAX, woken: 0 };
    let Some(_guard) = core_lock::enter() else { output.reply = reply(9); return output; };
    // SAFETY: trusted ABI callback, before borrowing the exclusively owned storage.
    if !context_valid_on(cpu) {
        output.reply = reply(10); return output;
    }
    if operation > 2 || (operation != 1 && observed != 0) {
        output.reply = reply(12); return output;
    }
    // SAFETY: same exclusive guard as apply; no references escape or callbacks follow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { output.reply = reply(11); return output; };
    let handle = Handle { index, generation };
    let result: Result<state::EventResult, Error> = match operation {
        0 => owner.observe_on(cpu, handle, event),
        1 => owner.wait_on(cpu, handle, event, observed),
        2 => owner.signal_on(cpu, handle, event),
        _ => unreachable!(),
    };
    match result {
        Ok(result) => {
            let Ok(state) = owner.state(handle) else { output.reply = reply(1); return output; };
            output.reply.index = index; output.reply.generation = generation;
            output.reply.state = state as u32;
            output.epoch = result.epoch; output.outcome = result.outcome; output.woken = result.woken;
        }
        Err(error) => output.reply = reply(error as u32),
    }
    if let Ok((ready, events)) = owner.snapshot() {
        output.reply.ready = ready; output.reply.events = events;
    }
    output
}

// ====== gateway_stop_apply ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_stop_apply(operation: u32, index: u32,
    generation: u64) -> EventReply {
    stop_apply_on(0, operation, index, generation)
}

// ====== gateway_stop_apply_on ======

fn stop_apply_on(cpu: u32, operation: u32, index: u32, generation: u64) -> EventReply {
    let mut output = EventReply { reply: reply(0), epoch: 0, outcome: u32::MAX, woken: 0 };
    let Some(_guard) = core_lock::enter() else { output.reply = reply(9); return output; };
    // SAFETY: trusted context callback before creating any storage reference.
    if !context_valid_on(cpu) { output.reply = reply(10); return output; }
    if operation > 1 { output.reply = reply(12); return output; }
    // SAFETY: the core_lock guard owns the storage; no callback or switch during this borrow.
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { output.reply = reply(11); return output; };
    let handle = Handle { index, generation };
    let result = if operation == 0 {
        owner.request_stop_on(cpu, handle).map(|woken| (1, woken))
    } else { owner.stop_requested(handle).map(|stopped| (u32::from(stopped), 0)) };
    match result {
        Ok((outcome, woken)) => {
            let Ok(state) = owner.state(handle) else { output.reply = reply(1); return output; };
            output.reply.index = index; output.reply.generation = generation;
            output.reply.state = state as u32; output.outcome = outcome; output.woken = woken;
        }
        Err(error) => output.reply = reply(error as u32),
    }
    if let Ok((ready, events)) = owner.snapshot() {
        output.reply.ready = ready; output.reply.events = events;
    }
    output
}

// ====== gateway_irq_post ======

// Private trusted ISR API: backend verifies bound source/lifetime before entry.
// No owner access, borrow, callback, allocation, diagnostic or panic path.
#[unsafe(no_mangle)]
#[cfg(scheduler_host_unbound)]
pub extern "C" fn kernel_scheduler_irq_post(event: u32) -> u32 {
    u32::from(PENDING.post(event))
}

// ====== gateway_irq_drain ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_scheduler_irq_drain() -> EventReply {
    let mut output = EventReply { reply: reply(0), epoch: 0, outcome: 4, woken: 0 };
    let Some(_guard) = core_lock::enter() else { output.reply = reply(9); return output; };
    // SAFETY: trusted ABI callback before creating the exclusive owner borrow.
    if !context_valid_on(0) { output.reply = reply(10); return output; }
    // SAFETY: identical core_lock discipline; no callbacks while this borrow exists.
    let storage = unsafe { &mut *STORAGE.0.get() };
    let Some(owner) = storage.as_mut() else { output.reply = reply(11); return output; };
    if PENDING.empty() { return output; }
    let mask = PENDING.take();
    match owner.signal_pending(mask) {
        Ok(result) => output.woken = result.woken,
        Err(error) => {
            // Retain all notifications on rejection; concurrent posts stay pending.
            if !PENDING.restore(mask) { output.reply = reply(1); return output; }
            output.reply = reply(error as u32);
        }
    }
    if let Ok((ready, events)) = owner.snapshot() {
        output.reply.ready = ready; output.reply.events = events;
    }
    output
}

// ====== gateway_panic ======

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    // SAFETY: native terminal backend must never return. No reset/cleanup here.
    unsafe { kernel_scheduler_fail_stop() }
}
